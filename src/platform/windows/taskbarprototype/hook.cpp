#include "protocol.h"
#include "MinHook.h"
#include <array>
#include <dwmapi.h>
#include <string>

namespace {
using Unhide = void (*)(void*, int, int); // Windows x64 ABI; exact PDB signatures required by host.
Unhide originals[2]{};
void* targets[3]{};
using Expanded = bool (*)(void*, unsigned long long, bool);
Expanded expandedOriginal{};
volatile LONG active = 0, claimed = 0, counts[3]{};
constexpr wchar_t marker[] = L"IslandTaskbarPrototype.CloakOwner.v1";
void first(void* self, int flags, int request) {
    if (prototype::read(&active)) { InterlockedIncrement(&counts[0]); return; }
    originals[0](self, flags, request);
}
void second(void* self, int flags, int request) {
    if (prototype::read(&active)) { InterlockedIncrement(&counts[1]); return; }
    originals[1](self, flags, request);
}
bool expanded(void* self, unsigned long long value, bool flag) {
    if (prototype::read(&active)) { InterlockedIncrement(&counts[2]); return false; }
    return expandedOriginal(self, value, flag);
}
std::wstring path(HANDLE process) {
    wchar_t buffer[32768]; DWORD length = DWORD(std::size(buffer));
    return QueryFullProcessImageNameW(process, 0, buffer, &length) ? std::wstring(buffer, length) : L"";
}
bool sameUser(HANDLE process) {
    HANDLE a{}, b{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &a)) return false;
    if (!OpenProcessToken(process, TOKEN_QUERY, &b)) { CloseHandle(a); return false; }
    BYTE ua[1024]{}, ub[1024]{}; DWORD size{};
    const bool ok = GetTokenInformation(a, TokenUser, ua, sizeof(ua), &size)
        && GetTokenInformation(b, TokenUser, ub, sizeof(ub), &size)
        && EqualSid(reinterpret_cast<TOKEN_USER*>(ua)->User.Sid, reinterpret_cast<TOKEN_USER*>(ub)->User.Sid);
    CloseHandle(a); CloseHandle(b); return ok;
}
bool imageTarget(const prototype::Image& image, void** target) {
    if (!wmemchr(image.path, 0, MAX_PATH)) return false;
    HMODULE module = GetModuleHandleW(image.path);
    if (!module) return false;
    auto base = reinterpret_cast<BYTE*>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096) return false;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
        || nt->FileHeader.TimeDateStamp != image.timestamp || nt->OptionalHeader.SizeOfImage != image.size
        || image.rva >= image.size) return false;
    auto section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& s = section[i];
        if (image.rva >= s.VirtualAddress && image.rva - s.VirtualAddress < s.Misc.VirtualSize
            && (s.Characteristics & IMAGE_SCN_MEM_EXECUTE)) {
            MEMORY_BASIC_INFORMATION info{};
            if (VirtualQuery(base + image.rva, &info, sizeof(info)) != sizeof(info)
                || info.AllocationBase != module || info.State != MEM_COMMIT) return false;
            *target = base + image.rva; return true;
        }
    }
    return false;
}
struct Session { prototype::Shared* shared; HANDLE mapping, owner; prototype::Config config; };
bool windowAllowed(HWND window, const prototype::Config& c) {
    DWORD pid{}; GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId()) return false;
    wchar_t name[128]{}; GetClassNameW(window, name, 128);
    if (c.privateTest) return wcscmp(name, L"IslandTaskbarPrototypePrivate") == 0;
    return wcscmp(name, L"Shell_TrayWnd") == 0 || wcscmp(name, L"Shell_SecondaryTrayWnd") == 0;
}
DWORD WINAPI worker(void* data) {
    auto* s = static_cast<Session*>(data);
    auto* shared = s->shared;
    std::array<bool, 32> cloakedByUs{};
    bool installed = false;
    DWORD error = ERROR_INVALID_DATA;
    do {
        for (DWORD i = 0; i < s->config.windowCount; ++i)
            if (!windowAllowed(reinterpret_cast<HWND>(s->config.windows[i]), s->config)) goto finish;
        if (!imageTarget(s->config.images[0], &targets[0]) || !imageTarget(s->config.images[1], &targets[1])
            || !imageTarget(s->config.images[2], &targets[2])
            || targets[0] == targets[1] || targets[0] == targets[2] || targets[1] == targets[2]) break;
        if (WaitForSingleObject(s->owner, 0) != WAIT_TIMEOUT) { error = ERROR_PROCESS_ABORTED; break; }
        if (MH_Initialize() != MH_OK) { error = ERROR_INVALID_FUNCTION; break; }
        if (MH_CreateHook(targets[0], reinterpret_cast<void*>(&first), reinterpret_cast<void**>(&originals[0])) != MH_OK
            || MH_CreateHook(targets[1], reinterpret_cast<void*>(&second), reinterpret_cast<void**>(&originals[1])) != MH_OK
            || MH_CreateHook(targets[2], reinterpret_cast<void*>(&expanded), reinterpret_cast<void**>(&expandedOriginal)) != MH_OK) {
            error = ERROR_INVALID_FUNCTION; break;
        }
        // No target writes occur until all trampolines exist. Pin before any callback can run.
        HMODULE pinned{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(&worker), &pinned)) { error = GetLastError(); break; }
        if (MH_QueueEnableHook(targets[0]) != MH_OK || MH_QueueEnableHook(targets[1]) != MH_OK
            || MH_QueueEnableHook(targets[2]) != MH_OK || MH_ApplyQueued() != MH_OK) { error = ERROR_INVALID_FUNCTION; break; }
        installed = true;
        if (WaitForSingleObject(s->owner, 0) != WAIT_TIMEOUT) { error = ERROR_PROCESS_ABORTED; break; }
        InterlockedExchange(&active, 1);
        for (DWORD i = 0; i < s->config.windowCount; ++i) {
            auto window = reinterpret_cast<HWND>(s->config.windows[i]);
            DWORD originalCloak{};
            if (FAILED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &originalCloak, sizeof(originalCloak)))) {
                error = ERROR_NOT_SUPPORTED; goto finish;
            }
            if (!originalCloak) {
                if (GetPropW(window, marker) || !SetPropW(window, marker, s)) { error = ERROR_ACCESS_DENIED; goto finish; }
                BOOL cloak = TRUE;
                if (FAILED(DwmSetWindowAttribute(window, DWMWA_CLOAK, &cloak, sizeof(cloak)))) {
                    RemovePropW(window, marker);
                    error = ERROR_NOT_SUPPORTED; goto finish;
                }
                cloakedByUs[i] = true;
            }
        }
        InterlockedExchange(&shared->phase, prototype::ready);
        DWORD ownerWait{};
        while ((ownerWait = WaitForSingleObject(s->owner, 50)) == WAIT_TIMEOUT && !prototype::read(&shared->stop)) {
            InterlockedExchange(&shared->blocked[0], prototype::read(&counts[0]));
            InterlockedExchange(&shared->blocked[1], prototype::read(&counts[1]));
            InterlockedExchange(&shared->blocked[2], prototype::read(&counts[2]));
        }
        InterlockedExchange(&shared->phase, prototype::stopping);
        InterlockedExchange(&active, 0); // all future callbacks pass through immediately
        error = ownerWait == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
    } while (false);
finish:
    InterlockedExchange(&active, 0);
    // Trampolines and DLL stay resident permanently: no unload/free race with callbacks.
    // Only our three hooks are disabled; never MH_ALL_HOOKS / global uninitialize.
    if (targets[0]) MH_DisableHook(targets[0]);
    if (targets[1]) MH_DisableHook(targets[1]);
    if (targets[2]) MH_DisableHook(targets[2]);
    if (installed) for (DWORD i = 0; i < s->config.windowCount; ++i) {
        auto window = reinterpret_cast<HWND>(s->config.windows[i]);
        if (cloakedByUs[i] && windowAllowed(window, s->config) && GetPropW(window, marker) == s) {
            BOOL cloak = FALSE;
            const auto result = DwmSetWindowAttribute(window, DWMWA_CLOAK, &cloak, sizeof(cloak));
            RemovePropW(window, marker);
            if (SUCCEEDED(result)) InterlockedIncrement(&shared->restoredWindows);
            else error = DWORD(result);
        }
    }
    InterlockedExchange(&shared->blocked[0], prototype::read(&counts[0]));
    InterlockedExchange(&shared->blocked[1], prototype::read(&counts[1]));
    InterlockedExchange(&shared->blocked[2], prototype::read(&counts[2]));
    InterlockedExchange(&shared->error, error);
    InterlockedExchange(&shared->phase, error ? prototype::failed : prototype::restored);
    UnmapViewOfFile(shared); CloseHandle(s->mapping); CloseHandle(s->owner); delete s;
    return error;
}
}
extern "C" __declspec(dllexport) DWORD WINAPI StartSession(void* argument) {
    if (!argument) return ERROR_INVALID_PARAMETER;
    const auto args = *static_cast<prototype::StartArgs*>(argument);
    HANDLE mapping = reinterpret_cast<HANDLE>(args.mapping), owner = reinterpret_cast<HANDLE>(args.owner);
    auto* shared = static_cast<prototype::Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(prototype::Shared)));
    if (!shared) { CloseHandle(mapping); CloseHandle(owner); return ERROR_INVALID_HANDLE; }
    const auto config = shared->config;
    FILETIME creation{}, exit{}, kernel{}, user{};
    wchar_t ownDll[MAX_PATH]{}; HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      reinterpret_cast<LPCWSTR>(&StartSession), &module);
    GetModuleFileNameW(module, ownDll, MAX_PATH);
    std::wstring expected(ownDll); expected.resize(expected.find_last_of(L"\\/") + 1);
    expected += config.privateTest ? L"windows_taskbar_prototype_test.exe" : L"taskbar-prototype.exe";
    DWORD ownerSession{}, targetSession{};
    bool valid = config.signature == prototype::magic && config.revision == prototype::version
        && config.bytes == sizeof(config) && config.targetPid == GetCurrentProcessId()
        && config.ownerPid == GetProcessId(owner) && config.windowCount > 0 && config.windowCount <= 32
        && GetProcessTimes(owner, &creation, &exit, &kernel, &user) && prototype::equalTime(creation, config.ownerCreation)
        && WaitForSingleObject(owner, 0) == WAIT_TIMEOUT && sameUser(owner)
        && ProcessIdToSessionId(config.ownerPid, &ownerSession) && ProcessIdToSessionId(config.targetPid, &targetSession)
        && ownerSession == targetSession && _wcsicmp(path(owner).c_str(), expected.c_str()) == 0;
    if (config.privateTest) {
        const auto current = path(GetCurrentProcess());
        valid = valid && _wcsicmp(current.c_str(), expected.c_str()) == 0;
    } else {
        wchar_t system[MAX_PATH]{}; GetWindowsDirectoryW(system, MAX_PATH);
        valid = valid && _wcsicmp(path(GetCurrentProcess()).c_str(), (std::wstring(system) + L"\\explorer.exe").c_str()) == 0;
        for (int i = 0; i < 3; ++i) {
            const auto& image = config.images[i];
            std::wstring allowed(system); allowed += i < 2 ? L"\\System32\\taskbar.dll" : L"\\SystemApps\\MicrosoftWindows.Client.Core_cw5n1h2txyewy\\Taskbar.View.dll";
            valid = valid && wmemchr(image.path, 0, MAX_PATH) && _wcsicmp(image.path, allowed.c_str()) == 0;
        }
    }
    DWORD result = ERROR_ACCESS_DENIED;
    if (valid && InterlockedCompareExchange(&claimed, 1, 0) == 0) {
        auto* session = new Session{shared, mapping, owner, config};
        InterlockedExchange(&shared->phase, prototype::starting);
        HANDLE thread = CreateThread(nullptr, 0, worker, session, 0, nullptr);
        if (thread) { CloseHandle(thread); return ERROR_SUCCESS; }
        result = GetLastError(); delete session;
    }
    InterlockedExchange(&shared->error, result); InterlockedExchange(&shared->phase, prototype::failed);
    UnmapViewOfFile(shared); CloseHandle(mapping); CloseHandle(owner); return result;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}

