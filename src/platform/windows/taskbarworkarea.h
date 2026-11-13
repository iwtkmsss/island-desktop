#pragma once
#include "../../core/taskbarworkarea.h"
#include <windows.h>
#include <memory>
#include <string>
#include <vector>

namespace TaskbarNative {
using WorkRect = IslandDesktop::WorkArea::Rect;
struct MonitorWorkArea {
    std::wstring device;
    WorkRect monitor, work;
};
class WorkAreaOperations {
public:
    virtual ~WorkAreaOperations() = default;
    virtual bool permitted() { return true; }
    virtual std::vector<MonitorWorkArea> monitors() = 0;
    virtual void prepare(const MonitorWorkArea &, WorkRect, bool) {}
    virtual bool set(const MonitorWorkArea &monitor, WorkRect desired) = 0;
    virtual void reflow(const MonitorWorkArea &, WorkRect, WorkRect, bool) {}
    virtual void finishRestore() {}
};
std::unique_ptr<WorkAreaOperations> systemWorkAreaOperations(const std::wstring &marker);
void setActiveWorkAreaMarker(const std::wstring &marker);
bool isKnownWindowedMaximized(HWND window);
bool isInteractiveSession();

// Parent holds this mapping until guardian cleanup/fallback is complete. Records
// are immutable once published, including a pending change before its SPI call.
class WorkAreaJournal {
public:
    WorkAreaJournal();
    ~WorkAreaJournal();
    WorkAreaJournal(const WorkAreaJournal &) = delete;
    WorkAreaJournal &operator=(const WorkAreaJournal &) = delete;
    bool create(const std::wstring &marker);
    bool open(const std::wstring &marker);
    bool release(const MonitorWorkArea &monitor, WorkRect taskbar, WorkAreaOperations &operations);
    bool restore(WorkAreaOperations &operations);
    HANDLE nativeHandle() const { return mapping_; }
private:
    struct Data;
    HANDLE mapping_ = nullptr;
    Data *data_ = nullptr;
};
}
