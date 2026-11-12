#include "monitorcontroller.h"
#include <QGuiApplication>
#include <QScopedValueRollback>
#include <QScreen>
#include <QWindow>
#include <algorithm>
#include <utility>

MonitorController::MonitorController(const QString &settingsFile, QObject *parent)
    : MonitorController(settingsFile, TopologyProvider{}, parent) {}

MonitorController::MonitorController(const QString &settingsFile, TopologyProvider provider, QObject *parent)
    : QObject(parent), settings_(settingsFile, QSettings::IniFormat), provider_(std::move(provider)),
      selectedId_(settings_.value("display/monitorId").toString()),
      savedLabel_(settings_.value("display/monitorLabel").toString()) {
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(50);
    connect(&refreshTimer_, &QTimer::timeout, this, &MonitorController::refresh);
    if (!provider_) {
        provider_ = [this] { return readScreens(); };
        if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            connect(app, &QGuiApplication::screenAdded, this, [this](QScreen *screen) {
                watchScreen(screen);
                position();
            });
            connect(app, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) {
                // Do not retain a removed QScreen in a queued callback. Qt may
                // destroy it immediately after this notification returns.
                disconnect(screen, nullptr, this, nullptr);
                position();
            });
            connect(app, &QGuiApplication::primaryScreenChanged, this, &MonitorController::position);
            for (auto *screen : QGuiApplication::screens()) watchScreen(screen);
        }
    }
    refresh();
}

MonitorController::~MonitorController() = default;

void MonitorController::watchScreen(QScreen *screen) {
    connect(screen, &QScreen::geometryChanged, this, &MonitorController::position);
    connect(screen, &QScreen::availableGeometryChanged, this, &MonitorController::position);
    connect(screen, &QScreen::virtualGeometryChanged, this, &MonitorController::position);
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, &MonitorController::position);
    // QScreen's devicePixelRatio property uses this notification too.
    connect(screen, &QScreen::physicalDotsPerInchChanged, this, &MonitorController::position);
}

QList<MonitorInfo> MonitorController::readScreens() {
    screens_.clear();
    QList<MonitorInfo> result;
    const auto *primary = QGuiApplication::primaryScreen();
    for (auto *screen : QGuiApplication::screens()) {
        const auto id = monitorId(screen->manufacturer(), screen->model(), screen->serialNumber(), screen->name());
        auto screenName = screen->name();
        if (screenName.startsWith(QStringLiteral("\\\\.\\DISPLAY")))
            screenName = QStringLiteral("Монітор %1").arg(screenName.mid(11));
        QString label = screen->model().trimmed();
        if (label.isEmpty()) label = screenName;
        else if (!screenName.isEmpty() && screenName != label) label += QStringLiteral(" · ") + screenName;
        if (label.isEmpty()) label = QStringLiteral("Монітор");
        const auto logicalSize = screen->geometry().size();
        const QSize size(qRound(logicalSize.width() * screen->devicePixelRatio()),
                         qRound(logicalSize.height() * screen->devicePixelRatio()));
        label += QStringLiteral(" (%1 × %2)").arg(size.width()).arg(size.height());
        result.append({id, label, screen == primary});
        screens_.insert(id, screen);
    }
    return result;
}

void MonitorController::attach(QWindow *window) {
    disconnect(windowScreenConnection_);
    window_ = window;
    if (window_) {
        windowScreenConnection_ = connect(window_, &QWindow::screenChanged, this, &MonitorController::position);
        // Initial placement must be applied before the first event-loop frame.
        refreshTimer_.stop();
        refresh();
    }
}

void MonitorController::selectMonitor(const QString &id) {
    if (id == selectedId_) return;
    if (!id.isEmpty()) {
        const auto found = std::find_if(monitors_.cbegin(), monitors_.cend(),
            [&id](const MonitorInfo &monitor) { return monitor.id == id; });
        if (found == monitors_.cend()) return; // Reject stale/invalid popup rows.
        savedLabel_ = found->label;
    } else {
        savedLabel_.clear();
    }
    selectedId_ = id;
    settings_.setValue("display/monitorId", selectedId_);
    settings_.setValue("display/monitorLabel", savedLabel_);
    settings_.sync();
    updateOptions();
    emit selectedIdChanged();
    position();
}

void MonitorController::position() {
    if (!applyingGeometry_) refreshTimer_.start();
}

void MonitorController::updateOptions() {
    const auto next = monitorOptions(monitors_, selectedId_, savedLabel_);
    if (options_ == next) return;
    options_ = next;
    emit optionsChanged();
}

void MonitorController::refresh() {
    monitors_ = provider_();
    std::sort(monitors_.begin(), monitors_.end(), [](const MonitorInfo &left, const MonitorInfo &right) {
        if (left.primary != right.primary) return left.primary;
        return left.id < right.id;
    });
    updateOptions();
    const auto next = resolvedMonitorId(monitors_, selectedId_);
    if (activeId_ != next) {
        activeId_ = next;
        emit activeMonitorChanged();
    }
    // A fallback never overwrites selectedId_: reconnect restores that choice.
    applyWindowGeometry();
}

void MonitorController::applyWindowGeometry() {
    auto *target = screens_.value(activeId_).data();
    if (!window_ || !target || !QGuiApplication::screens().contains(target)) return;
    QScopedValueRollback<bool> applying(applyingGeometry_, true);
    if (window_->screen() != target) window_->setScreen(target);
    // setScreen updates QML's attached Screen synchronously; use the frame
    // properties after that update, so a narrower screen cannot oscillate the
    // old/new widths. They reserve the largest page throughout animations.
    const int width = qRound(window_->property("frameWidth").toReal()) + 40;
    const int height = qRound(window_->property("frameHeight").toReal()) + 48;
    if (width <= 40 || height <= 48) return;
    const auto available = target->availableGeometry();
    if (available.isEmpty()) return;
    const auto next = monitorFrameRect(available, QSize(width, height));
    if (window_->geometry() != next) window_->setGeometry(next);
}
