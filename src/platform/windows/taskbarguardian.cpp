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
};
thread_local Enumeration *activeHook = nullptr;
BOOL CALLBACK visit(HWND window, LPARAM parameter) {
    auto &scan = *reinterpret_cast<Enumeration *>(parameter);
    const auto marker = reinterpret_cast<HANDLE>(ownedValue);
    const bool owned = GetPropW(window, scan.marker.c_str()) == marker;
    if (scan.restore && !owned) return TRUE;
    if (!targetWindow(window, scan.policy, scan.restore)) return TRUE;
    if (scan.restore) {
        // The marker belongs to this HWND lifetime, so handle reuse cannot make
        // a new, initially hidden window appear during cleanup.
        if (ShowWindowAsync(window, SW_SHOWNOACTIVATE)) RemovePropW(window, scan.marker.c_str());
        else scan.success = false;
    } else if (IsWindowVisible(window)) {
        // An initially hidden taskbar is neither tagged nor restored by us.
        if (!owned && !SetPropW(window, scan.marker.c_str(), marker)) scan.success = false;
        else if (!ShowWindowAsync(window, SW_HIDE)) scan.success = false;
    }
    return TRUE;
}
void CALLBACK shown(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD, DWORD) {
    if (activeHook && window && object == OBJID_WINDOW && child == CHILDID_SELF)
        visit(window, reinterpret_cast<LPARAM>(activeHook));
}
bool enumerate(const std::wstring &marker, const Policy &policy, bool restore) {
    Enumeration scan{marker, policy, restore};
    return EnumWindows(visit, reinterpret_cast<LPARAM>(&scan)) && scan.success;
}
HANDLE argumentHandle(const char *text) {
    errno = 0; char *end = nullptr;
    const auto value = std::strtoull(text, &end, 10);
    if (errno || !end || *end || !value || value > std::numeric_limits<ULONG_PTR>::max()) return nullptr;
    return reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(value));
}
}

bool hideWindows(const std::wstring &marker, const Policy &policy) { return enumerate(marker, policy, false); }
bool restoreWindows(const std::wstring &marker, const Policy &policy) { return enumerate(marker, policy, true); }

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
    int result = 0;
    const HANDLE waits[]{parent, stop};
    Enumeration hookScan{marker, policy};
    activeHook = &hookScan;
    const auto hook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, shown,
        0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!hook) result = 4;
    if (!result && !hideWindows(marker, policy)) result = 4;
    if (!result) SetEvent(ready);
    auto nextSweep = GetTickCount64() + pollMs;
    while (!result) {
        const auto now = GetTickCount64();
        const auto timeout = now >= nextSweep ? DWORD(0) : static_cast<DWORD>(nextSweep - now);
        const auto wait = MsgWaitForMultipleObjects(2, waits, FALSE, timeout, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_OBJECT_0 + 1) break;
        if (wait == WAIT_OBJECT_0 + 2) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            if (!hookScan.success) { result = 4; break; }
        } else if (wait != WAIT_TIMEOUT) { result = 4; break; }
        if (GetTickCount64() >= nextSweep) {
            if (!hideWindows(marker, policy)) { result = 4; break; }
            nextSweep = GetTickCount64() + pollMs;
        }
    }
    if (hook) UnhookWinEvent(hook);
    activeHook = nullptr;
    if (!restoreWindows(marker, policy)) result = 4;
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
}
bool GuardianProcess::start(const std::wstring &executable, const std::wstring &marker,
    const std::vector<std::wstring> &testArguments) {
    if (process) return false;
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
        const HANDLE inherited[]{lifetime, stop, ready};
        if (UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                const_cast<HANDLE *>(inherited), sizeof(inherited), nullptr, nullptr)) {
            std::wstring command = L"\"" + executable + L"\" --taskbar-guardian "
                + std::to_wstring(reinterpret_cast<ULONG_PTR>(lifetime)) + L" "
                + std::to_wstring(reinterpret_cast<ULONG_PTR>(stop)) + L" "
                + std::to_wstring(reinterpret_cast<ULONG_PTR>(ready)) + L" " + marker;
            for (const auto &argument : testArguments) command += L" " + argument;
            STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
            PROCESS_INFORMATION child{};
            if (CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                    EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr, &startup.StartupInfo, &child)) {
                process = child.hProcess; CloseHandle(child.hThread); success = true;
            }
        }
    }
    if (initialized) DeleteProcThreadAttributeList(attributes);
    CloseHandle(lifetime);
    if (stop) SetHandleInformation(stop, HANDLE_FLAG_INHERIT, 0);
    if (ready) SetHandleInformation(ready, HANDLE_FLAG_INHERIT, 0);
    if (!success) close();
    return success;
}
}
