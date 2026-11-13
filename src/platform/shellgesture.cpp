#include "shellcontroller.h"
#include "appstate.h"
#include "islandshape.h"
#include <QMouseEvent>
#include <QQuickWindow>
#include <QQuickItem>
#ifdef Q_OS_LINUX
#include "linux/kwinpointer.h"
#endif

namespace {
bool interactiveAt(QQuickItem *item, const QPointF &scenePosition) {
    if (!item || !item->isVisible() || !item->isEnabled() || item->opacity() <= 0 || item->scale() == 0) return false;
    const auto local = item->mapFromScene(scenePosition);
    if (item->clip() && !item->boundingRect().contains(local)) return false;
    if (item->property("islandInteractive").toBool() && item->contains(local)) return true;
    // Decorations can sit above controls, and non-clipping layout containers can
    // have zero size while their children overflow. Search every visual branch;
    // only an actual clip (or a hidden/disabled ancestor) may prune descendants.
    for (auto *child : item->childItems()) {
        if (interactiveAt(child, scenePosition)) return true;
    }
    return false;
}
}

bool ShellController::handleDoubleClick(QEvent *event) {
    if (!window_ || event->type() != QEvent::MouseButtonDblClick || suppressed_) return false;
    const auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::LeftButton) return false;
    if (state_->passThroughActive()) return true;
    const QRectF body(window_->property("shellX").toReal(), window_->property("shellY").toReal(),
        window_->property("shellWidth").toReal(), window_->property("shellHeight").toReal());
    if (!islandBodyPath(body, 25, {}, false, window_->property("crownHeight").toReal()).contains(mouse->position())) return false;
    // Interactive controls opt out; background/eyes keep the established gesture.
    if (interactiveAt(window_->contentItem(), mouse->position())) return false;
    state_->startPassThrough();
    event->accept(); return true;
}

void ShellController::updatePassThrough() {
    if (state_->passThroughActive()) setPinned(false);
    interaction_.suppressUntilLeave();
    holdUntil_ = 0;
    ignorePressUntilRelease_ = true;
    proximity_.reset(0);
    resetGaze();
    geometryChanged_ = true;
    setNativeInputSuppressed(suppressed_ || state_->passThroughActive());
#ifdef Q_OS_LINUX
    if (desktopPointer_) desktopPointer_->setEnabled(window_ && window_->isVisible() && !suppressed_ && !state_->passThroughActive());
#endif
    emit pointerChanged();
}
