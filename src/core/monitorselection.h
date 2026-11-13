#pragma once

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QList>
#include <QRect>
#include <QString>
#include <QVariantList>

struct MonitorInfo {
    QString id;
    QString label;
    bool primary = false;
};

// Never use the enumeration index: Windows can reorder screens on hotplug.
// A hardware serial survives connector renumbering. Without a usable serial,
// the connector name distinguishes otherwise identical monitors.
inline QString monitorId(const QString &manufacturer, const QString &model,
                         const QString &serial, const QString &name) {
    auto serialKey = serial.trimmed();
    // Some EDIDs report an all-zero placeholder rather than a serial number.
    if (serialKey.count(QLatin1Char('0')) == serialKey.size()) serialKey.clear();
    const QJsonArray identity{manufacturer.trimmed(), model.trimmed(), serialKey,
        serialKey.isEmpty() ? name : QString{}};
    return QStringLiteral("screen-v1-") + QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

inline QString resolvedMonitorId(const QList<MonitorInfo> &monitors, const QString &selectedId) {
    if (!selectedId.isEmpty()) {
        for (const auto &monitor : monitors)
            if (monitor.id == selectedId) return monitor.id;
    }
    for (const auto &monitor : monitors)
        if (monitor.primary) return monitor.id;
    // During a topology change Qt may briefly have screens but no primary yet.
    return monitors.isEmpty() ? QString{} : monitors.first().id;
}

inline QVariantList monitorOptions(const QList<MonitorInfo> &monitors,
                                   const QString &selectedId, const QString &savedLabel) {
    QVariantList options{QVariantMap{{"id", QString{}},
        {"label", QStringLiteral("Автоматично — основний екран")}}};
    bool selectedAvailable = selectedId.isEmpty();
    for (const auto &monitor : monitors) {
        options.append(QVariantMap{{"id", monitor.id}, {"label", monitor.label
            + (monitor.primary ? QStringLiteral(" — основний") : QString{})}});
        selectedAvailable |= monitor.id == selectedId;
    }
    if (!selectedAvailable) {
        options.append(QVariantMap{{"id", selectedId}, {"label",
            (savedLabel.isEmpty() ? QStringLiteral("Вибраний монітор") : savedLabel)
                + QStringLiteral(" — недоступний")}});
    }
    return options;
}

// QML supplies the largest fixed frame, never the animated shell dimensions.
inline QRect monitorFrameRect(const QRect &available, const QSize &frame) {
    return {available.x() + (available.width() - frame.width()) / 2,
        available.y() + 2, frame.width(), frame.height()};
}

inline bool validDockEdge(const QString &edge) {
    return edge == "top" || edge == "bottom" || edge == "left" || edge == "right";
}
inline QRect dockFrameRect(const QRect &screen, const QSize &frame, const QString &edge) {
    int x = screen.x() + (screen.width()-frame.width())/2;
    int y = screen.y() + (screen.height()-frame.height())/2;
    if (edge == "left") x = screen.left();
    else if (edge == "right") x = screen.right()+1-frame.width();
    else if (edge == "bottom") y = screen.bottom()+1-frame.height();
    else y = screen.top();
    return {QPoint(x, y), frame};
}
