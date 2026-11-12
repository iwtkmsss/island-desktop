#pragma once

#include "devicetracker.h"
#include <QObject>
#include <QString>
#include <QVariantList>
#include <memory>

class BluetoothWorkerThread;

// Paired Bluetooth devices, their connected state and (classic devices only)
// battery level. Scanning runs on a controlled worker thread.
class BluetoothController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(int connectedCount READ connectedCount NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit BluetoothController(QObject *parent = nullptr);
    ~BluetoothController() override;

    bool available() const { return available_; }
    QVariantList devices() const { return devices_; }
    int connectedCount() const { return connectedCount_; }
    QString error() const { return error_; }

    Q_INVOKABLE void refresh();

signals:
    void changed();
    void deviceEvent(const QString &id, const QString &name, bool connected, int battery);

private:
    friend class BluetoothWorkerThread;
    void apply(quint64 sequence, bool available, const QList<DeviceInfo> &devices, const QString &error,
        const QList<DeviceChange> &changes);

    std::unique_ptr<BluetoothWorkerThread> worker_;
    quint64 lastSequence_ = 0;
    bool available_ = false;
    QVariantList devices_;
    int connectedCount_ = 0;
    QString error_;
};
