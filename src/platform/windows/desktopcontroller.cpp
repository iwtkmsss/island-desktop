#include "desktopcontroller.h"
#include "shellapps.h"
#include "tasklist.h"
#include "tasksmodel.h"
#include "taskwatcher.h"
#include "trayhost.h"
#include <QCursor>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QMap>
#include <QMenu>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QWindow>
#include <algorithm>
#include <cstdio>
#include <thread>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

namespace Shell = IslandDesktop::Shell;
using IslandDesktop::TaskList;
using IslandDesktop::TaskWindow;

namespace {
bool trayCaptureAllowedFlag = true;
constexpr int taskIconSize = 96;
constexpr int menuIconSize = 32;
const QString taskPrefix = QStringLiteral("task/");
const QString trayPrefix = QStringLiteral("tray/");
const QString aumidPrefix = QStringLiteral("aumid:");
const QString exePrefix = QStringLiteral("exe:");
const QString windowPrefix = QStringLiteral("window:");

// Development trace (ISLAND_DESKTOP_TRACE=1): once a second prints how often and
// for how long the GUI thread was busy with task refreshes and tray rebuilds.
struct DesktopTrace {
    const bool enabled = qEnvironmentVariableIsSet("ISLAND_DESKTOP_TRACE");
    int refreshes = 0, bumps = 0, trayRebuilds = 0, trayChanges = 0;
    qint64 refreshMs = 0, refreshMax = 0, trayMs = 0;
};
DesktopTrace &trace() { static DesktopTrace instance; return instance; }

quint64 fileTime(const FILETIME &time) { return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime; }

// Letter submenu of the application menu: Latin and Cyrillic letters get their
// own group (accented Latin folds to its base letter), everything else is "#".
QString menuGroup(const QString &name) {
    for (const QChar c : name) {
        if (c.isSpace()) continue;
        if (!c.isLetter()) return QStringLiteral("#");
        if (c.script() == QChar::Script_Latin) {
            const auto base = QString(c).normalized(QString::NormalizationForm_D);
            return QString(base.isEmpty() ? c : base.at(0)).toUpper();
        }
        if (c.script() == QChar::Script_Cyrillic) return QString(c).toUpper();
        return QStringLiteral("#");
    }
    return QStringLiteral("#");
}
int groupRank(const QString &group) {
    if (group == QLatin1String("#")) return 2;
    return group.at(0).script() == QChar::Script_Cyrillic ? 1 : 0;
}
}

// Windows state of DesktopController (pimpl). Everything runs on the GUI thread.
class WindowsDesktop final {
public:
    explicit WindowsDesktop(DesktopController *controller);
    ~WindowsDesktop();

    struct App {
        QString name;
        QString appId;       // AUMID in its original case, if known
        QString exe;         // launch target, if known
        bool inAppsFolder = false;
        bool resolved = false;
        bool hasIcon = false;
        quint64 iconRevision = 0;
    };
    struct WindowCache {
        quint32 processId = 0;
        QString exe;
        QString packageAppId;  // MSIX identity of the window's own process
        bool processRead = false;
        QString frameAppId;
    };

    void scheduleRefresh() { refreshTimer.start(); }
    void refresh();
    void bump() { ++trace().bumps; ++q->taskRevision_; emit q->taskRevisionChanged(); }
    void noteForeground();
    quintptr effectiveActive() const;
    // Returns true if name or icon were (re)loaded now.
    bool ensureApp(const QString &key, const QString &appId, const QString &exe, quintptr window);
    QString iconUrl(const QString &key) const;
    bool canLaunch(const QString &key) const;
    void launch(const QString &key);
    void saveLaunchers();
    void loadLaunchers();
    void sampleMetrics();
    void rebuildTray();
    void watchNativeMenu();
    void stopNativeMenuWatch();
    void prefetchStartApps();

    DesktopController *q;
    TaskList list;
    TasksModel model;
    QHash<QString, App> apps;
    QHash<quintptr, WindowCache> windowCache;
    quintptr lastForeign = 0;
    quint64 iconCounter = 0;
    bool refreshing = false, refreshPending = false;
    QTimer refreshTimer, resyncTimer, metricsTimer, nativeMenuTimer;
    std::unique_ptr<TaskWatcher> watcher;
    std::unique_ptr<TrayHost> tray;
    QHash<QString, quint64> trayRevisions;
    QElapsedTimer nativeMenuClock;
    qint64 nativeMenuSeenAt = -1;
    // Enumerating shell:AppsFolder takes ~0.4–0.8 s, so it runs on a worker
    // thread (joined, never detached) and the menu shows the latest list.
    QList<Shell::StartApp> startApps;
    bool startAppsReady = false;
    bool startAppsLoading = false;  // GUI thread only
    std::thread startAppsThread;
};

WindowsDesktop::WindowsDesktop(DesktopController *controller)
    : q(controller),
      model(list, {[this](const QString &key) { return apps.value(key).name; },
                   [this](const QString &key) { return iconUrl(key); }}) {
    list.setObserver(&model);
    loadLaunchers();

    QObject::connect(&model, &QAbstractItemModel::dataChanged, q, [this] { bump(); });
    QObject::connect(&model, &QAbstractItemModel::rowsInserted, q, [this] { bump(); });
    QObject::connect(&model, &QAbstractItemModel::rowsRemoved, q, [this] { bump(); });
    QObject::connect(&model, &QAbstractItemModel::rowsMoved, q, [this] { bump(); });
    QObject::connect(&model, &QAbstractItemModel::modelReset, q, [this] { bump(); });
    if (qGuiApp)
        QObject::connect(qGuiApp, &QGuiApplication::focusWindowChanged, q, [this](QWindow *) {
            // Re-evaluate after the foreground change has been reported.
            QTimer::singleShot(0, q, [this] { bump(); });
        });

    // Shell and WinEvent notifications are coalesced into one re-enumeration.
    refreshTimer.setSingleShot(true);
    refreshTimer.setInterval(60);
    QObject::connect(&refreshTimer, &QTimer::timeout, q, [this] { refresh(); });
    resyncTimer.setInterval(2000);
    QObject::connect(&resyncTimer, &QTimer::timeout, q, [this] { refresh(); });
    resyncTimer.start();
    watcher = std::make_unique<TaskWatcher>(
        [this] { scheduleRefresh(); },
        [this](quintptr window) {
            // Hooks can fire inside nested loops: record, then act from the event loop.
            if (window && !Shell::isOwnWindow(window)) lastForeign = window;
            QTimer::singleShot(0, q, [this] { noteForeground(); bump(); });
            scheduleRefresh();
        });
    if (!watcher->running()) qWarning("Island Desktop: shell hook window unavailable; tasks resync every 2 s.");
    QTimer::singleShot(0, q, [this] {
        for (const auto &key : list.launchers()) {
            const auto app = apps.value(key);
            ensureApp(key, app.appId, app.exe, 0);
        }
        model.refreshAll();
        refresh();
        prefetchStartApps();
    });

    if (trace().enabled) {
        auto *report = new QTimer(q);
        report->setInterval(1000);
        QObject::connect(report, &QTimer::timeout, q, [] {
            auto &t = trace();
            std::fprintf(stderr, "desktop-trace refresh=%d total=%lldms max=%lldms bumps=%d tray-rebuilds=%d tray-changes=%d tray=%lldms\n",
                t.refreshes, (long long)t.refreshMs, (long long)t.refreshMax, t.bumps, t.trayRebuilds, t.trayChanges, (long long)t.trayMs);
            t.refreshes = t.bumps = t.trayRebuilds = t.trayChanges = 0;
            t.refreshMs = t.refreshMax = t.trayMs = 0;
        });
        report->start();
    }
    metricsTimer.setInterval(2000);
    QObject::connect(&metricsTimer, &QTimer::timeout, q, [this] { sampleMetrics(); });
    metricsTimer.start();

    nativeMenuTimer.setInterval(100);
    QObject::connect(&nativeMenuTimer, &QTimer::timeout, q, [this] {
        const auto elapsed = nativeMenuClock.elapsed();
        if (TrayHost::nativeMenuOpen()) nativeMenuSeenAt = elapsed;
        // Grace for the owner to open its menu, then wait until none is visible.
        const qint64 since = nativeMenuSeenAt < 0 ? elapsed : elapsed - nativeMenuSeenAt;
        if (elapsed >= 800 && since >= 250) {
            stopNativeMenuWatch();
            if (q->menuOpen_) { q->menuOpen_ = false; emit q->menuOpenChanged(); }
        }
    });

    if (trayCaptureAllowedFlag && q->settings_.value("desktop/trayCapture", true).toBool()) {
        tray = std::make_unique<TrayHost>();
        QObject::connect(tray.get(), &TrayHost::iconsChanged, q, [this] { rebuildTray(); });
        QTimer::singleShot(0, q, [this] {
            if (!tray) return;
            if (!tray->start()) qWarning("Island Desktop: could not host the notification area.");
            rebuildTray();
        });
    }
}

WindowsDesktop::~WindowsDesktop() {
    // Unhook first so no callback reaches a half-destroyed object.
    watcher.reset();
    refreshTimer.stop();
    resyncTimer.stop();
    metricsTimer.stop();
    nativeMenuTimer.stop();
    if (tray) {
        QObject::disconnect(tray.get(), nullptr, q, nullptr);
        tray->stop();
        tray.reset();
    }
    // The worker only posts its result to q; the posted event is discarded
    // together with q, which is destroyed right after this object.
    if (startAppsThread.joinable()) startAppsThread.join();
    list.setObserver(nullptr);
}

void WindowsDesktop::prefetchStartApps() {
    if (startAppsLoading) return;
    // A previous worker has already posted its result; it is exiting or gone.
    if (startAppsThread.joinable()) startAppsThread.join();
    startAppsLoading = true;
    // q outlives the worker: ~WindowsDesktop joins it inside ~DesktopController.
    DesktopController *target = q;
    startAppsThread = std::thread([this, target] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto apps = Shell::startApps();
        if (SUCCEEDED(com)) CoUninitialize();
        // PIDLs are plain memory owned by shared_ptr; safe to hand over.
        QMetaObject::invokeMethod(target, [this, apps = std::move(apps)]() mutable {
            startApps = std::move(apps);
            startAppsReady = true;
            startAppsLoading = false;
        }, Qt::QueuedConnection);
    });
}

void WindowsDesktop::refresh() {
    // Shell calls below may pump messages (COM); never re-enter.
    if (refreshing) { refreshPending = true; return; }
    refreshing = true;
    QElapsedTimer traceClock;
    traceClock.start();
    const auto windows = Shell::taskbarWindows();
    QList<TaskWindow> snapshot;
    QHash<quintptr, WindowCache> cache;
    QStringList reloaded;
    for (const auto &info : windows) {
        auto entry = windowCache.value(info.handle);
        if (entry.processId != info.processId) entry = WindowCache{info.processId};
        if (!entry.processRead) {
            entry.exe = Shell::processImagePath(info.processId);
            entry.packageAppId = Shell::processAppId(info.processId);
            entry.processRead = true;
        }
        const bool frame = info.className == QLatin1String("ApplicationFrameWindow");
        // Identity, most specific first: the window's explicit AUMID, the
        // hosted UWP app of a frame, the package of an MSIX desktop app, the exe.
        QString appId = Shell::windowAppId(info.handle);
        bool packaged = false;
        if (appId.isEmpty() && frame) {
            if (entry.frameAppId.isEmpty()) entry.frameAppId = Shell::frameAppId(info.handle);
            appId = entry.frameAppId;
            packaged = true;
        }
        if (appId.isEmpty() && !frame && !entry.packageAppId.isEmpty()) {
            appId = entry.packageAppId;
            packaged = true;
        }
        QString key, exe;
        if (!appId.isEmpty()) {
            key = aumidPrefix + appId.toLower();
            // Packaged apps launch through their AUMID only: their versioned
            // WindowsApps path changes on update and cannot be started directly.
            if (!frame && !packaged && entry.packageAppId.isEmpty()) exe = entry.exe;
        } else if (!frame && !entry.exe.isEmpty()) {
            key = exePrefix + entry.exe.toLower();
            exe = entry.exe;
        } else {
            key = windowPrefix + QString::number(info.handle, 16);
        }
        cache.insert(info.handle, entry);
        if (ensureApp(key, appId, exe, info.handle)) reloaded.append(key);
        snapshot.append({info.handle, key, info.title, info.minimized});
    }
    windowCache = cache;
    list.update(snapshot);
    for (const auto &key : reloaded) model.refreshRow(list.rowOf(key));
    // Forget metadata of applications that have neither a row nor a window.
    for (auto it = apps.begin(); it != apps.end();) {
        if (list.rowOf(it.key()) < 0) {
            q->icons_.remove(taskPrefix + it.key());
            it = apps.erase(it);
        } else {
            ++it;
        }
    }
    noteForeground();
    if (trace().enabled) {
        const auto spent = traceClock.elapsed();
        ++trace().refreshes; trace().refreshMs += spent; trace().refreshMax = std::max(trace().refreshMax, spent);
    }
    refreshing = false;
    if (refreshPending) { refreshPending = false; scheduleRefresh(); }
}

void WindowsDesktop::noteForeground() {
    const auto foreground = Shell::foregroundWindow();
    if (foreground && !Shell::isOwnWindow(foreground)) lastForeign = foreground;
    list.noteActivated(foreground);
}

// Clicking the island moves the foreground to it before the click arrives, so
// the window that was in front right before one of our windows took over is
// treated as active (if it still exists and is not minimized). Our own windows
// that are tasks themselves (the settings window) count normally.
quintptr WindowsDesktop::effectiveActive() const {
    const auto foreground = Shell::foregroundWindow();
    if (foreground && Shell::isOwnWindow(foreground) && !list.contains(foreground))
        return Shell::isRestoredWindow(lastForeign) ? lastForeign : 0;
    return foreground;
}

bool WindowsDesktop::ensureApp(const QString &key, const QString &appId, const QString &exe, quintptr window) {
    auto &app = apps[key];
    if (app.appId.isEmpty() && !appId.isEmpty()) app.appId = appId;
    if (app.exe.isEmpty() && !exe.isEmpty()) app.exe = exe;
    bool loaded = false;
    if (!app.resolved) {
        app.resolved = true;
        loaded = true;
        QImage image;
        if (key.startsWith(aumidPrefix)) {
            const auto id = app.appId.isEmpty() ? key.mid(aumidPrefix.size()) : app.appId;
            app.name = Shell::appsFolderName(id);
            app.inAppsFolder = !app.name.isEmpty();
            if (app.inAppsFolder) image = Shell::appsFolderImage(id, taskIconSize);
        } else if (key.startsWith(exePrefix)) {
            if (app.exe.isEmpty()) app.exe = key.mid(exePrefix.size());
        }
        if (app.name.isEmpty() && !app.exe.isEmpty()) {
            app.name = Shell::fileDescription(app.exe);
            if (app.name.isEmpty()) app.name = QFileInfo(app.exe).completeBaseName();
        }
        if (image.isNull() && !app.exe.isEmpty()) image = Shell::fileImage(app.exe, taskIconSize);
        if (image.isNull() && window) image = Shell::windowIcon(window);
        if (!image.isNull()) {
            q->icons_[taskPrefix + key] = QIcon(QPixmap::fromImage(image));
            app.hasIcon = true;
        }
        app.iconRevision = ++iconCounter;
    } else if (!app.hasIcon && window) {
        // A launcher without a readable icon got a window: use the window's icon.
        const auto image = Shell::windowIcon(window);
        if (!image.isNull()) {
            q->icons_[taskPrefix + key] = QIcon(QPixmap::fromImage(image));
            app.hasIcon = true;
            app.iconRevision = ++iconCounter;
            loaded = true;
        }
    }
    return loaded;
}

QString WindowsDesktop::iconUrl(const QString &key) const {
    return QStringLiteral("image://desktop/") + QString::fromLatin1(QUrl::toPercentEncoding(taskPrefix + key))
        + QLatin1Char('?') + QString::number(apps.value(key).iconRevision) + QStringLiteral("&raw");
}

bool WindowsDesktop::canLaunch(const QString &key) const {
    if (key.startsWith(windowPrefix)) return false;
    const auto app = apps.value(key);
    if (key.startsWith(aumidPrefix) && app.inAppsFolder) return true;
    return !app.exe.isEmpty();
}

void WindowsDesktop::launch(const QString &key) {
    const auto app = apps.value(key);
    if (key.startsWith(aumidPrefix)) {
        const auto id = app.appId.isEmpty() ? key.mid(aumidPrefix.size()) : app.appId;
        if (Shell::launchAppsFolder(id)) return;
    }
    if (!key.startsWith(windowPrefix) && Shell::launchFile(app.exe)) return;
    q->fail(QStringLiteral("Не вдалося запустити застосунок."));
}

// Entries are "<appKey>|<exe path or empty>" so a pinned application can be
// named, iconed and launched while it has no window.
void WindowsDesktop::saveLaunchers() {
    QStringList entries;
    for (const auto &key : list.launchers()) entries.append(key + QLatin1Char('|') + apps.value(key).exe);
    q->settings_.setValue("desktop/launchers", entries);
    q->settings_.sync();
}

void WindowsDesktop::loadLaunchers() {
    QStringList keys;
    for (const auto &entry : q->settings_.value("desktop/launchers").toStringList()) {
        const int separator = entry.indexOf(QLatin1Char('|'));
        const auto key = (separator < 0 ? entry : entry.left(separator)).trimmed().toLower();
        const auto exe = separator < 0 ? QString() : entry.mid(separator + 1).trimmed();
        if (!key.startsWith(aumidPrefix) && !key.startsWith(exePrefix)) continue;
        if (key.size() <= key.indexOf(QLatin1Char(':')) + 1) continue;
        auto &app = apps[key];
        if (!exe.isEmpty()) app.exe = exe;
        keys.append(key);
    }
    list.setLaunchers(keys);
}

// Adapted from the GetSystemTimes / GlobalMemoryStatusEx sampling in the
// supplied mod 1.4.0, lines 8293–8299 and 8326–8346.
// Copyright (c) 2026 devcode90, MIT; third_party/dynamic-island/LICENSE.
// Kernel time includes idle time. Format and timer semantics follow Linux.
void WindowsDesktop::sampleMetrics() {
    if (!q->metricsActive_) { q->cpuTotal_ = q->cpuIdle_ = 0; return; }
    QString cpuText = QStringLiteral("—"), ramText = QStringLiteral("—");
    FILETIME idleTime{}, kernelTime{}, userTime{};
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        const quint64 total = fileTime(kernelTime) + fileTime(userTime), idle = fileTime(idleTime);
        if (q->cpuTotal_ && total > q->cpuTotal_ && idle >= q->cpuIdle_)
            cpuText = QString::number(qBound(0, qRound(100.0 * (1.0 - double(idle - q->cpuIdle_) / double(total - q->cpuTotal_))), 100)) + "%";
        q->cpuTotal_ = total;
        q->cpuIdle_ = idle;
    }
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys && memory.ullAvailPhys <= memory.ullTotalPhys) {
        constexpr double gib = 1024.0 * 1024.0 * 1024.0;
        const QLocale locale(QLocale::Ukrainian);
        ramText = locale.toString(double(memory.ullTotalPhys - memory.ullAvailPhys) / gib, 'f', 1) + " / "
            + locale.toString(double(memory.ullTotalPhys) / gib, 'f', 1) + QStringLiteral(" ГіБ");
    }
    q->metrics_ = QStringLiteral("CPU  %1   ·   RAM  %2").arg(cpuText, ramText);
    emit q->metricsChanged();
}

// QML builds "image://desktop/" + encodeURIComponent(id) and calls
// trayAction(id, ...), so the exposed id is the icons_ key ("tray/<host id>").
void WindowsDesktop::rebuildTray() {
    QElapsedTimer traceClock;
    traceClock.start();
    ++trace().trayRebuilds;
    const auto icons = tray && tray->running() ? tray->icons() : QList<TrayIconInfo>();
    QVariantList items;
    QSet<QString> present;
    for (const auto &info : icons) {
        if (info.id.isEmpty()) continue;
        const auto id = trayPrefix + info.id;
        if (present.contains(id)) continue;
        present.insert(id);
        if (!trayRevisions.contains(id) || trayRevisions.value(id) != info.imageRevision || !q->icons_.contains(id)) {
            q->icons_[id] = info.image.isNull() ? QIcon() : QIcon(QPixmap::fromImage(info.image));
            trayRevisions[id] = info.imageRevision;
        }
        items.append(QVariantMap{{"id", id}, {"title", info.title}, {"status", QStringLiteral("Active")},
                                 {"revision", QVariant::fromValue(info.imageRevision)}});
    }
    for (auto it = trayRevisions.begin(); it != trayRevisions.end();) {
        if (present.contains(it.key())) { ++it; continue; }
        q->icons_.remove(it.key());
        it = trayRevisions.erase(it);
    }
    if (items == q->items_) return;
    q->items_ = items;
    emit q->trayChanged();
    ++trace().trayChanges;
    trace().trayMs += traceClock.elapsed();
}

// Native menus belong to the icon's owner; their lifetime is only observable.
void WindowsDesktop::watchNativeMenu() {
    nativeMenuClock.start();
    nativeMenuSeenAt = -1;
    nativeMenuTimer.start();
    if (!q->menuOpen_) { q->menuOpen_ = true; emit q->menuOpenChanged(); }
}

void WindowsDesktop::stopNativeMenuWatch() {
    nativeMenuTimer.stop();
    nativeMenuSeenAt = -1;
}

void DesktopController::setTrayCaptureAllowed(bool allowed) { trayCaptureAllowedFlag = allowed; }

DesktopController::DesktopController(const QString &file, QObject *parent)
    : QObject(parent), settings_(file, QSettings::IniFormat) {
    win_ = std::make_unique<WindowsDesktop>(this);
}

DesktopController::~DesktopController() = default;

bool DesktopController::available() const { return true; }
QAbstractItemModel *DesktopController::tasks() { return &win_->model; }
void DesktopController::fail(const QString &message) { error_ = message; emit errorChanged(); }

void DesktopController::showMenu(QMenu *menu) {
    win_->stopNativeMenuWatch();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menuOpen_ = true;
    emit menuOpenChanged();
    connect(menu, &QMenu::aboutToHide, this, [this] { menuOpen_ = false; emit menuOpenChanged(); });
    menu->popup(QCursor::pos());
}

QVariantMap DesktopController::taskState(int row) const {
    const auto state = win_->list.state(row, win_->effectiveActive());
    return {{"count", state.count}, {"active", state.active}};
}

void DesktopController::activateTask(int row) {
    using Kind = TaskList::ActionKind;
    const auto action = win_->list.click(row, win_->effectiveActive());
    switch (action.kind) {
    case Kind::None:
        return;
    case Kind::Launch:
        win_->launch(action.appKey);
        break;
    case Kind::Activate:
    case Kind::Restore:
        Shell::activateWindow(action.window);
        break;
    case Kind::Minimize:
        Shell::minimizeWindow(action.window);
        break;
    }
    win_->scheduleRefresh();
}

void DesktopController::taskMenu(int row) {
    const auto *task = win_->list.row(row);
    if (!task) return;
    // Actions capture the application key, never the row: rows may shift
    // while the menu is open.
    const QString key = task->appKey;
    const bool pinned = task->pinned;
    auto *menu = new QMenu;
    auto *pin = menu->addAction(pinned ? QStringLiteral("Відкріпити") : QStringLiteral("Закріпити"), this, [this, key, pinned] {
        const int current = win_->list.rowOf(key);
        if (current < 0) return;
        if (pinned ? win_->list.unpin(current) : win_->list.pin(current)) win_->saveLaunchers();
    });
    pin->setEnabled(pinned || win_->canLaunch(key));
    menu->addAction(QStringLiteral("Нове вікно"), this, [this, key] { win_->launch(key); })
        ->setEnabled(win_->canLaunch(key));
    menu->addAction(QStringLiteral("Ліворуч"), this, [this, key] {
        const int current = win_->list.rowOf(key);
        if (current > 0) moveTask(current, current - 1);
    })->setEnabled(row > 0);
    menu->addAction(QStringLiteral("Праворуч"), this, [this, key] {
        const int current = win_->list.rowOf(key);
        if (current >= 0) moveTask(current, current + 1);
    })->setEnabled(row + 1 < win_->list.rowCount());
    showMenu(menu);
}

void DesktopController::moveTask(int from, int to) {
    if (win_->list.move(from, to)) win_->saveLaunchers();
}

void DesktopController::applicationMenu() {
    // Show the latest prefetched list at once and refresh it for the next time
    // (newly installed applications appear from the next opening).
    auto apps = win_->startAppsReady ? win_->startApps : Shell::startApps();
    win_->prefetchStartApps();
    std::sort(apps.begin(), apps.end(), [](const auto &a, const auto &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    auto *menu = new QMenu;
    using Entries = QList<QPair<QPointer<QAction>, std::shared_ptr<void>>>;
    QMap<QString, QMenu *> groups;
    QHash<QMenu *, Entries> entries;
    for (const auto &app : apps) {
        const auto group = menuGroup(app.name);
        QMenu *submenu = groups.value(group);
        if (!submenu) {
            submenu = new QMenu(group, menu);
            groups.insert(group, submenu);
        }
        // The functor owns a reference to the PIDL for the action's lifetime.
        auto *action = submenu->addAction(app.name, this, [this, item = app.item] {
            if (!Shell::launchItem(item)) fail(QStringLiteral("Не вдалося запустити застосунок."));
        });
        entries[submenu].append({action, app.item});
    }
    QList<QString> order = groups.keys();
    std::sort(order.begin(), order.end(), [](const QString &a, const QString &b) {
        const int ra = groupRank(a), rb = groupRank(b);
        return ra != rb ? ra < rb : QString::localeAwareCompare(a, b) < 0;
    });
    for (const auto &group : order) {
        auto *submenu = groups.value(group);
        menu->addMenu(submenu);
        // Icons are loaded per submenu when it first opens, not for every app up
        // front. The connection (and the PIDLs it holds) dies with the submenu.
        auto pending = std::make_shared<Entries>(entries.value(submenu));
        connect(submenu, &QMenu::aboutToShow, submenu, [pending] {
            for (const auto &entry : std::as_const(*pending)) {
                if (!entry.first) continue;
                const auto image = Shell::itemImage(entry.second, menuIconSize);
                if (!image.isNull()) entry.first->setIcon(QIcon(QPixmap::fromImage(image)));
            }
            pending->clear();
        });
    }
    if (menu->isEmpty()) {
        delete menu;
        fail(QStringLiteral("Не вдалося прочитати список застосунків."));
        return;
    }
    showMenu(menu);
}

void DesktopController::openSettings(const QString &module) {
    static const QHash<QString, QString> pages{
        {"kcm_networkmanagement", "ms-settings:network"},
        {"kcm_bluetooth", "ms-settings:bluetooth"},
        {"kcm_pulseaudio", "ms-settings:sound"},
        {"kcm_powerdevilprofilesconfig", "ms-settings:powersleep"}};
    const auto page = pages.value(module);
    if (page.isEmpty()) return;
    if (!Shell::openUri(page)) fail(QStringLiteral("Не вдалося відкрити параметри Windows."));
}

void DesktopController::refreshTray() { win_->rebuildTray(); }

void DesktopController::trayAction(const QString &id, const QString &action, int) {
    if (!win_->tray || !win_->tray->running() || !id.startsWith(trayPrefix)) return;
    bool known = false;
    for (const auto &entry : items_)
        if (entry.toMap().value("id").toString() == id) { known = true; break; }
    if (!known) return;
    auto kind = TrayHost::Action::Activate;
    if (action == QLatin1String("Activate")) kind = TrayHost::Action::Activate;
    else if (action == QLatin1String("SecondaryActivate")) kind = TrayHost::Action::SecondaryActivate;
    else if (action == QLatin1String("ContextMenu")) kind = TrayHost::Action::ContextMenu;
    else return;  // "Scroll": the notification area has no wheel message
    int x = 0, y = 0;
    Shell::cursorPosition(&x, &y);
    if (!win_->tray->trigger(id.mid(trayPrefix.size()), kind, QPoint(x, y))) {
        win_->rebuildTray();
        return;
    }
    win_->watchNativeMenu();
}

void DesktopController::trayMenu(const QString &, const QString &) {}
