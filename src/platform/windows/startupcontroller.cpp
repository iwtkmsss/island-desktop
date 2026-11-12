#include "startupcontroller.h"
#include "startupentry.h"

#include <QCoreApplication>
#include <QSettings>

namespace {
const QString valueName = QStringLiteral("IslandDesktop");
}

QString StartupController::defaultKey() {
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}

#ifdef Q_OS_WIN

StartupController::StartupController(const QString &registryKey, QObject *parent)
    : QObject(parent), key_(registryKey) {
    enabled_ = readEnabled();
}

bool StartupController::available() const { return true; }

bool StartupController::readEnabled() const {
    QSettings settings(key_, QSettings::NativeFormat);
    if (!settings.contains(valueName)) return false;
    return startupCommandMatches(settings.value(valueName).toString(), QCoreApplication::applicationFilePath());
}

void StartupController::setError(const QString &error) {
    if (error_ == error) return;
    error_ = error;
    emit errorChanged();
}

void StartupController::setEnabled(bool enabled) {
    bool failed = false;
    {
        QSettings settings(key_, QSettings::NativeFormat);
        if (enabled) settings.setValue(valueName, startupCommand(QCoreApplication::applicationFilePath()));
        else settings.remove(valueName);
        settings.sync();
        failed = settings.status() != QSettings::NoError;
    }
    setError(failed ? QStringLiteral("Не вдалося змінити автозапуск у реєстрі Windows.") : QString());
    const bool actual = readEnabled();
    if (actual != enabled_) {
        enabled_ = actual;
        emit enabledChanged();
    }
}

#else

StartupController::StartupController(const QString &registryKey, QObject *parent)
    : QObject(parent), key_(registryKey) {}
bool StartupController::available() const { return false; }
bool StartupController::readEnabled() const { return false; }
void StartupController::setError(const QString &) {}
void StartupController::setEnabled(bool) {}

#endif
