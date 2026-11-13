#include "bluetoothcontroller.h"
#include "dbushelpers.h"
#include <QDBusObjectPath>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <algorithm>
using BluezInterfaces = QMap<QString, QVariantMap>;
using BluezObjects = QMap<QDBusObjectPath, BluezInterfaces>;
Q_DECLARE_METATYPE(BluezInterfaces)
Q_DECLARE_METATYPE(BluezObjects)
class BluetoothWorkerThread final : public QThread {
public:
    explicit BluetoothWorkerThread(BluetoothController *owner) : owner_(owner) {}
    void stop() { requestInterruption(); requestRefresh(); }
    void requestRefresh() { QMutexLocker lock(&mutex_); refresh_ = true; wake_.wakeAll(); }
protected:
    void run() override {
        qDBusRegisterMetaType<BluezInterfaces>(); qDBusRegisterMetaType<BluezObjects>();
        while (!isInterruptionRequested()) {
            const auto reply = LinuxBus::call(QDBusConnection::systemBus(), "org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
            const bool available = reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty();
            QList<DeviceInfo> devices;
            if (available) {
                const auto objects = qdbus_cast<BluezObjects>(reply.arguments().first());
                for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
                    if (!it.value().contains("org.bluez.Device1")) continue;
                    const auto props = it.value().value("org.bluez.Device1");
                    if (!props.value("Paired").toBool() && !props.value("Connected").toBool()) continue;
                    DeviceInfo device;
                    device.id = it.key().path(); device.name = props.value("Alias", props.value("Name")).toString();
                    device.kind = deviceKindFromClass(props.value("Class").toUInt()); device.connected = props.value("Connected").toBool();
                    const auto battery = it.value().value("org.bluez.Battery1").value("Percentage");
                    if (battery.isValid()) device.battery = std::clamp(battery.toInt(), 0, 100);
                    devices.append(device);
                }
                std::sort(devices.begin(), devices.end(), [](const DeviceInfo &a, const DeviceInfo &b) {
                    return a.connected != b.connected ? a.connected : a.name.compare(b.name, Qt::CaseInsensitive) < 0;
                });
            }
            const auto changes = available ? tracker_.update(devices) : QList<DeviceChange>{};
            if (!available) tracker_.reset();
            const auto sequence = ++sequence_;
            QMetaObject::invokeMethod(owner_, [owner = owner_, sequence, available, devices, changes] {
                owner->apply(sequence, available, devices,
                    available ? QString() : QStringLiteral("Bluetooth недоступний. Перевірте службу BlueZ і адаптер."), changes);
            }, Qt::QueuedConnection);
            QMutexLocker lock(&mutex_);
            if (!refresh_ && !isInterruptionRequested()) wake_.wait(&mutex_, 3000);
            refresh_ = false;
        }
    }
private:
    BluetoothController *owner_;
    QMutex mutex_; QWaitCondition wake_; bool refresh_ = false;
    DeviceTracker tracker_; quint64 sequence_ = 0;
};

BluetoothController::BluetoothController(QObject *parent)
    : QObject(parent), worker_(std::make_unique<BluetoothWorkerThread>(this)) {
    worker_->start();
}

BluetoothController::~BluetoothController() {
    worker_->stop();
    worker_->wait();
}

void BluetoothController::refresh() {
    worker_->requestRefresh();
}

void BluetoothController::apply(quint64 sequence, bool available, const QList<DeviceInfo> &devices,
    const QString &error, const QList<DeviceChange> &changes) {
    if (sequence <= lastSequence_) return;  // stale result
    lastSequence_ = sequence;

    QVariantList list;
    int connected = 0;
    for (const DeviceInfo &d : devices) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), d.id);
        map.insert(QStringLiteral("name"), d.name);
        map.insert(QStringLiteral("kind"), d.kind);
        map.insert(QStringLiteral("connected"), d.connected);
        map.insert(QStringLiteral("battery"), d.battery);
        list.append(map);
        if (d.connected) ++connected;
    }
    if (available != available_ || list != devices_ || error != error_) {
        available_ = available;
        devices_ = std::move(list);
        connectedCount_ = connected;
        error_ = error;
        emit changed();
    }
    for (const DeviceChange &c : changes)
        emit deviceEvent(c.device.id, c.device.name, c.connected, c.device.battery);
}
