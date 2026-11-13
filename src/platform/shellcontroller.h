#pragma once
#include "borrowed/spring.h"
#include "cursorgaze.h"
#include "shellinteraction.h"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#ifdef Q_OS_LINUX
#include <memory>
class KWinPointer;
#endif

class AppState;
class QQuickWindow;
class ShellController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(qreal pointerX READ pointerX NOTIFY pointerChanged)
    Q_PROPERTY(qreal pointerY READ pointerY NOTIFY pointerChanged)
    Q_PROPERTY(qreal proximity READ proximity NOTIFY pointerChanged)
    Q_PROPERTY(qreal gazeX READ gazeX NOTIFY gazeChanged)
    Q_PROPERTY(qreal gazeY READ gazeY NOTIFY gazeChanged)
    Q_PROPERTY(quint64 pointerActivity READ pointerActivity NOTIFY gazeChanged)
    Q_PROPERTY(bool pinned READ pinned WRITE setPinned NOTIFY pinnedChanged)
    Q_PROPERTY(bool suppressed READ suppressed NOTIFY suppressionChanged)
public:
    explicit ShellController(AppState *state, QObject *parent = nullptr);
    ~ShellController() override;
    void attach(QQuickWindow *window);
    qreal pointerX() const { return x_.value; }
    qreal pointerY() const { return y_.value; }
    qreal proximity() const { return proximity_.value; }
    qreal gazeX() const { return gaze_.x(); }
    qreal gazeY() const { return gaze_.y(); }
    quint64 pointerActivity() const { return gaze_.activity(); }
    bool pinned() const { return pinned_; }
    bool suppressed() const { return suppressed_; }
    void setPinned(bool pinned);
    Q_INVOKABLE void open();
    Q_INVOKABLE void close();
    // QML asks for its frameWidth/frameHeight to be applied to the native window.
    Q_INVOKABLE void requestFrame() { emit frameRequested(); }
signals:
    void frameRequested();
    void pointerChanged();
    void gazeChanged();
    void pinnedChanged();
    void suppressionChanged();
private:
#ifdef Q_OS_WIN
    friend class WindowsShellTest;
#endif
    void poll();
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool handleDoubleClick(QEvent *event);
    void updatePassThrough();
    bool gazeTrackingEnabled() const;
    void resetGaze();
    void setSuppressed(bool suppressed);
    void setNativeInputSuppressed(bool suppressed);
    void setClickThrough(bool clickThrough);
    void refreshHoverAfterGeometryChange(bool pressed);
#ifdef Q_OS_LINUX
    std::unique_ptr<KWinPointer> desktopPointer_;
    quint64 lastPointerSerial_ = 0;
#endif
    AppState *state_;
    QQuickWindow *window_ = nullptr;
    QTimer ticker_;
    QElapsedTimer clock_;
    qint64 lastTick_ = 0;
    qint64 holdUntil_ = 0;
    qint64 nextFullscreenCheck_ = 0;
    ShellInteraction interaction_;
    IslandSpring x_, y_, proximity_;
    CursorGaze gaze_;
    bool pinned_ = false;
    bool wasPressed_ = false;
    bool ignorePressUntilRelease_ = false;
    bool hoverOpening_ = false;
    bool suppressed_ = false;
    bool geometryChanged_ = true;
    quintptr suppressedWindowId_ = 0;
    quintptr savedWindowRegion_ = 0;
    bool wasNativeInputTransparent_ = false;
    bool nativeRegionSuppressed_ = false;
};
