#pragma once
// Windows notification-area host for the Home panel.
//
// Windows has no public API to enumerate other applications' tray icons.
// Shell_NotifyIcon() locates the top-most window of class "Shell_TrayWnd" and
// sends it WM_COPYDATA. TrayHost owns a hidden window of that class placed
// above Explorer's, records every icon and forwards each message unchanged to
// Explorer, so the regular taskbar tray keeps working. The same technique is
// used by alternative Windows shells (ManagedShell / RetroBar).
//
// Threading contract: every public member is called on the GUI thread and
// iconsChanged() is emitted on the GUI thread. The host window lives on its own
// thread so a busy GUI thread never delays other applications' Shell_NotifyIcon.
#include <QImage>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QString>
#include <memory>

struct TrayIconInfo {
    QString id;                 // unique and stable while the icon is registered
    QString title;              // tooltip text; may be empty
    QImage image;               // deep copy of the icon pixels; null if unreadable
    quint64 imageRevision = 0;  // changes only when the pixels change
};

class TrayHost final : public QObject {
    Q_OBJECT
public:
    struct Options {
        // Tests use a private class name with forwarding and announcing off,
        // so they never touch the real notification area.
        QString windowClass = QStringLiteral("Shell_TrayWnd");
        bool forwardToShell = true;  // relay every message to Explorer's tray
        bool announce = true;        // broadcast TaskbarCreated so running apps re-add icons
    };
    enum class Action { Activate, SecondaryActivate, ContextMenu };

    explicit TrayHost(QObject *parent = nullptr);
    explicit TrayHost(const Options &options, QObject *parent = nullptr);
    ~TrayHost() override;

    // Creates the host thread and window. Returns false if the window could not
    // be created; the object then stays inert. Safe to call once.
    bool start();
    // Destroys the window and joins the thread. Icons stay in Explorer's tray.
    void stop();
    bool running() const;
    // Native handle of the host window (0 when not running). For tests.
    quintptr windowHandle() const;

    // Visible icons in registration order.
    QList<TrayIconInfo> icons() const;
    // Delivers a click to the icon's owner the way Explorer does. `cursor` is
    // the pointer position in physical screen pixels. Returns false if the icon
    // is unknown or its owner window is gone.
    bool trigger(const QString &id, Action action, const QPoint &cursor);
    // True while a native Win32 popup menu (class #32768) is visible anywhere.
    static bool nativeMenuOpen();

signals:
    void iconsChanged();

private:
    class Private;
    std::unique_ptr<Private> d;
};
