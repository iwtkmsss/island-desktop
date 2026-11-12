#pragma once

#include <QObject>
#include <QString>

// Per-user "start with the system" entry. Windows: HKCU Run key.
class StartupController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    explicit StartupController(const QString &registryKey = defaultKey(), QObject *parent = nullptr);
    static QString defaultKey();
    bool available() const;
    bool enabled() const { return enabled_; }
    QString error() const { return error_; }
    void setEnabled(bool enabled);
signals:
    void enabledChanged();
    void errorChanged();
private:
    bool readEnabled() const;
    void setError(const QString &error);
    QString key_;
    bool enabled_ = false;
    QString error_;
};
