#pragma once
// Notifies about top-level window changes relevant to the task row:
// RegisterShellHookWindow on a hidden helper window plus out-of-context
// WinEvent hooks for foreground, minimize and cloak changes. Everything runs on
// the thread that created the watcher (the GUI thread); callbacks may arrive
// while a nested event loop runs, so they should only schedule work.
#include <QtGlobal>
#include <functional>
#include <vector>

class TaskWatcher final {
public:
    TaskWatcher(std::function<void()> changed, std::function<void(quintptr)> foreground);
    ~TaskWatcher();
    TaskWatcher(const TaskWatcher &) = delete;
    TaskWatcher &operator=(const TaskWatcher &) = delete;
    bool running() const { return window_ != nullptr; }

private:
    friend struct TaskWatcherAccess;
    std::function<void()> changed_;
    std::function<void(quintptr)> foreground_;
    void *window_ = nullptr;           // HWND
    unsigned shellMessage_ = 0;
    std::vector<void *> hooks_;        // HWINEVENTHOOK
};
