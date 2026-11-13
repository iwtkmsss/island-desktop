#include "taskbarworkarea.h"
#include <dwmapi.h>
#include <wtsapi32.h>
#include <algorithm>
#include <cwchar>

namespace TaskbarNative {
namespace {
namespace Area = IslandDesktop::WorkArea;
constexpr LONG journalMagic = 0x49574131;
constexpr LONG maxRecords = 64;
constexpr LONG active = 1, retired = 2;
constexpr size_t deviceCapacity = 512;
std::wstring activeMarker;
struct Record {
    volatile LONG state = 0;
    wchar_t device[deviceCapacity]{};
    WorkRect monitor;
    Area::Change change{};
};
class PhysicalCoordinates {
public:
    PhysicalCoordinates() : previous_(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {}
    ~PhysicalCoordinates() { if (previous_) SetThreadDpiAwarenessContext(previous_); }
private:
    DPI_AWARENESS_CONTEXT previous_;
};
WorkRect rectangle(RECT r) { return {r.left, r.top, r.right, r.bottom}; }
RECT rectangle(WorkRect r) { return {r.left, r.top, r.right, r.bottom}; }
std::wstring mappingName(const std::wstring &marker) { return L"Local\\" + marker + L".WorkArea"; }
BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter) {
    auto &result = *reinterpret_cast<std::vector<MonitorWorkArea> *>(parameter);
    MONITORINFOEXW info{}; info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return TRUE;
    DISPLAY_DEVICEW device{}; device.cb = sizeof(device);
    std::wstring identity = info.szDevice;
    if (EnumDisplayDevicesW(info.szDevice, 0, &device, EDD_GET_DEVICE_INTERFACE_NAME)) identity += L"|" + std::wstring(device.DeviceID);
    result.push_back({std::move(identity), rectangle(info.rcMonitor), rectangle(info.rcWork)});
    return TRUE;
}
bool zoomed(HWND window) {
    return IsWindowVisible(window) && !IsIconic(window) && IsZoomed(window);
}
struct ReflowScan {
    WorkRect monitor, before, after;
    const std::wstring &marker;
    bool restoring;
    bool prepare = false;
};
BOOL CALLBACK reflowWindow(HWND window, LPARAM parameter) {
    auto &scan = *reinterpret_cast<ReflowScan *>(parameter);
    if (!zoomed(window)) return TRUE;
    DWORD cloaked = 0;
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) || cloaked) return TRUE;
    MONITORINFO info{}; info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONULL), &info) || rectangle(info.rcMonitor) != scan.monitor) return TRUE;
    const auto style = GetWindowLongPtrW(window, GWL_STYLE);
    const HANDLE original = GetPropW(window, scan.marker.c_str());
    if (scan.restoring && (!original || reinterpret_cast<ULONG_PTR>(original) != static_cast<ULONG_PTR>(style))) return TRUE;
    RECT outer{}, frame{};
    if (!GetWindowRect(window, &outer)
        || FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) return TRUE;
    if (!scan.restoring && !Area::windowedBeforeRelease(scan.monitor, scan.before, rectangle(frame))) return TRUE;
    const auto target = Area::reflow(scan.before, scan.after, rectangle(frame), rectangle(outer));
    if (!target) return TRUE;
    // A pre-existing fullscreen frame cannot match a reserved old work area.
    // Restore requires our HWND-lifetime marker and unchanged maximized style;
    // newly opened fullscreen windows are never resized by cleanup.
    if (!scan.restoring && !original
        && !SetPropW(window, scan.marker.c_str(), reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(style)))) return TRUE;
    if (scan.prepare) return TRUE;
    SetWindowPos(window, nullptr, target->left, target->top, target->right - target->left, target->bottom - target->top,
        SWP_ASYNCWINDOWPOS | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    return TRUE;
}
BOOL CALLBACK clearWindowMarker(HWND window, LPARAM parameter) {
    RemovePropW(window, reinterpret_cast<const wchar_t *>(parameter)); return TRUE;
}
class SystemOperations final : public WorkAreaOperations {
public:
    bool permitted() override { return isInteractiveSession(); }
    explicit SystemOperations(const std::wstring &marker) : reflowMarker_(marker + L".ReflowStyle") {}
    std::vector<MonitorWorkArea> monitors() override {
        PhysicalCoordinates physical;
        std::vector<MonitorWorkArea> result;
        EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&result));
        return result;
    }
    bool set(const MonitorWorkArea &monitor, WorkRect desired) override {
        if (!isInteractiveSession()) return false;
        PhysicalCoordinates physical;
        // Check again immediately before the write. SPI has no compare-and-set;
        // do not knowingly overwrite a concurrent external appbar/topology edit.
        const auto current = monitors();
        const auto found = std::find_if(current.begin(), current.end(), [&](const auto &m) {
            return m.device == monitor.device && m.monitor == monitor.monitor && m.work == monitor.work;
        });
        if (found == current.end() || !Area::within(desired, monitor.monitor)) return false;
        auto area = rectangle(desired);
        if (!SystemParametersInfoW(SPI_SETWORKAREA, 0, &area, 0)) return false;
        // No registry persistence and no synchronous calls into foreign GUI
        // threads: notifications cannot hang guardian shutdown.
        PostMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
        return true;
    }
    void prepare(const MonitorWorkArea &monitor, WorkRect after, bool restoring) override {
        if (!isInteractiveSession()) return;
        if (restoring || monitor.work == after) return;
        PhysicalCoordinates physical;
        ReflowScan scan{monitor.monitor, monitor.work, after, reflowMarker_, false, true};
        EnumWindows(reflowWindow, reinterpret_cast<LPARAM>(&scan));
    }
    void reflow(const MonitorWorkArea &monitor, WorkRect before, WorkRect after, bool restoring) override {
        if (!isInteractiveSession()) return;
        if (before == after) return;
        PhysicalCoordinates physical;
        ReflowScan scan{monitor.monitor, before, after, reflowMarker_, restoring};
        EnumWindows(reflowWindow, reinterpret_cast<LPARAM>(&scan));
    }
    void finishRestore() override { EnumWindows(clearWindowMarker, reinterpret_cast<LPARAM>(reflowMarker_.c_str())); }
private:
    std::wstring reflowMarker_;
};
}

struct WorkAreaJournal::Data {
    volatile LONG magic = 0;
    volatile LONG count = 0;
    Record records[maxRecords];
};
WorkAreaJournal::WorkAreaJournal() = default;
WorkAreaJournal::~WorkAreaJournal() {
    if (data_) UnmapViewOfFile(data_);
    if (mapping_) CloseHandle(mapping_);
}
bool WorkAreaJournal::create(const std::wstring &marker) {
    if (mapping_) return false;
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Data), mappingName(marker).c_str());
    if (!mapping_) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mapping_); mapping_ = nullptr; return false; }
    data_ = static_cast<Data *>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Data)));
    if (!data_) return false;
    InterlockedExchange(&data_->magic, journalMagic);
    return true;
}
bool WorkAreaJournal::open(const std::wstring &marker) {
    if (mapping_) return false;
    mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mappingName(marker).c_str());
    if (!mapping_) return false;
    data_ = static_cast<Data *>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Data)));
    return data_ && InterlockedCompareExchange(&data_->magic, 0, 0) == journalMagic;
}
bool WorkAreaJournal::release(const MonitorWorkArea &monitor, WorkRect taskbar, WorkAreaOperations &operations) {
    if (!data_) return false;
    const auto change = Area::taskbarStrip(monitor.monitor, monitor.work, taskbar);
    if (!change) return true;
    LONG count = InterlockedCompareExchange(&data_->count, 0, 0);
    if (count < 0 || count > maxRecords || monitor.device.size() >= deviceCapacity) return false;
    bool recorded = false;
    for (LONG index = count; index > 0; --index) {
        auto &record = data_->records[index - 1];
        const auto state = InterlockedCompareExchange(&record.state, 0, 0);
        if (state && record.device == monitor.device && record.monitor == monitor.monitor && record.change.side == change->side) {
            recorded = state == active && record.change.before == change->before && record.change.after == change->after;
            break; // Only the latest baseline on this edge can be reused.
        }
    }
    if (!recorded) {
        if (count == maxRecords) return false;
        auto &record = data_->records[count];
        std::copy(monitor.device.begin(), monitor.device.end(), record.device);
        record.device[monitor.device.size()] = 0;
        record.monitor = monitor.monitor; record.change = *change;
        // Immutable metadata is committed before SPI. If the guardian dies at
        // either side of the write, the parent can compare live edge and repair.
        InterlockedExchange(&record.state, active);
        InterlockedExchange(&data_->count, count + 1);
    }
    const auto desired = Area::withEdge(monitor.work, change->side, change->after);
    operations.prepare(monitor, desired, false);
    if (!operations.set(monitor, desired)) return false;
    operations.reflow(monitor, monitor.work, desired, false);
    return true;
}
bool WorkAreaJournal::restore(WorkAreaOperations &operations) {
    if (!operations.permitted()) return false;
    if (!data_) return false;
    const LONG count = InterlockedCompareExchange(&data_->count, 0, 0);
    if (count < 0 || count > maxRecords) return false;
    bool success = true;
    // A later Explorer baseline supersedes earlier records on the same edge.
    for (LONG index = count; index > 0; --index) {
        auto &record = data_->records[index - 1];
        if (InterlockedCompareExchange(&record.state, 0, 0) != active) continue;
        bool superseded = false;
        for (LONG later = index; later < count; ++later) {
            const auto &newer = data_->records[later];
            if (newer.state && newer.monitor == record.monitor && std::wcscmp(newer.device, record.device) == 0
                && newer.change.side == record.change.side) { superseded = true; break; }
        }
        if (superseded) { InterlockedExchange(&record.state, retired); continue; }
        const auto monitors = operations.monitors();
        const auto found = std::find_if(monitors.begin(), monitors.end(), [&](const auto &m) {
            return record.device == m.device && record.monitor == m.monitor;
        });
        if (found != monitors.end()) {
            if (!operations.permitted()) { success = false; continue; }
            const auto desired = Area::restore(found->monitor, found->work, record.change);
            if (desired) {
                if (!operations.set(*found, *desired)) { success = false; continue; }
                operations.reflow(*found, found->work, *desired, true);
            } else if (Area::edge(found->work, record.change.side) == record.change.before) {
                // Work area may already be restored by Explorer, or a guardian
                // may have died between restoring SPI and reflowing windows.
                operations.reflow(*found, Area::withEdge(found->work, record.change.side, record.change.after), found->work, true);
            }
        }
        if (!operations.permitted()) { success = false; continue; }
        // Different monitor topology or a different external managed edge is
        // deliberately left alone. Live unrelated edges always remain intact.
        InterlockedExchange(&record.state, retired);
    }
    if (success) operations.finishRestore();
    return success;
}
std::unique_ptr<WorkAreaOperations> systemWorkAreaOperations(const std::wstring &marker) {
    return std::make_unique<SystemOperations>(marker);
}
void setActiveWorkAreaMarker(const std::wstring &marker) { activeMarker = marker; }
bool isKnownWindowedMaximized(HWND window) {
    if (activeMarker.empty() || !IsZoomed(window)) return false;
    const auto style = GetPropW(window, (activeMarker + L".ReflowStyle").c_str());
    return style && reinterpret_cast<ULONG_PTR>(style) == static_cast<ULONG_PTR>(GetWindowLongPtrW(window, GWL_STYLE));
}
bool isInteractiveSession() {
    wchar_t *buffer = nullptr; DWORD bytes = 0;
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSSessionInfoEx, &buffer, &bytes)) return false;
    const auto *info = reinterpret_cast<const WTSINFOEXW *>(buffer);
    const bool unlocked = bytes >= sizeof(WTSINFOEXW) && info->Level == 1
        && info->Data.WTSInfoExLevel1.SessionState == WTSActive
        && info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_UNLOCK;
    WTSFreeMemory(buffer);
    if (!unlocked) return false;
    const auto desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desktop) return false;
    wchar_t name[256]{}; DWORD required = 0;
    const bool normal = GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &required)
        && _wcsicmp(name, L"Default") == 0;
    CloseDesktop(desktop);
    return normal;
}
}
