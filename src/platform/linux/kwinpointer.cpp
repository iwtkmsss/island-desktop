#include "kwinpointer.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScreen>
#include <cmath>

namespace {
QDBusMessage scripting(const QString &method, const QVariantList &args) {
    auto message = QDBusMessage::createMethodCall("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", method);
    message.setArguments(args); return message;
}
QString quoted(const QString &text) {
    auto json = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}
}
KWinPointer::KWinPointer(const QString &identity, QObject *parent) : QObject(parent) {
    const auto token = QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex().left(20);
    plugin_ = "island-desktop-pointer-" + QString::fromLatin1(token);
    path_ = "/IslandDesktop/Pointer" + QString::fromLatin1(token);
    auto bus = QDBusConnection::sessionBus();
    registered_ = bus.registerObject(path_, this, QDBusConnection::ExportAllSlots);
    QFile source(":/linux/kwin-pointer.js");
    if (!registered_ || !temporary_.isValid() || !source.open(QIODevice::ReadOnly)) return;
    auto code = QString::fromUtf8(source.readAll());
    code.replace("@SERVICE@", quoted(bus.baseService())).replace("@PATH@", quoted(path_)).replace("@PLUGIN@", quoted(plugin_));
    script_ = temporary_.filePath("pointer.js");
    QSaveFile file(script_);
    if (!file.open(QIODevice::WriteOnly) || file.write(code.toUtf8()) < 0 || !file.commit()) { script_.clear(); return; }
    watcher_.setConnection(bus);
    watcher_.setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    watcher_.addWatchedService("org.kde.KWin");
    connect(&watcher_, &QDBusServiceWatcher::serviceOwnerChanged, this,
        [this](const QString &, const QString &, const QString &owner) {
            ++epoch_; owner_ = owner; setAvailable(false);
            if (enabled_ && !owner.isEmpty()) start(owner);
        });
}
KWinPointer::~KWinPointer() {
    stop();
    if (registered_) QDBusConnection::sessionBus().unregisterObject(path_);
}
void KWinPointer::setAvailable(bool available) {
    if (available_ == available) return;
    available_ = available; emit availabilityChanged();
}
void KWinPointer::setEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    if (!enabled) { stop(); return; }
    if (!registered_ || script_.isEmpty()) return;
    const auto epoch = ++epoch_;
    auto bus = QDBusConnection::sessionBus();
    auto request = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner");
    request << QStringLiteral("org.kde.KWin");
    auto *call = new QDBusPendingCallWatcher(bus.asyncCall(request, 500), this);
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, epoch](QDBusPendingCallWatcher *call) {
        QDBusPendingReply<QString> reply = *call; call->deleteLater();
        if (epoch != epoch_ || !enabled_ || reply.isError()) return;
        owner_ = reply.value(); start(owner_);
    });
}
void KWinPointer::start(const QString &owner) {
    if (script_.isEmpty() || !enabled_) return;
    const auto epoch = ++epoch_;
    owner_ = owner; setAvailable(false);
    // A previous crash may have left this profile's script behind. Other
    // profiles use distinct keys and are never stopped by this instance.
    auto bus = QDBusConnection::sessionBus();
    auto *cleanup = new QDBusPendingCallWatcher(bus.asyncCall(scripting("unloadScript", {plugin_}), 500), this);
    connect(cleanup, &QDBusPendingCallWatcher::finished, this, [this, epoch](QDBusPendingCallWatcher *cleanup) {
        cleanup->deleteLater(); if (epoch != epoch_ || !enabled_) return;
        auto *load = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(scripting("loadScript", {script_, plugin_}), 1000), this);
        connect(load, &QDBusPendingCallWatcher::finished, this, [this, epoch](QDBusPendingCallWatcher *load) {
            QDBusPendingReply<int> reply = *load; load->deleteLater();
            if (epoch != epoch_ || !enabled_ || reply.isError() || reply.value() < 0) return;
            auto run = QDBusMessage::createMethodCall("org.kde.KWin", "/Scripting/Script" + QString::number(reply.value()), "org.kde.kwin.Script", "run");
            QDBusConnection::sessionBus().asyncCall(run, 1000);
        });
    });
}
void KWinPointer::stop() {
    ++epoch_; setAvailable(false);
    // Bound teardown, and remove the observer before its endpoint/file vanish.
    if (enabled_ || !owner_.isEmpty())
        QDBusConnection::sessionBus().call(scripting("unloadScript", {plugin_}), QDBus::Block, 250);
    owner_.clear();
}
bool KWinPointer::Update(const QString &payload) {
    if (!enabled_ || !calledFromDBus() || message().service() != owner_ || payload.size() > 4096) return false;
    const auto values = QJsonDocument::fromJson(payload.toUtf8()).object();
    for (const auto *key : {"x", "y", "left", "top", "width", "height", "movement"})
        if (!values.value(key).isDouble() || !std::isfinite(values.value(key).toDouble())) return false;
    const double x = values.value("x").toDouble(), y = values.value("y").toDouble();
    const double left = values.value("left").toDouble(), top = values.value("top").toDouble();
    const double width = values.value("width").toDouble(), height = values.value("height").toDouble();
    const auto screen = values.value("screen").toString();
    if (width <= 0 || height <= 0) return false;
    const double movement = values.value("movement").toDouble();
    if (movement < 0 || movement > 9007199254740991.0 || std::floor(movement) != movement) return false;
    const auto serial = quint64(movement);
    const QPointF next(x, y);
    if (available_ && serial != sourceMovement_) ++movementSerial_;
    sourceMovement_ = serial;
    position_ = next; screen_ = screen; output_ = QRectF(left, top, width, height);
    setAvailable(true); return true;
}
QPointF KWinPointer::position() const {
    // KWin and XWayland/Qt can use different scales and monitor origins. Map
    // through the named output instead of multiplying the whole desktop by DPR.
    for (const auto *screen : QGuiApplication::screens()) if (screen->name() == screen_ && output_.isValid()) {
        const auto geometry = screen->geometry();
        return QPointF(geometry.x() + (position_.x() - output_.x()) * geometry.width() / output_.width(),
                       geometry.y() + (position_.y() - output_.y()) * geometry.height() / output_.height());
    }
    return position_;
}
