#pragma once
#include <QObject>
#include <QSettings>
#include <memory>

class TaskbarController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool hidden READ hidden WRITE setHidden NOTIFY hiddenChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    explicit TaskbarController(const QString &settingsFile, QObject *parent = nullptr);
    ~TaskbarController() override;
    static void setControlAllowed(bool allowed);
    bool available() const;
    bool hidden() const { return hidden_; }
    QString error() const { return error_; }
    void setHidden(bool hidden);
signals:
    void hiddenChanged();
    void errorChanged();
private:
    void fail(const QString &error);
    class Native;
    std::unique_ptr<Native> native_;
    QSettings settings_;
    bool hidden_ = false;
    QString error_;
};
