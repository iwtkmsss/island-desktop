#include "startupcontroller.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
namespace {
QByteArray entry() {
    auto executable = QCoreApplication::applicationFilePath();
    executable.replace('\\', "\\\\").replace('"', "\\\"").replace('`', "\\`").replace('$', "\\$").replace('%', "%%");
    // Desktop Entry string escaping is applied after Exec argument escaping.
    executable.replace('\\', "\\\\");
    return "[Desktop Entry]\nType=Application\nName=Island Desktop\nExec=\"" + executable.toUtf8()
        + "\"\nTerminal=false\nX-GNOME-Autostart-enabled=true\n";
}
}
QString StartupController::defaultKey() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/io.github.iwtkmsss.island-desktop.desktop";
}
StartupController::StartupController(const QString &path, QObject *parent) : QObject(parent), key_(path) { enabled_ = readEnabled(); }
bool StartupController::available() const { return !key_.isEmpty(); }
bool StartupController::readEnabled() const {
    QFile file(key_); return file.open(QIODevice::ReadOnly) && file.readAll() == entry();
}
void StartupController::setError(const QString &error) { if (error_ != error) { error_ = error; emit errorChanged(); } }
void StartupController::setEnabled(bool enabled) {
    bool ok = true;
    if (enabled) {
        ok = QDir().mkpath(QFileInfo(key_).absolutePath());
        QSaveFile file(key_);
        ok = ok && file.open(QIODevice::WriteOnly);
        if (ok) ok = file.write(entry()) == entry().size() && file.commit();
    } else if (QFile::exists(key_)) ok = QFile::remove(key_);
    setError(ok ? QString() : QStringLiteral("Не вдалося змінити автозапуск користувача."));
    const bool actual = readEnabled();
    if (actual != enabled_) { enabled_ = actual; emit enabledChanged(); }
}
