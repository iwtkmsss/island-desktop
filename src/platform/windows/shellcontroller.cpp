#include "shellcontroller.h"
#include "appstate.h"
#include "islandshape.h"
#include "taskbarworkarea.h"
#include <QCursor>
#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainterPath>
#include <QQuickWindow>
#include <QScreen>
#include <cstdio>
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cwchar>
#endif

namespace {
#ifdef Q_OS_WIN
constexpr wchar_t suppressionOwnerProperty[] = L"IslandDesktop.InputSuppressionOwner";

// Opt-in diagnostics cover the input path *before* expandedChanged as well as
// synchronous native work inside a poll. Frame-only traces miss both delays.
struct InputTrace {
    const bool enabled = qEnvironmentVariableIsSet("ISLAND_MOTION_TRACE")
        || qEnvironmentVariableIsSet("ISLAND_DESKTOP_TRACE");
    bool inside = false;
    qint64 previousPoll = 0;
    static qint64 now() { QElapsedTimer clock; clock.start(); return clock.msecsSinceReference(); }
};
InputTrace &inputTrace() { static InputTrace instance; return instance; }

struct PollTrace {
    QElapsedTimer clock;
    qint64 gap = 0, fullscreen = 0, clickThrough = 0, hover = 0, interaction = 0;
    bool nearby = false;
    PollTrace() {
        auto &trace = inputTrace();
        if (!trace.enabled) return;
        const auto now = InputTrace::now();
        gap = trace.previousPoll ? now - trace.previousPoll : 0;
        trace.previousPoll = now;
        clock.start();
    }
    ~PollTrace() {
        if (!clock.isValid()) return;
        const auto total = clock.elapsed();
        if (total >= 8 || (nearby && gap >= 100))
            std::fprintf(stderr, "input-trace poll at=%lldms gap=%lldms total=%lldms fullscreen=%lldms click-through=%lldms hover=%lldms interaction=%lldms\n",
                (long long)InputTrace::now(), (long long)gap, (long long)total,
                (long long)fullscreen, (long long)clickThrough, (long long)hover, (long long)interaction);
    }
};

// Adapted from IsForegroundFullscreen in the supplied mod 1.4.0, lines 2108–2147.
// Copyright (c) 2026 devcode90, MIT; third_party/dynamic-island/LICENSE.
// Restrict to the island's own monitor and ignore minimized/system/own windows.
bool foregroundFillsIslandMonitor(QQuickWindow *island) {
    if (!island) return false;
    const auto target = reinterpret_cast<HWND>(island->winId());
    const HWND foreground = GetForegroundWindow();
    if (!foreground || foreground == target || foreground == GetDesktopWindow() || foreground == GetShellWindow()) return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    if (!processId || processId == GetCurrentProcessId()) return false;
    if (!IsWindowVisible(foreground) || IsIconic(foreground)) return false;

    wchar_t className[256]{};
    GetClassNameW(foreground, className, 256);
    if (std::wcscmp(className, L"WorkerW") == 0 || std::wcscmp(className, L"Progman") == 0
        || std::wcscmp(className, L"Shell_TrayWnd") == 0 || std::wcscmp(className, L"Shell_SecondaryTrayWnd") == 0) return false;

    const HMONITOR islandMonitor = MonitorFromWindow(target, MONITOR_DEFAULTTONULL);
    const HMONITOR foregroundMonitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL);
    if (!islandMonitor || foregroundMonitor != islandMonitor) return false;
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    RECT client{};
    if (!GetMonitorInfoW(islandMonitor, &info) || !GetClientRect(foreground, &client)) return false;
    POINT topLeft{client.left, client.top}, bottomRight{client.right, client.bottom};
    if (!ClientToScreen(foreground, &topLeft) || !ClientToScreen(foreground, &bottomRight)) return false;
    ShellFullscreenCandidate candidate;
    candidate.visible = true;
    candidate.sameMonitor = true;
    candidate.client = {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    candidate.monitor = {info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom};
    const auto style = GetWindowLongPtrW(foreground, GWL_STYLE);
    candidate.maximized = IsZoomed(foreground) != FALSE;
    candidate.knownWindowedMaximized = TaskbarNative::isKnownWindowedMaximized(foreground);
    candidate.decoratedResizable = (style & (WS_CAPTION | WS_THICKFRAME)) == (WS_CAPTION | WS_THICKFRAME);
    RECT outer{};
    if (GetWindowRect(foreground, &outer)) candidate.outer = {outer.left, outer.top, outer.right, outer.bottom};
    if (inputTrace().enabled) {
        static HWND previous = nullptr;
        static bool previousQualifies = false;
        if (previous != foreground || previousQualifies != candidate.qualifies()) {
            previous = foreground;
            previousQualifies = candidate.qualifies();
            std::fprintf(stderr, "input-trace fullscreen at=%lldms hwnd=%llx class=%ls zoomed=%d style=%llx normal-maximized=%d qualifies=%d client=%ld,%ld,%ld,%ld outer=%ld,%ld,%ld,%ld monitor=%ld,%ld,%ld,%ld work=%ld,%ld,%ld,%ld\n",
                (long long)InputTrace::now(), (unsigned long long)reinterpret_cast<quintptr>(foreground), className,
                candidate.maximized, (unsigned long long)style, candidate.normalMaximized(), candidate.qualifies(),
                topLeft.x, topLeft.y, bottomRight.x, bottomRight.y,
                outer.left, outer.top, outer.right, outer.bottom,
                info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom,
                info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom);
        }
    }
    return candidate.qualifies();
}
#endif
}

bool ShellController::eventFilter(QObject *watched, QEvent *event) {
#ifdef Q_OS_WIN
    if (watched == window_ && inputTrace().enabled
        && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::MouseButtonDblClick))
        std::fprintf(stderr, "input-trace mouse at=%lldms type=%d expanded=%d\n",
            (long long)InputTrace::now(), int(event->type()), state_->expanded());
#endif
    return watched == window_ && handleDoubleClick(event);
}

ShellController::ShellController(AppState *state, QObject *parent) : QObject(parent), state_(state) {
    clock_.start();
    connect(&ticker_, &QTimer::timeout, this, &ShellController::poll);
    connect(state_, &AppState::passThroughActiveChanged, this, &ShellController::updatePassThrough);
    const auto resetInactiveGaze = [this] {
        if (!gazeTrackingEnabled()) resetGaze();
    };
    connect(state_, &AppState::sectionChanged, this, resetInactiveGaze);
    connect(state_, &AppState::eyesEnabledChanged, this, resetInactiveGaze);
    connect(state_, &AppState::reducedMotionChanged, this, resetInactiveGaze);
    connect(state_, &AppState::eventChanged, this, resetInactiveGaze);
    connect(state_, &AppState::expandedChanged, this, [this] {
#ifdef Q_OS_WIN
        if (inputTrace().enabled)
            std::fprintf(stderr, "input-trace expanded at=%lldms value=%d source=%s\n",
                (long long)InputTrace::now(), state_->expanded(), hoverOpening_ ? "hover" : "explicit");
#endif
        if (!state_->expanded()) {
            interaction_.suppressUntilLeave();
            setPinned(false);
            ignorePressUntilRelease_ = false;
        } else {
            // Hover intent already proved intent. The longer grace is only for
            // explicit opens from the tray/keyboard, while the cursor travels.
            holdUntil_ = hoverOpening_ ? 0 : clock_.elapsed() + 1800;
            ignorePressUntilRelease_ = !hoverOpening_;
        }
    });
}
ShellController::~ShellController() { setNativeInputSuppressed(false); }
void ShellController::attach(QQuickWindow *window) {
    window_ = window;
    window->installEventFilter(this);
    resetGaze();
    connect(window, &QWindow::visibleChanged, this, [this] {
        if (window_ && window_->isVisible()) setNativeInputSuppressed(suppressed_ || state_->passThroughActive());
        if (!gazeTrackingEnabled()) resetGaze();
    });
    connect(window, &QWindow::xChanged, this, [this] { geometryChanged_ = true; });
    connect(window, &QWindow::yChanged, this, [this] { geometryChanged_ = true; });
    connect(window, &QWindow::widthChanged, this, [this] { geometryChanged_ = true; });
    connect(window, &QWindow::heightChanged, this, [this] { geometryChanged_ = true; });
    connect(window, &QObject::destroyed, this, [this] {
        window_ = nullptr;
        resetGaze();
        ticker_.stop();
        setNativeInputSuppressed(false);
    });
    holdUntil_ = clock_.elapsed() + 1800;
    ignorePressUntilRelease_ = state_->expanded();
    ticker_.start(50);
    if (state_->passThroughActive()) updatePassThrough();
}
void ShellController::setPinned(bool pinned) {
    if (pinned && state_->passThroughActive()) return;
    if (pinned_ == pinned) return;
    pinned_ = pinned;
    if (pinned) state_->setExpanded(true);
    emit pinnedChanged();
}
void ShellController::open() {
    if (state_->passThroughActive()) return;
    interaction_.suspend();
    holdUntil_ = clock_.elapsed() + 1800;
    ignorePressUntilRelease_ = true;
    state_->setExpanded(true);
}
void ShellController::close() { state_->setExpanded(false); }

void ShellController::setSuppressed(bool suppressed) {
    if (suppressed_ == suppressed) {
        // A native window may be recreated while the application stays running.
        if (suppressed) setNativeInputSuppressed(true);
        return;
    }
    suppressed_ = suppressed;
    if (inputTrace().enabled)
        std::fprintf(stderr, "input-trace suppression at=%lldms value=%d visible=%d expanded=%d pass-through=%d\n",
            (long long)InputTrace::now(), suppressed_, window_ && window_->isVisible(), state_->expanded(), state_->passThroughActive());
    if (suppressed) resetGaze();
    interaction_.suspend();
    wasPressed_ = false;
    ignorePressUntilRelease_ = true;
    if (!suppressed && state_->expanded()) holdUntil_ = clock_.elapsed() + 1800;
    proximity_.reset(0);
    geometryChanged_ = true;
    setNativeInputSuppressed(suppressed || state_->passThroughActive());
    emit pointerChanged();
    emit suppressionChanged();
}

void ShellController::setNativeInputSuppressed(bool suppressed) {
#ifdef Q_OS_WIN
    const auto currentId = window_ ? static_cast<quintptr>(window_->winId()) : 0;
    const auto owner = reinterpret_cast<HANDLE>(this);
    if (suppressedWindowId_ && (!suppressed || currentId != suppressedWindowId_
        || GetPropW(reinterpret_cast<HWND>(suppressedWindowId_), suppressionOwnerProperty) != owner)) {
        const auto previous = reinterpret_cast<HWND>(suppressedWindowId_);
        auto region = reinterpret_cast<HRGN>(savedWindowRegion_);
        // Native handles may be reused after Qt recreates a window. A property
        // belongs to that native window's lifetime, so never restore onto a
        // different window that happens to receive the same numeric handle.
        if (GetPropW(previous, suppressionOwnerProperty) == owner) {
            // Restore only our style bit, preserving other Qt/native changes.
            const LONG_PTR style = GetWindowLongPtrW(previous, GWL_EXSTYLE);
            const LONG_PTR restored = wasNativeInputTransparent_ ? style | WS_EX_TRANSPARENT : style & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
            if (restored != style) SetWindowLongPtrW(previous, GWL_EXSTYLE, restored);
            if (nativeRegionSuppressed_) {
                if (SetWindowRgn(previous, region, TRUE)) region = nullptr; // Windows owns it after success.
                else if (!SetWindowRgn(previous, nullptr, TRUE)) qWarning("Could not restore the island's native input region.");
            }
            RemovePropW(previous, suppressionOwnerProperty);
            SetWindowPos(previous, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        if (region) DeleteObject(region);
        savedWindowRegion_ = suppressedWindowId_ = 0;
        nativeRegionSuppressed_ = false;
    }
    if (!suppressed || !currentId) return;
    const auto target = reinterpret_cast<HWND>(currentId);
    const LONG_PTR style = GetWindowLongPtrW(target, GWL_EXSTYLE);
    if (!suppressedWindowId_) {
        if (!SetPropW(target, suppressionOwnerProperty, owner)) {
            qWarning("Could not mark the island's native input region for restoration.");
            return;
        }
        suppressedWindowId_ = currentId;
        wasNativeInputTransparent_ = (style & WS_EX_TRANSPARENT) != 0;
    }
    // A window region clips drawing as well as input on Windows. Only fullscreen
    // suppression may empty it; double-click pass-through must keep the surface
    // drawable for the collapse and opacity animations, using transparency alone.
    const bool hideSurface = suppressed_;
    if (hideSurface && !nativeRegionSuppressed_) {
        auto savedRegion = CreateRectRgn(0, 0, 0, 0);
        if (!savedRegion) return;
        if (GetWindowRgn(target, savedRegion) == ERROR) { DeleteObject(savedRegion); savedRegion = nullptr; }
        auto empty = CreateRectRgn(0, 0, 0, 0);
        if (!empty || !SetWindowRgn(target, empty, TRUE)) {
            if (empty) DeleteObject(empty);
            if (savedRegion) DeleteObject(savedRegion);
            qWarning("Could not suppress the island's native input region.");
        } else {
            savedWindowRegion_ = reinterpret_cast<quintptr>(savedRegion);
            nativeRegionSuppressed_ = true;
        }
    } else if (!hideSurface && nativeRegionSuppressed_) {
        auto region = reinterpret_cast<HRGN>(savedWindowRegion_);
        if (SetWindowRgn(target, region, TRUE)) region = nullptr;
        else if (!SetWindowRgn(target, nullptr, TRUE)) qWarning("Could not restore the island's native input region.");
        if (region) DeleteObject(region);
        savedWindowRegion_ = 0;
        nativeRegionSuppressed_ = false;
    }
    if (!(style & WS_EX_TRANSPARENT)) {
        SetWindowLongPtrW(target, GWL_EXSTYLE, style | WS_EX_TRANSPARENT);
        SetWindowPos(target, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#else
    Q_UNUSED(suppressed)
#endif
}

// Adapted from SetClickThrough in the supplied mod 1.4.0, lines 10226–10241.
// Copyright (c) 2026 devcode90, MIT; third_party/dynamic-island/LICENSE.
void ShellController::setClickThrough(bool clickThrough) {
#ifdef Q_OS_WIN
    if (!window_ || suppressed_ || state_->passThroughActive() || suppressedWindowId_) return; // fullscreen suppression owns the bit
    const auto hwnd = reinterpret_cast<HWND>(window_->winId());
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (((style & WS_EX_TRANSPARENT) != 0) == clickThrough) return;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, clickThrough ? style | WS_EX_TRANSPARENT : style & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#else
    Q_UNUSED(clickThrough)
#endif
}

void ShellController::refreshHoverAfterGeometryChange(bool pressed) {
    if (!geometryChanged_ || pressed || !window_ || suppressed_ || !window_->isVisible()) return;
    geometryChanged_ = false;
    const QPointF global = QCursor::pos();
    const QPointF local = global - window_->position();
    // A stationary OS pointer does not necessarily produce a move when the
    // centred window resizes under it. Refresh Qt's own hover hit testing only;
    // this neither moves the OS cursor nor activates a window or sends a click.
    if (QRectF(0, 0, window_->width(), window_->height()).contains(local)) {
        QMouseEvent event(QEvent::MouseMove, local, local, global, Qt::NoButton,
            Qt::NoButton, QGuiApplication::keyboardModifiers(), Qt::MouseEventSynthesizedByApplication);
        QCoreApplication::sendEvent(window_, &event);
    } else {
        QEvent event(QEvent::Leave);
        QCoreApplication::sendEvent(window_, &event);
    }
}

bool ShellController::gazeTrackingEnabled() const {
    return window_ && window_->isVisible() && !suppressed_ && !state_->passThroughActive()
        && state_->section() == QStringLiteral("home") && state_->eyesEnabled()
        && !state_->reducedMotion()
        && state_->eventTitle().isEmpty();
}

void ShellController::resetGaze() {
    if (gaze_.reset()) emit gazeChanged();
}

void ShellController::poll() {
    if (!window_) return;
    PollTrace trace;
    const auto now = clock_.elapsed();
    const float dt = static_cast<float>(now - lastTick_) / 1000;
    lastTick_ = now;
    if (now >= nextFullscreenCheck_) {
        nextFullscreenCheck_ = now + 400;
#ifdef Q_OS_WIN
        setSuppressed(foregroundFillsIslandMonitor(window_));
#endif
    }
    if (trace.clock.isValid()) trace.fullscreen = trace.clock.elapsed();
    if (!window_->isVisible() || suppressed_ || state_->passThroughActive()) {
        resetGaze();
        interaction_.suspend();
        wasPressed_ = false;
        ignorePressUntilRelease_ = true;
        if (ticker_.interval() != 150) ticker_.setInterval(150);
        return;
    }
    const qreal width = window_->property("shellWidth").toReal();
    const qreal height = window_->property("shellHeight").toReal();
    const QPointF globalCursor = QCursor::pos();
    const QPointF shellOrigin = QPointF(window_->position()) + QPointF(window_->property("shellX").toReal(), window_->property("shellY").toReal());
    const QPointF cursor = globalCursor - shellOrigin;
    // Independent of edge springs and their nearby early return. Track both
    // compact and expanded Home, using the selected island monitor's scale.
    if (gazeTrackingEnabled()) {
        const auto *screen = window_->screen();
        const QSizeF screenSize = screen ? QSizeF(screen->geometry().size()) : QSizeF(1920, 1080);
        if (gaze_.sample(globalCursor, shellOrigin + QPointF(width / 2, window_->property("eyeCenterY").isValid() ? window_->property("eyeCenterY").toReal() : height / 2), screenSize))
            emit gazeChanged();
    } else resetGaze();
    const QRectF body(0, 0, width, height);
    const qreal grace = state_->expanded() ? 14 : 0;
    const auto target = islandBodyPath(body.adjusted(-grace, -grace, grace, grace), 24,
        QString{}, false, window_->property("crownHeight").toReal());
    const bool inside = target.contains(cursor);
    const bool nearby = body.adjusted(-24, -24, 24, 24).contains(cursor);
    trace.nearby = nearby;
    if (inputTrace().enabled && inside != inputTrace().inside) {
        inputTrace().inside = inside;
        std::fprintf(stderr, "input-trace boundary at=%lldms inside=%d expanded=%d poll-gap=%lldms\n",
            (long long)InputTrace::now(), inside, state_->expanded(), (long long)trace.gap);
    }
    bool pressed = QGuiApplication::mouseButtons() != Qt::NoButton;
#ifdef Q_OS_WIN
    pressed = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000);
#endif
    // The frame is fixed at the expanded size; only the visible shell takes input.
    const auto beforeClickThrough = trace.clock.isValid() ? trace.clock.elapsed() : 0;
    setClickThrough(!islandBodyPath(body.adjusted(-2, -2, 2, 2), 24,
        QString{}, false, window_->property("crownHeight").toReal()).contains(cursor));
    if (trace.clock.isValid()) trace.clickThrough = trace.clock.elapsed() - beforeClickThrough;
    if (!pressed) ignorePressUntilRelease_ = false;
    if (pressed && !wasPressed_ && !ignorePressUntilRelease_ && !inside && state_->expanded() && !pinned_ && !window_->property("interactionHold").toBool()) close();
    wasPressed_ = pressed;
    const auto beforeHover = trace.clock.isValid() ? trace.clock.elapsed() : 0;
    refreshHoverAfterGeometryChange(pressed);
    if (trace.clock.isValid()) trace.hover = trace.clock.elapsed() - beforeHover;
    const auto beforeInteraction = trace.clock.isValid() ? trace.clock.elapsed() : 0;
    const auto action = interaction_.update({inside, state_->expanded(), pinned_ || window_->property("interactionHold").toBool() || now < holdUntil_, pressed}, now);
    if (action == ShellInteraction::Action::Open) {
        hoverOpening_ = true;
        state_->setExpanded(true);
        hoverOpening_ = false;
    }
    else if (action == ShellInteraction::Action::Close) state_->setExpanded(false);
    if (trace.clock.isValid()) trace.interaction = trace.clock.elapsed() - beforeInteraction;

    x_.target = static_cast<float>(std::clamp(cursor.x(), -24.0, width + 24));
    y_.target = static_cast<float>(std::clamp(cursor.y(), -24.0, height + 24));
    proximity_.target = nearby && !state_->reducedMotion() ? 1 : 0;
    const auto oldX = x_.value, oldY = y_.value, oldProximity = proximity_.value;
    if (!nearby && proximity_.value == 0) {
        x_.reset(x_.target); y_.reset(y_.target);
        if (ticker_.interval() != 80) ticker_.setInterval(80);
        return;
    }
    if (state_->reducedMotion()) { x_.reset(x_.target); y_.reset(y_.target); proximity_.reset(0); }
    else { x_.step(dt, 360, 30); y_.step(dt, 360, 30); proximity_.step(dt); }
    if (std::abs(oldX - x_.value) > 0.05 || std::abs(oldY - y_.value) > 0.05 || std::abs(oldProximity - proximity_.value) > 0.002)
        emit pointerChanged();
    const int interval = nearby || proximity_.value > 0.01 ? 16 : 80;
    if (ticker_.interval() != interval) ticker_.setInterval(interval);
}
