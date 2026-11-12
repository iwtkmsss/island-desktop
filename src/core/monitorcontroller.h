#pragma once

#include "monitorselection.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <functional>

class QScreen;
class QWindow;

class MonitorController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList options READ options NOTIFY optionsChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectedIdChanged)
public:
    using TopologyProvider = std::function<QList<MonitorInfo>()>;
    explicit MonitorController(const QString &settingsFile, QObject *parent = nullptr);
    // An injected catalog exercises the same persistence/debounce path without
    // requiring a desktop or changing the machine's display configuration.
    MonitorController(const QString &settingsFile, TopologyProvider provider, QObject *parent = nullptr);
    ~MonitorController() override;

    QVariantList options() const { return options_; }
    QString selectedId() const { return selectedId_; }
    QString activeId() const { return activeId_; }
    void attach(QWindow *window);
    Q_INVOKABLE void selectMonitor(const QString &id);
    // Coalesce screen topology/DPI notifications and QML frame requests. This
    // does not observe shellHeight, expansion, or tab animations.
    void position();

signals:
    void optionsChanged();
    void selectedIdChanged();
    void activeMonitorChanged();

private:
    void watchScreen(QScreen *screen);
    QList<MonitorInfo> readScreens();
    void refresh();
    void updateOptions();
    void applyWindowGeometry();

    QSettings settings_;
    TopologyProvider provider_;
    QTimer refreshTimer_;
    QList<MonitorInfo> monitors_;
    QHash<QString, QPointer<QScreen>> screens_;
    QPointer<QWindow> window_;
    QMetaObject::Connection windowScreenConnection_;
    QVariantList options_;
    QString selectedId_;
    QString savedLabel_;
    QString activeId_;
    bool applyingGeometry_ = false;
};
