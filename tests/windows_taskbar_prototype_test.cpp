#include "protocol.h"
#include <dwmapi.h>
#include <iostream>
#include <string>
#include <stdexcept>

namespace {
volatile LONG called[3]{};
__declspec(noinline) void one(void*, int a, int b) { InterlockedIncrement(&called[0]); if (a == 999) InterlockedAdd(&called[0], b); }
__declspec(noinline) void two(void*, int a, int b) { InterlockedIncrement(&called[1]); if (b == 999) InterlockedAdd(&called[1], a); }
__declspec(noinline) bool three(void*, unsigned long long a, bool b) { InterlockedIncrement(&called[2]); return b || a == 999; }
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void pump() { MSG m{}; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
template<class F> bool until(F predicate) {
    const auto end = GetTickCount64() + 8000;
    do { pump(); if (predicate()) return true; Sleep(10); } while (GetTickCount64() < end);
    return false;
}
DWORD cloak(HWND window) {
    DWORD value{}; check(SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &value, sizeof(value))), "read private cloak"); return value;
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--owner") { Sleep(30000); return 0; }
    try {
        wchar_t path[MAX_PATH]{}; GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring command = L"\"" + std::wstring(path) + L"\" --owner";
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION child{};
        check(CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child), "private owner process");
        CloseHandle(child.hThread);
        WNDCLASSW cls{}; cls.lpfnWndProc = DefWindowProcW; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"IslandTaskbarPrototypePrivate";
        check(RegisterClassW(&cls) != 0, "private window class");
        HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName, L"", WS_POPUP,
                                     -30000, -30000, 16, 16, nullptr, nullptr, cls.hInstance, nullptr);
        HWND initiallyCloaked = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName, L"", WS_POPUP,
                                     -30000, -30000, 16, 16, nullptr, nullptr, cls.hInstance, nullptr);
        check(window && initiallyCloaked, "private windows");
        ShowWindow(window, SW_SHOWNOACTIVATE); ShowWindow(initiallyCloaked, SW_SHOWNOACTIVATE);
        BOOL yes = TRUE; check(SUCCEEDED(DwmSetWindowAttribute(initiallyCloaked, DWMWA_CLOAK, &yes, sizeof(yes))), "initial private cloak");
        HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(prototype::Shared), nullptr);
        auto* shared = static_cast<prototype::Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(prototype::Shared)));
        check(shared != nullptr, "private mapping");
        shared->config = prototype::Config{};
        auto& config = shared->config;
        config.ownerPid = child.dwProcessId; config.targetPid = GetCurrentProcessId(); config.privateTest = 1;
        FILETIME exit{}, kernel{}, user{}; check(GetProcessTimes(child.hProcess, &config.ownerCreation, &exit, &kernel, &user), "owner creation");
        auto base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
        void* functions[] = {reinterpret_cast<void*>(&one), reinterpret_cast<void*>(&two), reinterpret_cast<void*>(&three)};
        for (unsigned i = 0; i < 3; ++i) {
            auto& image = config.images[i]; wcsncpy_s(image.path, path, _TRUNCATE);
            image.timestamp = nt->FileHeader.TimeDateStamp; image.size = nt->OptionalHeader.SizeOfImage;
            image.rva = DWORD(reinterpret_cast<BYTE*>(functions[i]) - base);
        }
        config.windowCount = 2; config.windows[0] = reinterpret_cast<UINT_PTR>(window); config.windows[1] = reinterpret_cast<UINT_PTR>(initiallyCloaked);
        std::wstring dll(path); dll.resize(dll.find_last_of(L"\\/") + 1); dll += L"IslandTaskbarPrototype.dll";
        HMODULE module = LoadLibraryW(dll.c_str()); check(module != nullptr, "load private DLL");
        auto start = reinterpret_cast<prototype::Start>(GetProcAddress(module, "StartSession")); check(start != nullptr, "private export");
        HANDLE mappingCopy{}, ownerCopy{};
        check(DuplicateHandle(GetCurrentProcess(), mapping, GetCurrentProcess(), &mappingCopy, 0, FALSE, DUPLICATE_SAME_ACCESS), "mapping duplicate");
        check(DuplicateHandle(GetCurrentProcess(), child.hProcess, GetCurrentProcess(), &ownerCopy,
                              SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0), "owner duplicate");
        prototype::StartArgs args{reinterpret_cast<UINT_PTR>(mappingCopy), reinterpret_cast<UINT_PTR>(ownerCopy)};
        one(nullptr, 0, 0); two(nullptr, 0, 0); check(three(nullptr, 0, true), "original bool result");
        check(start(&args) == ERROR_SUCCESS, "private StartSession validation");
        check(until([&] { return prototype::read(&shared->phase) >= prototype::ready; }), "ready timeout");
        check(prototype::read(&shared->phase) == prototype::ready, "hook installation failed");
        one(nullptr, 0, 0); two(nullptr, 0, 0); check(!three(nullptr, 0, true), "bool hook pre-call gate");
        check(called[0] == 1 && called[1] == 1 && called[2] == 1, "original invoked while blocked");
        check(until([&] { return cloak(window) != 0; }), "private window not cloaked");
        check(IsWindowVisible(window), "original visibility must stay unchanged");
        check(TerminateProcess(child.hProcess, 0), "terminate private owner"); CloseHandle(child.hProcess);
        check(until([&] { return prototype::read(&shared->phase) >= prototype::restored; }), "owner death restore timeout");
        check(prototype::read(&shared->phase) == prototype::restored && shared->error == 0, "owner death restoration failed");
        check(until([&] { return cloak(window) == 0; }), "own cloak not restored");
        check(cloak(initiallyCloaked) != 0, "pre-existing cloak incorrectly removed");
        check(shared->restoredWindows == 1, "restoration count");
        one(nullptr, 0, 0); two(nullptr, 0, 0); check(three(nullptr, 0, true), "original bool restore");
        check(called[0] == 2 && called[1] == 2 && called[2] == 2, "original calls not restored");
        check(shared->blocked[0] == 1 && shared->blocked[1] == 1 && shared->blocked[2] == 1, "blocked diagnostic counts");
        DestroyWindow(window); DestroyWindow(initiallyCloaked); UnmapViewOfFile(shared); CloseHandle(mapping);
        // DLL intentionally remains pinned. No real Explorer/taskbar window touched.
        std::cout << "PASS: 3 pre-call gates, private cloak, owner death, original callbacks and prior cloak restored\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << " Win32=" << GetLastError() << "\n"; return 1; }
}
