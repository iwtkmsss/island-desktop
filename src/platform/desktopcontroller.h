#pragma once
#include <QObject>
#include <QVariantList>
#include <QSettings>
#include <QIcon>
#include <QAbstractItemModel>
#ifdef Q_OS_LINUX
#include <taskmanager/tasksmodel.h>
#include <QDBusServiceWatcher>
#endif
class DesktopController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QAbstractItemModel* tasks READ tasks CONSTANT)
    Q_PROPERTY(QVariantList trayItems READ trayItems NOTIFY trayChanged)
    Q_PROPERTY(QString metrics READ metrics NOTIFY metricsChanged)
    Q_PROPERTY(bool metricsActive READ metricsActive WRITE setMetricsActive)
    Q_PROPERTY(bool menuOpen READ menuOpen NOTIFY menuOpenChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // Bumped on any task change; QML bindings on taskState() depend on it.
    Q_PROPERTY(int taskRevision READ taskRevision NOTIFY taskRevisionChanged)
public:
    explicit DesktopController(const QString &settingsFile, QObject *parent=nullptr);
    bool available() const;
    QString metrics() const { return metrics_; }
    bool metricsActive() const { return metricsActive_; }
    void setMetricsActive(bool active) { metricsActive_=active; }
    QAbstractItemModel *tasks();
    QVariantList trayItems() const { return items_; }
    bool menuOpen() const { return menuOpen_; }
    QString error() const { return error_; }
    int taskRevision() const { return taskRevision_; }
    // {count: windows in the task (1 for a single window), active: index of the
    // active window or -1}. Counts the window the island's own focus displaced.
    Q_INVOKABLE QVariantMap taskState(int row) const;
    QIcon trayIcon(const QString &id) const { return icons_.value(id); }
    Q_INVOKABLE void activateTask(int row);
    Q_INVOKABLE void taskMenu(int row);
    Q_INVOKABLE void moveTask(int from, int to);
    Q_INVOKABLE void applicationMenu();
    Q_INVOKABLE void trayAction(const QString &id, const QString &action, int delta=0);
    Q_INVOKABLE void openSettings(const QString &module);
signals:
    void metricsChanged();
    void trayChanged();
    void menuOpenChanged();
    void errorChanged();
    void taskRevisionChanged();
private slots:
    void refreshTray();
private:
    void fail(const QString &message);
    void showMenu(class QMenu *menu);
    void trayMenu(const QString &service, const QString &path);
    QSettings settings_;
    QVariantList items_;
    QHash<QString,QIcon> icons_;
    QHash<QString,size_t> iconHashes_;
    QString metrics_;
    bool metricsActive_=false;
    quint64 cpuTotal_=0,cpuIdle_=0;
    bool menuOpen_=false;
    QString error_;
    int taskRevision_=0;
#ifdef Q_OS_LINUX
    TaskManager::TasksModel tasks_;
    QPersistentModelIndex recentActive_;
    QModelIndex displacedActive() const;
    qint64 activeLostAt_=0,focusGainedAt_=0;
    QDBusServiceWatcher watcher_;
    quint64 generation_=0;
#endif
};
