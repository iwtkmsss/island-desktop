#include "shellcontroller.h"
#include "appstate.h"
#include "islandshape.h"
#include "kwinpointer.h"
#include <QCursor>
#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QPainterPath>
#include <QQuickWindow>
#include <QScreen>

#include <QtGui/qguiapplication_platform.h>
#include <xcb/shape.h>
#include <KX11Extras>
#include <KWindowInfo>

namespace {
bool wayland() { return QGuiApplication::platformName().startsWith("wayland"); }
xcb_connection_t *x11Connection() {
    if (QGuiApplication::platformName() != "xcb") return nullptr;
    const auto *native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    return native ? native->connection() : nullptr;
}
bool foregroundFillsIslandMonitor(QQuickWindow *window) {
    if (wayland() || QGuiApplication::platformName() != "xcb") return false;
    const auto active = KX11Extras::activeWindow();
    if (!active || active == window->winId()) return false;
    KWindowInfo info(active, NET::WMState | NET::WMGeometry | NET::WMWindowType);
    if (!info.valid() || !info.hasState(NET::FullScreen)) return false;
    const auto screen = window->screen();
    if (!screen) return false;
    const auto centre = info.geometry().center() / window->devicePixelRatio();
    return screen->geometry().contains(centre);
}
}

bool ShellController::eventFilter(QObject *watched, QEvent *event) {
    if (watched != window_) return false;
    if (handleDoubleClick(event)) return true;
    if (event->type() == QEvent::Enter) {
        window_->setProperty("islandPointerInside", true);
        holdUntil_ = 0; // The cursor arrived; a later leave uses the normal 620 ms grace.
        window_->setProperty("islandPointerPosition", static_cast<QEnterEvent *>(event)->position());
    } else if (event->type() == QEvent::MouseMove) {
        window_->setProperty("islandPointerPosition", static_cast<QMouseEvent *>(event)->position());
    } else if (event->type() == QEvent::Leave) {
        window_->setProperty("islandPointerInside", false);
        // Even a leave/return between polling ticks must release Escape's guard.
        const auto now = clock_.elapsed();
        const auto action = interaction_.update({false, state_->expanded(), pinned_ || window_->property("interactionHold").toBool() || now < holdUntil_,
            QGuiApplication::mouseButtons() != Qt::NoButton, window_->isVisible() && !suppressed_}, now);
        if (action == ShellInteraction::Action::Close) close();
    }
    return false;
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
        if (!state_->expanded()) {
            if (window_ && window_->property("islandPointerInside").toBool()) interaction_.suppressUntilLeave();
            else interaction_.suspend();
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
    if (qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland"
        && qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains("KDE", Qt::CaseInsensitive)
        && (wayland() || QGuiApplication::platformName() == "xcb")) {
        desktopPointer_ = std::make_unique<KWinPointer>(window->property("pointerBridgeKey").toString(), this);
        connect(desktopPointer_.get(), &KWinPointer::availabilityChanged, this, [this] {
            resetGaze();
            lastPointerSerial_ = desktopPointer_->movementSerial();
            if (window_) window_->setProperty("globalPointerAvailable", desktopPointer_->available());
        });
        desktopPointer_->setEnabled(window->isVisible() && !suppressed_ && !state_->passThroughActive());
    }
    resetGaze();
    connect(window, &QWindow::visibleChanged, this, [this] {
        if (window_ && window_->isVisible()) setNativeInputSuppressed(suppressed_ || state_->passThroughActive());
        if (window_ && window_->isVisible() && x11Connection()) {
            // Qt writes its utility-window type while mapping. Apply the overlay
            // role afterward, then position against the physical screen edge.
            // No strut is reserved, so other applications retain their work area.
            QTimer::singleShot(0, this, [this] {
                if (!window_ || !window_->isVisible() || !x11Connection()) return;
                KX11Extras::setType(window_->winId(), NET::Dock);
                emit frameRequested();
            });
        }
        if (desktopPointer_) desktopPointer_->setEnabled(window_ && window_->isVisible() && !suppressed_ && !state_->passThroughActive());
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
    if (desktopPointer_) desktopPointer_->setEnabled(window_ && window_->isVisible() && !suppressed && !state_->passThroughActive());
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
    if (!window_) return;
    if (suppressed) {
        window_->setProperty("islandInputRegion", QVariant());
        if (auto *connection = x11Connection()) {
            xcb_shape_rectangles(connection, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT,
                XCB_CLIP_ORDERING_UNSORTED, window_->winId(), 0, 0, 0, nullptr);
            xcb_flush(connection);
        } else {
            // An empty QRegion resets the mask. A nonempty region wholly outside
            // the surface yields an actually empty Wayland input region.
            window_->setMask(QRegion(QRect(-2, -2, 1, 1)));
        }
    } else setClickThrough(false);
}

// Adapted from SetClickThrough in the supplied mod 1.4.0, lines 10226–10241.
// Copyright (c) 2026 devcode90, MIT; third_party/dynamic-island/LICENSE.
void ShellController::setClickThrough(bool clickThrough) {
    Q_UNUSED(clickThrough)
    if (!window_ || suppressed_ || state_->passThroughActive()) return;
    const qreal dpr = wayland() ? 1 : window_->devicePixelRatio();
    const QRectF body(window_->property("shellX").toReal(), window_->property("shellY").toReal(),
        window_->property("shellWidth").toReal(), window_->property("shellHeight").toReal());
    const auto path = islandBodyPath(body.adjusted(-6, -6, 6, 6), 28,
        QString{}, false, window_->property("crownHeight").toReal());
    const QRegion region(path.toFillPolygon(QTransform::fromScale(dpr, dpr)).toPolygon());
    if (auto *connection = x11Connection()) {
        if (window_->property("islandInputRegion").value<QRegion>() == region) return;
        window_->setProperty("islandInputRegion", region);
        QList<xcb_rectangle_t> rects;
        for (const auto &rect : region) rects.append({int16_t(rect.x()), int16_t(rect.y()), uint16_t(rect.width()), uint16_t(rect.height())});
        xcb_shape_rectangles(connection, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT,
            XCB_CLIP_ORDERING_UNSORTED, window_->winId(), 0, 0, rects.size(), rects.constData());
        xcb_flush(connection);
    } else if (window_->mask() != region) {
        window_->setMask(region);
    }
}

void ShellController::refreshHoverAfterGeometryChange(bool pressed) {
    if (!geometryChanged_ || pressed || !window_ || suppressed_ || !window_->isVisible()) return;
    geometryChanged_ = false;
    if (!window_->property("islandPointerInside").toBool()) return;
    const QPointF global = desktopPointer_ && desktopPointer_->available() ? desktopPointer_->position()
        : QPointF(window_->position()) + window_->property("islandPointerPosition").toPointF();
    const QPointF local = global - window_->position();
    // A stationary OS pointer does not necessarily produce a move when the
    // centred window resizes under it. Refresh Qt's own hover hit testing only;
    // this neither moves the OS cursor nor activates a window or sends a click.
    if (QRectF(0, 0, window_->width(), window_->height()).contains(local)) {
        QMouseEvent event(QEvent::MouseMove, local, local, global, Qt::NoButton,
            Qt::NoButton, QGuiApplication::keyboardModifiers(), Qt::MouseEventSynthesizedByApplication);
        QCoreApplication::sendEvent(window_, &event);
    }
    // Leave is a native crossing event. Never fabricate it from an XWayland
    // coordinate query: that query may already be stale over a Wayland client.
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
    const auto now = clock_.elapsed();
    const float dt = static_cast<float>(now - lastTick_) / 1000;
    lastTick_ = now;
    if (now >= nextFullscreenCheck_) {
        nextFullscreenCheck_ = now + 400;
        setSuppressed(foregroundFillsIslandMonitor(window_));
    }
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
    const bool compositorPointer = desktopPointer_ && desktopPointer_->available();
    const bool nativeX11 = QGuiApplication::platformName() == "xcb" && qEnvironmentVariable("XDG_SESSION_TYPE") != "wayland";
    const bool pointerKnown = compositorPointer || nativeX11 || window_->property("islandPointerInside").toBool();
    const QPointF globalCursor = compositorPointer ? desktopPointer_->position() : nativeX11 ? QCursor::pos()
        : QPointF(window_->position()) + window_->property("islandPointerPosition").toPointF();
    const auto serial = compositorPointer ? desktopPointer_->movementSerial() : 0;
    const bool observedMovement = compositorPointer && serial != lastPointerSerial_;
    lastPointerSerial_ = serial;
    const QPointF shellOrigin = QPointF(window_->position()) + QPointF(window_->property("shellX").toReal(), window_->property("shellY").toReal());
    const bool entered = window_->property("islandPointerInside").toBool();
    const QPointF cursor = !nativeX11 && entered
        ? window_->property("islandPointerPosition").toPointF() - QPointF(window_->property("shellX").toReal(), window_->property("shellY").toReal())
        : globalCursor - shellOrigin;
    // Independent of edge springs and their nearby early return. Track both
    // compact and expanded Home, using the selected island monitor's scale.
    if (gazeTrackingEnabled() && pointerKnown) {
        const auto *screen = window_->screen();
        const QSizeF screenSize = screen ? QSizeF(screen->geometry().size()) : QSizeF(1920, 1080);
        if (gaze_.sample(globalCursor, shellOrigin + QPointF(width / 2, window_->property("eyeCenterY").isValid() ? window_->property("eyeCenterY").toReal() : height / 2), screenSize, observedMovement))
            emit gazeChanged();
    } else resetGaze();
    const QRectF body(0, 0, width, height);
    const qreal grace = state_->expanded() ? 14 : 0;
    const auto target = islandBodyPath(body.adjusted(-grace, -grace, grace, grace), 24,
        QString{}, false, window_->property("crownHeight").toReal());
    // Crossing events remain authoritative under Wayland, even when a global
    // bridge's last update is still in flight. The compositor never has to
    // grant global input access just to make leave-to-close reliable.
    const bool inside = (nativeX11 ? pointerKnown : entered) && target.contains(cursor);
    const bool nearby = pointerKnown && body.adjusted(-24, -24, 24, 24).contains(cursor);
    bool pressed = QGuiApplication::mouseButtons() != Qt::NoButton;
    // The frame is fixed at the expanded size; only the visible shell takes input.
    setClickThrough(!body.adjusted(-2, -2, 2, 2).contains(cursor));
    if (!pressed) ignorePressUntilRelease_ = false;
    if (pressed && !wasPressed_ && !ignorePressUntilRelease_ && !inside && state_->expanded() && !pinned_ && !window_->property("interactionHold").toBool()) close();
    wasPressed_ = pressed;
    refreshHoverAfterGeometryChange(pressed);
    const auto action = interaction_.update({inside, state_->expanded(), pinned_ || window_->property("interactionHold").toBool() || now < holdUntil_, pressed}, now);
    if (action == ShellInteraction::Action::Open) {
        hoverOpening_ = true;
        state_->setExpanded(true);
        hoverOpening_ = false;
    }
    else if (action == ShellInteraction::Action::Close) state_->setExpanded(false);

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
