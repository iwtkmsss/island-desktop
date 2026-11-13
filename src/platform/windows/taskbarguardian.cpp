#include "taskbarguardian.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cwchar>
#include <limits>

namespace TaskbarNative {
namespace {
constexpr auto ownedValue = static_cast<ULONG_PTR>(1);
constexpr DWORD pollMs = 250;

std::wstring windowClass(HWND window) {
    wchar_t value[256]{};
    const auto length = GetClassNameW(window, value, 256);
    return std::wstring(value, length > 0 ? length : 0);
}
bool explorerProcess(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[32768]{}; DWORD length = 32768;
    const bool readable = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
    CloseHandle(process);
    wchar_t windows[32768]{};
    const auto size = GetWindowsDirectoryW(windows, 32768);
    if (!readable || !size || size >= 32768) return false;
    const auto expected = std::wstring(windows, size) + L"\\explorer.exe";
    return _wcsicmp(path, expected.c_str()) == 0;
}
bool targetWindow(HWND window, const Policy &policy, bool restoring) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    const auto cls = windowClass(window);
    if (!policy.testClass.empty()) return pid == policy.testOwner && cls == policy.testClass;
    if (pid == GetCurrentProcessId() || (cls != L"Shell_TrayWnd" && cls != L"Shell_SecondaryTrayWnd")) return false;
    if (!restoring) {
        DWORD shellPid = 0;
        GetWindowThreadProcessId(GetShellWindow(), &shellPid);
        if (!shellPid || pid != shellPid) return false;
    }
    return explorerProcess(pid);
}
struct Enumeration {
    const std::wstring &marker;
    const Policy &policy;
    bool restore = false, success = true;
    WorkAreaJournal *journal = nullptr;
    WorkAreaOperations *operations = nullptr;
    IslandDesktop::TaskbarSessionControl *session = nullptr;
    bool suspended = false;
};
thread_local Enumeration *activeHook = nullptr;
BOOL CALLBACK visit(HWND window, LPARAM parameter) {
    auto &scan = *reinterpret_cast<Enumeration *>(parameter);
    if (scan.suspended) return TRUE;
    const auto marker = reinterpret_cast<HANDLE>(ownedValue);
    const bool owned = GetPropW(window, scan.marker.c_str()) == marker;
    if (scan.restore && !owned) return TRUE;
    if (!targetWindow(window, scan.policy, scan.restore)) return TRUE;
    if (scan.policy.testClass.empty() && !isInteractiveSession()) return TRUE;
    if (scan.restore) {
        // The marker belongs to this HWND lifetime, so handle reuse cannot make
        // a new, initially hidden window appear during cleanup.
        if (ShowWindowAsync(window, SW_SHOWNOACTIVATE)) RemovePropW(window, scan.marker.c_str());
        else scan.success = false;
    } else {
        if (IsWindowVisible(window)) {
            if (scan.session && !scan.session->allowHide(reinterpret_cast<ULONG_PTR>(window), GetTickCount64())) return TRUE;
            // An initially hidden taskbar is neither tagged nor restored by us.
            if (!owned && !SetPropW(window, scan.marker.c_str(), marker)) scan.success = false;
            else {
                if (!ShowWindowAsync(window, SW_HIDE)) scan.success = false;
            }
        }
        // Visibility and reservation have different owners. Explorer can start
        // with a taskbar already hidden while its appbar strip is still reserved.
        // Release that proven strip too, but never tag/show the initially hidden
        // window: the journal owns only the work-area edge we actually change.
        if (scan.journal && scan.operations) {
            RECT bar{}; MONITORINFO info{}; info.cbSize = sizeof(info);
            if (GetWindowRect(window, &bar)
                && GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONULL), &info)) {
                const auto monitors = scan.operations->monitors();
                for (const auto &monitor : monitors) {
                    if (monitor.monitor == WorkRect{info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom}
                        && !scan.journal->release(monitor, {bar.left, bar.top, bar.right, bar.bottom}, *scan.operations)) scan.success = false;
                }
            }
        }
    }
    return TRUE;
}
void CALLBACK shown(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD, DWORD) {
    if (activeHook && window && object == OBJID_WINDOW && child == CHILDID_SELF)
        visit(window, reinterpret_cast<LPARAM>(activeHook));
}
bool enumerate(const std::wstring &marker, const Policy &policy, bool restore,
    WorkAreaJournal *journal = nullptr, WorkAreaOperations *operations = nullptr,
    IslandDesktop::TaskbarSessionControl *session = nullptr) {
    const auto previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Enumeration scan{marker, policy, restore, true, journal, operations, session};
    const bool result = EnumWindows(visit, reinterpret_cast<LPARAM>(&scan)) && scan.success;
    if (previous) SetThreadDpiAwarenessContext(previous);
    return result;
}
HANDLE argumentHandle(const char *text) {
    errno = 0; char *end = nullptr;
    const auto value = std::strtoull(text, &end, 10);
    if (errno || !end || *end || !value || value > std::numeric_limits<ULONG_PTR>::max()) return nullptr;
    return reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(value));
}
}

bool hideWindows(const std::wstring &marker, const Policy &policy, WorkAreaJournal *journal, WorkAreaOperations *operations,
    IslandDesktop::TaskbarSessionControl *session) {
    return enumerate(marker, policy, false, journal, operations, session);
}
bool restoreWindows(const std::wstring &marker, const Policy &policy) {
    if (policy.testClass.empty() && !isInteractiveSession()) return false;
    return enumerate(marker, policy, true);
}
ControlSnapshot controlSnapshot(const Policy &policy, WorkAreaOperations *operations) {
    struct Bars { const Policy &policy; std::vector<std::pair<ULONG_PTR, RECT>> values; } bars{policy, {}};
    const auto previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    EnumWindows([](HWND window, LPARAM p) -> BOOL {
        auto &bars = *reinterpret_cast<Bars *>(p); RECT rect{};
        if (targetWindow(window, bars.policy, false) && GetWindowRect(window, &rect))
            bars.values.push_back({reinterpret_cast<ULONG_PTR>(window), rect});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&bars));
    if (previous) SetThreadDpiAwarenessContext(previous);
    std::sort(bars.values.begin(), bars.values.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    ControlSnapshot result{1469598103934665603ull, false};
    const auto mix = [&](std::uint64_t value) { result.geometry = (result.geometry ^ value) * 1099511628211ull; };
    for (const auto &[window, rect] : bars.values) { mix(window); mix(rect.left); mix(rect.top); mix(rect.right); mix(rect.bottom); }
    if (operations) {
        auto monitors = operations->monitors();
        std::sort(monitors.begin(), monitors.end(), [](const auto &a, const auto &b) { return a.device < b.device; });
        for (const auto &monitor : monitors) {
            for (auto c : monitor.device) mix(c);
            for (const auto rect : {monitor.monitor, monitor.work}) { mix(rect.left); mix(rect.top); mix(rect.right); mix(rect.bottom); }
            for (const auto &[window, rect] : bars.values) {
                (void)window;
                if (IslandDesktop::WorkArea::taskbarStrip(monitor.monitor, monitor.work, {rect.left,rect.top,rect.right,rect.bottom})) result.reservation = true;
            }
        }
    }
    return result;
}

int guardianEntry(int argc, char **argv, const Policy &policy) {
    if (argc < 2 || std::string(argv[1]) != "--taskbar-guardian") return -1;
    if (argc < 6) return 2;
    const HANDLE parent = argumentHandle(argv[2]), stop = argumentHandle(argv[3]), ready = argumentHandle(argv[4]);
    const std::string rawMarker(argv[5]);
    if (rawMarker.size() > 180 || rawMarker.rfind("IslandDesktop.HiddenTaskbar.", 0) != 0
        || rawMarker.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") != std::string::npos) return 2;
    const std::wstring marker(rawMarker.begin(), rawMarker.end());
    DWORD flags = 0;
    if (!parent || !stop || !ready || !GetProcessId(parent)
        || !GetHandleInformation(stop, &flags) || !GetHandleInformation(ready, &flags)
        || WaitForSingleObject(parent, 0) != WAIT_TIMEOUT) return 2;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, policy.mutexName.c_str());
    if (!mutex) return 3;
    const auto acquired = WaitForSingleObject(mutex, 0);
    if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) { CloseHandle(mutex); return 3; }
    WorkAreaJournal journal;
    if (!journal.open(marker)) { ReleaseMutex(mutex); CloseHandle(mutex); return 4; }
    // Private-window tests must never instantiate the real SPI adapter.
    auto operations = policy.testClass.empty() ? systemWorkAreaOperations(marker) : nullptr;
    int result = 0;
    bool recovery = false;
    for (int index = 6; index < argc; ++index) {
        if (std::string(argv[index]) == "--restore-only") {
            recovery = true;
            if (index + 1 < argc) {
                const auto inheritedMapping = argumentHandle(argv[index + 1]);
                if (inheritedMapping) CloseHandle(inheritedMapping); // journal.open already holds it.
            }
            break;
        }
    }
    IslandDesktop::TaskbarSessionControl session;
    const HANDLE waits[]{parent, stop};
    Enumeration hookScan{marker, policy};
    hookScan.session = &session;
    hookScan.suspended = true;
    activeHook = &hookScan;
    auto hook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, shown,
        0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!hook) { result = 4; recovery = true; }
    // Ownership acknowledgement is independent of lock/debounce. The parent
    // may drop its mapping only after this process has opened recovery state.
    SetEvent(ready);
    auto nextSweep = GetTickCount64() + pollMs;
    while (true) {
        const auto now = GetTickCount64();
        const bool interactive = !policy.testClass.empty() || isInteractiveSession();
        const auto snapshot = controlSnapshot(policy, operations.get());
        session.observe(interactive, snapshot.geometry, now);
        if (WaitForSingleObject(parent, 0) == WAIT_OBJECT_0 || WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) recovery = true;
        if (session.failed()) { result = 5; recovery = true; }
        hookScan.suspended = recovery || !session.active(now);
        if (recovery && interactive && session.cleanupReady(now)) {
            if (hook) { UnhookWinEvent(hook); hook = nullptr; activeHook = nullptr; }
            const bool windows = restoreWindows(marker, policy);
            const bool area = !operations || journal.restore(*operations);
            if (windows && area && (!policy.testClass.empty() || isInteractiveSession())) break;
            if (isInteractiveSession()) result = result ? result : 4;
        } else if (!recovery && session.active(now)) {
            const bool work = snapshot.reservation && session.allowWork(snapshot.geometry, now);
            if (!hideWindows(marker, policy, work ? &journal : nullptr, work ? operations.get() : nullptr, &session)
                && (!policy.testClass.empty() || isInteractiveSession())) { result = 4; recovery = true; }
        }
        const auto timeout = now >= nextSweep ? DWORD(0) : static_cast<DWORD>(nextSweep - now);
        const DWORD count = recovery ? 0 : 2;
        const auto wait = MsgWaitForMultipleObjects(count, count ? waits : nullptr, FALSE, timeout, QS_ALLINPUT);
        if (count && (wait == WAIT_OBJECT_0 || wait == WAIT_OBJECT_0 + 1)) { recovery = true; continue; }
        if (wait == WAIT_OBJECT_0 + count) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            if (!hookScan.success) { result = 4; recovery = true; }
        } else if (wait != WAIT_TIMEOUT) { result = 4; recovery = true; }
        if (GetTickCount64() >= nextSweep) {
            nextSweep = GetTickCount64() + pollMs;
        }
    }
    if (hook) UnhookWinEvent(hook);
    activeHook = nullptr;
    ReleaseMutex(mutex); CloseHandle(mutex);
    CloseHandle(parent); CloseHandle(stop); CloseHandle(ready);
    return result;
}

GuardianProcess::~GuardianProcess() { close(); }
void GuardianProcess::close() {
    if (process) CloseHandle(process);
    if (stop) CloseHandle(stop);
    if (ready) CloseHandle(ready);
    process = stop = ready = nullptr;
    workArea.reset();
}
bool GuardianProcess::start(const std::wstring &executable, const std::wstring &marker,
    const std::vector<std::wstring> &testArguments, bool restoreOnly) {
    if (process) return false;
    workArea = std::make_unique<WorkAreaJournal>();
    if (!(restoreOnly ? workArea->open(marker) : workArea->create(marker))) { close(); return false; }
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE lifetime = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &lifetime, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0)) return false;
    stop = CreateEventW(&security, TRUE, FALSE, nullptr);
    ready = CreateEventW(&security, TRUE, FALSE, nullptr);
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    bool initialized = false, success = false;
    if (stop && ready && InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) {
        initialized = true;
        const HANDLE inherited[]{lifetime, stop, ready, workArea->nativeHandle()};
        if (restoreOnly) SetHandleInformation(inherited[3], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        if (UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                const_cast<HANDLE *>(inherited), sizeof(HANDLE) * (restoreOnly ? 4 : 3), nullptr, nullptr)) {
            std::wstring command = L"\"" + executable + L"\" --taskbar-guardian "
                + std::to_wstring(reinterpret_cast<ULONG_PTR>(lifetime)) + L" "
                + std::to_wstring(reinterpret_cast<ULONG_PTR>(stop)) + L" "
                + std::to_wstring(reinterpret_cast<ULONG_PTR>(ready)) + L" " + marker;
            for (const auto &argument : testArguments) command += L" " + argument;
            if (restoreOnly) command += L" --restore-only " + std::to_wstring(reinterpret_cast<ULONG_PTR>(inherited[3]));
            STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
            PROCESS_INFORMATION child{};
            if (CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                    EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr, &startup.StartupInfo, &child)) {
                process = child.hProcess; CloseHandle(child.hThread); success = true;
            }
        }
    }
    if (initialized) DeleteProcThreadAttributeList(attributes);
    if (restoreOnly && workArea) SetHandleInformation(workArea->nativeHandle(), HANDLE_FLAG_INHERIT, 0);
    CloseHandle(lifetime);
    if (stop) SetHandleInformation(stop, HANDLE_FLAG_INHERIT, 0);
    if (ready) SetHandleInformation(ready, HANDLE_FLAG_INHERIT, 0);
    if (!success) close();
    return success;
}
}
