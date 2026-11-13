#pragma once
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QVariantMap>
namespace LinuxBus {
inline QVariantMap map(const QVariant &value) {
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) return qdbus_cast<QVariantMap>(value);
    return value.toMap();
}
inline QDBusMessage call(const QDBusConnection &bus, const QString &service, const QString &path,
                        const QString &interface, const QString &method, const QVariantList &args = {}) {
    auto message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(args);
    return bus.call(message, QDBus::Block, 350); // Used only by workers, never the GUI.
}
inline QVariantMap properties(const QDBusConnection &bus, const QString &service, const QString &path, const QString &interface) {
    const auto reply = call(bus, service, path, "org.freedesktop.DBus.Properties", "GetAll", {interface});
    return reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty() ? map(reply.arguments().first()) : QVariantMap{};
}
}
