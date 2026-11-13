#include "taskwatcher.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>

namespace {
constexpr wchar_t windowClass[] = L"IslandDesktopTaskWatcher";
std::vector<TaskWatcher *> &watchers() {
    static std::vector<TaskWatcher *> list;
    return list;
}
}

struct TaskWatcherAccess {
    static void changed(TaskWatcher *watcher) {
        if (watcher->changed_) watcher->changed_();
    }
    static void foreground(TaskWatcher *watcher, HWND window) {
        if (watcher->foreground_) watcher->foreground_(reinterpret_cast<quintptr>(window));
    }
    static bool owns(const TaskWatcher *watcher, HWINEVENTHOOK hook) {
        return std::find(watcher->hooks_.begin(), watcher->hooks_.end(), static_cast<void *>(hook)) != watcher->hooks_.end();
    }
    static unsigned shellMessage(const TaskWatcher *watcher) { return watcher->shellMessage_; }

    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto *watcher = reinterpret_cast<TaskWatcher *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (watcher && message == shellMessage(watcher) && message) {
            switch (wParam & 0x7fff) {  // strip HSHELL_HIGHBIT (rude activation, flash)
            case HSHELL_WINDOWACTIVATED:
                foreground(watcher, reinterpret_cast<HWND>(lParam));
                [[fallthrough]];
            case HSHELL_WINDOWCREATED:
            case HSHELL_WINDOWDESTROYED:
            case HSHELL_REDRAW:
            case HSHELL_GETMINRECT:
            case HSHELL_WINDOWREPLACED:
                changed(watcher);
                break;
            default:
                break;
            }
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    static void CALLBACK eventProc(HWINEVENTHOOK hook, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) {
        if (!window || object != OBJID_WINDOW || child != CHILDID_SELF) return;
        // Copy: a callback may destroy a watcher.
        const auto list = watchers();
        for (auto *watcher : list) {
            if (std::find(watchers().begin(), watchers().end(), watcher) == watchers().end() || !owns(watcher, hook))
                continue;
            if (event == EVENT_SYSTEM_FOREGROUND) foreground(watcher, window);
            else if ((event == EVENT_OBJECT_CLOAKED || event == EVENT_OBJECT_UNCLOAKED)
                     && GetAncestor(window, GA_ROOT) != window)
                continue;
            changed(watcher);
        }
    }
};

TaskWatcher::TaskWatcher(std::function<void()> changed, std::function<void(quintptr)> foreground)
    : changed_(std::move(changed)), foreground_(std::move(foreground)) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    if (!GetClassInfoExW(instance, windowClass, &cls)) {
        cls = {};
        cls.cbSize = sizeof(cls);
        cls.lpfnWndProc = &TaskWatcherAccess::windowProc;
        cls.hInstance = instance;
        cls.lpszClassName = windowClass;
        RegisterClassExW(&cls);
    }
    // A hidden top-level window: RegisterShellHookWindow does not accept
    // message-only windows. It is never shown, so it never appears as a task.
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, windowClass, L"", WS_POPUP,
                                  0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (window) {
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        shellMessage_ = RegisterWindowMessageW(L"SHELLHOOK");
        RegisterShellHookWindow(window);
        window_ = window;
    }
    watchers().push_back(this);
    // Narrow ranges only: the whole EVENT_OBJECT_* range floods system-wide.
    const DWORD ranges[][2] = {{EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
                               {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
                               {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED}};
    for (const auto &range : ranges) {
        if (auto hook = SetWinEventHook(range[0], range[1], nullptr, &TaskWatcherAccess::eventProc, 0, 0,
                                        WINEVENT_OUTOFCONTEXT))
            hooks_.push_back(hook);
    }
}

TaskWatcher::~TaskWatcher() {
    for (auto *hook : hooks_) UnhookWinEvent(static_cast<HWINEVENTHOOK>(hook));
    hooks_.clear();
    auto &list = watchers();
    list.erase(std::remove(list.begin(), list.end(), this), list.end());
    if (window_) {
        const HWND window = static_cast<HWND>(window_);
        DeregisterShellHookWindow(window);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        DestroyWindow(window);
        window_ = nullptr;
    }
}
