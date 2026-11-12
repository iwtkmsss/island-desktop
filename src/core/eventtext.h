#pragma once

#include "eventqueue.h"
#include "powertracker.h"
#include <QString>

inline IslandEvent powerIslandEvent(PowerEvent event, int percent) {
    IslandEvent e;
    e.kind = QStringLiteral("power");
    e.coalesceKey = QStringLiteral("power");
    e.priority = 60;
    e.durationMs = 4000;
    const QString suffix = percent >= 0 ? QStringLiteral(" — %1%").arg(percent) : QString();
    switch (event) {
    case PowerEvent::Plugged:
        e.title = QStringLiteral("Зарядку під'єднано") + suffix;
        e.icon = QStringLiteral("plug");
        break;
    case PowerEvent::Unplugged:
        e.title = QStringLiteral("Зарядку від'єднано") + suffix;
        e.icon = QStringLiteral("battery");
        break;
    case PowerEvent::Low:
        e.title = QStringLiteral("Батарея %1%").arg(percent);
        e.icon = QStringLiteral("battery");
        e.priority = 80;
        e.durationMs = 5000;
        break;
    case PowerEvent::Critical:
        e.title = QStringLiteral("Батарея %1% — підключіть зарядку").arg(percent);
        e.icon = QStringLiteral("battery");
        e.priority = 90;
        e.durationMs = 8000;
        break;
    case PowerEvent::None:
        break;
    }
    return e;
}

inline IslandEvent deviceIslandEvent(const QString &id, const QString &name, bool connected, int battery) {
    IslandEvent e;
    const QString label = name.isEmpty() ? QStringLiteral("Пристрій") : name;
    if (!connected) e.title = label + QStringLiteral(" відключено");
    else if (battery >= 0) e.title = label + QStringLiteral(" підключено — %1%").arg(battery);
    else e.title = label + QStringLiteral(" підключено");
    e.icon = QStringLiteral("bluetooth");
    e.kind = QStringLiteral("device");
    e.priority = 60;
    e.durationMs = 4000;
    e.coalesceKey = QStringLiteral("device:") + id;
    return e;
}
