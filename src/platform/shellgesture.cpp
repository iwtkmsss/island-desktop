#include "shellcontroller.h"
#include "appstate.h"
#include "islandshape.h"
#include <QMouseEvent>
#include <QQuickWindow>
#include <QQuickItem>
#ifdef Q_OS_LINUX
#include "linux/kwinpointer.h"
#endif

bool ShellController::handleDoubleClick(QEvent *event) {
    if (!window_ || event->type() != QEvent::MouseButtonDblClick || suppressed_) return false;
    const auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::LeftButton) return false;
    if (state_->passThroughActive()) return true;
    const QRectF body(window_->property("shellX").toReal(), window_->property("shellY").toReal(),
        window_->property("shellWidth").toReal(), window_->property("shellHeight").toReal());
    if (!islandBodyPath(body, 25, {}, false, window_->property("crownHeight").toReal()).contains(mouse->position())) return false;
    // Interactive controls opt out; background/eyes keep the established gesture.
    QQuickItem *item=window_->contentItem();
    while (auto *child=item->childAt(item->mapFromScene(mouse->position()).x(),item->mapFromScene(mouse->position()).y())) {
        item=child;
        if(item->property("islandInteractive").toBool()) return false;
    }
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
