#pragma once

#include <QDir>
#include <QString>

// Helpers for the "start with Windows" registry value. Pure string logic.

inline QString startupCommand(const QString &exePath) {
    return QLatin1Char('"') + QDir::toNativeSeparators(exePath) + QLatin1Char('"');
}

inline bool startupCommandMatches(const QString &registryValue, const QString &exePath) {
    QString value = registryValue.trimmed();
    if (value.isEmpty() || exePath.trimmed().isEmpty()) return false;
    if (value.startsWith(QLatin1Char('"'))) {
        const auto close = value.indexOf(QLatin1Char('"'), 1);
        // Anything after the closing quote is command-line arguments.
        value = close < 0 ? value.mid(1) : value.mid(1, close - 1);
    }
    const auto normalise = [](QString path) {
        return path.trimmed().replace(QLatin1Char('/'), QLatin1Char('\\')).toLower();
    };
    return normalise(value) == normalise(exePath);
}
