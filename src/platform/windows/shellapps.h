#pragma once
// Win32 / shell helpers for the Windows Home panel. Handles are passed as
// quintptr so this header does not pull in <windows.h>. Every function
// tolerates windows and processes that vanished in the meantime and never
// blocks on another process for more than a short SendMessageTimeout.
#include <QImage>
#include <QList>
#include <QString>
#include <memory>

namespace IslandDesktop::Shell {

struct WindowInfo {
    quintptr handle = 0;
    quint32 processId = 0;
    QString title;
    QString className;
    bool minimized = false;
};
// Taskbar-eligible top-level windows of the current virtual desktop in
// z-order, top first.
QList<WindowInfo> taskbarWindows();

// Explicit AppUserModelID set on the window (PKEY_AppUserModel_ID), or empty.
QString windowAppId(quintptr window);
// For a UWP frame (class ApplicationFrameWindow): AUMID of the process that
// owns the hosted CoreWindow, or empty if it cannot be found right now.
QString frameAppId(quintptr window);
// AUMID of a packaged (MSIX / Store) process, or empty for unpackaged ones.
QString processAppId(quint32 processId);
QString processImagePath(quint32 processId);

// FileDescription from the version resource, or empty.
QString fileDescription(const QString &path);
// Display name of shell:AppsFolder\<id>, or empty if there is no such item.
QString appsFolderName(const QString &id);

QImage appsFolderImage(const QString &id, int size);
QImage fileImage(const QString &path, int size);
// The window's own icon (WM_GETICON with a timeout, then the class icon).
QImage windowIcon(quintptr window);

bool launchAppsFolder(const QString &id);
bool launchFile(const QString &path);
bool openUri(const QString &uri);

struct StartApp {
    QString name;
    std::shared_ptr<void> item;  // absolute PIDL, freed with CoTaskMemFree
};
// Every application of the Start menu ("All apps"), unsorted. Safe to call on
// any thread that has initialised COM.
QList<StartApp> startApps();
QImage itemImage(const std::shared_ptr<void> &item, int size);
bool launchItem(const std::shared_ptr<void> &item);

// Restores the window if minimized and brings it to the foreground.
bool activateWindow(quintptr window);
bool minimizeWindow(quintptr window);
quintptr foregroundWindow();
bool isOwnWindow(quintptr window);
// The window still exists and is not minimized.
bool isRestoredWindow(quintptr window);
// Cursor position in physical screen pixels.
void cursorPosition(int *x, int *y);

}  // namespace IslandDesktop::Shell
