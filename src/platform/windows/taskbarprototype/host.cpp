#include "protocol.h"
#include <dbghelp.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
volatile LONG stopping{};
BOOL WINAPI console(DWORD) { InterlockedExchange(&stopping, 1); return TRUE; }
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    operator HANDLE() const { return value; }
};
[[noreturn]] void fail(const char* message) {
    throw std::runtime_error(std::string(message) + " (Win32=" + std::to_string(GetLastError()) + ")");
}
std::wstring processPath(HANDLE process) {
    wchar_t buffer[32768]; DWORD size = DWORD(std::size(buffer));
    return QueryFullProcessImageNameW(process, 0, buffer, &size) ? std::wstring(buffer, size) : L"";
}
std::wstring ownPath() { wchar_t buffer[MAX_PATH]{}; GetModuleFileNameW(nullptr, buffer, MAX_PATH); return buffer; }
struct Module { std::wstring path, name; ULONG_PTR base; DWORD size; };
std::vector<Module> modules(DWORD pid) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (snapshot.value == INVALID_HANDLE_VALUE) fail("module snapshot");
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    std::vector<Module> result;
    if (Module32FirstW(snapshot, &entry)) do {
        result.push_back({entry.szExePath, entry.szModule, reinterpret_cast<ULONG_PTR>(entry.modBaseAddr), entry.modBaseSize});
    } while (Module32NextW(snapshot, &entry));
    return result;
}
const Module& moduleNamed(const std::vector<Module>& list, const wchar_t* name) {
    const Module* found{};
    for (auto& m : list) if (_wcsicmp(m.name.c_str(), name) == 0) {
        if (found) fail("ambiguous module"); found = &m;
    }
    if (!found) fail("required module absent");
    return *found;
}
struct Windows { DWORD pid{}; std::vector<HWND> bars; };
BOOL CALLBACK collect(HWND window, LPARAM parameter) {
    auto& result = *reinterpret_cast<Windows*>(parameter);
    wchar_t cls[128]{}; GetClassNameW(window, cls, 128);
    if (wcscmp(cls, L"Shell_TrayWnd") && wcscmp(cls, L"Shell_SecondaryTrayWnd")) return TRUE;
    DWORD pid{}; GetWindowThreadProcessId(window, &pid);
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    wchar_t root[MAX_PATH]{}; GetWindowsDirectoryW(root, MAX_PATH);
    if (_wcsicmp(processPath(process).c_str(), (std::wstring(root) + L"\\explorer.exe").c_str())) return TRUE;
    DWORD currentSession{}, targetSession{};
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSession) || !ProcessIdToSessionId(pid, &targetSession)
        || currentSession != targetSession) return TRUE;
    if (result.pid && result.pid != pid) { result.pid = MAXDWORD; return FALSE; }
    result.pid = pid; result.bars.push_back(window); return TRUE;
}
Windows taskbars() {
    Windows result; EnumWindows(collect, reinterpret_cast<LPARAM>(&result));
    if (result.pid == MAXDWORD || result.bars.size() > 32) fail("ambiguous taskbar owner/window count");
    return result;
}
bool guardianConflict() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE) return true;
    PROCESSENTRY32W e{}; e.dwSize = sizeof(e);
    if (!Process32FirstW(snapshot, &e)) return true;
    do { if (_wcsicmp(e.szExeFile, L"IslandDesktop.exe") == 0) return true; } while (Process32NextW(snapshot, &e));
    return false;
}
struct Rsds { DWORD signature; GUID guid; DWORD age; };
struct FileImage {
    HMODULE mapped{};
    BYTE* base{};
    IMAGE_NT_HEADERS64* nt{};
    Rsds pdb{};
    explicit FileImage(const std::wstring& path) {
        mapped = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (!mapped) fail("read-only PE mapping");
        base = reinterpret_cast<BYTE*>(reinterpret_cast<ULONG_PTR>(mapped) & ~ULONG_PTR(3));
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096) fail("PE DOS header");
        nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
            || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) fail("unsupported PE architecture");
        const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
        if (directory.VirtualAddress > nt->OptionalHeader.SizeOfImage || directory.Size > nt->OptionalHeader.SizeOfImage - directory.VirtualAddress) fail("PE debug bounds");
        auto debug = reinterpret_cast<IMAGE_DEBUG_DIRECTORY*>(base + directory.VirtualAddress);
        bool found = false;
        for (DWORD i = 0; i < directory.Size / sizeof(*debug); ++i) {
            if (debug[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW) continue;
            if (debug[i].SizeOfData < sizeof(Rsds) || debug[i].AddressOfRawData > nt->OptionalHeader.SizeOfImage
                || debug[i].SizeOfData > nt->OptionalHeader.SizeOfImage - debug[i].AddressOfRawData) fail("RSDS bounds");
            memcpy(&pdb, base + debug[i].AddressOfRawData, sizeof(pdb));
            if (pdb.signature != 0x53445352 || found) fail("unsupported or ambiguous PDB identity");
            found = true;
        }
        if (!found) fail("missing RSDS identity");
    }
    ~FileImage() { if (mapped) FreeLibrary(mapped); }
    FileImage(const FileImage&) = delete;
    bool executable(DWORD rva) const {
        auto section = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
            if (rva >= section[i].VirtualAddress && rva - section[i].VirtualAddress < section[i].Misc.VirtualSize
                && (section[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) return true;
        return false;
    }
};
template<class T> T remoteRead(HANDLE process, ULONG_PTR address) {
    T value{}; SIZE_T count{};
    if (!ReadProcessMemory(process, reinterpret_cast<void*>(address), &value, sizeof(value), &count) || count != sizeof(value)) fail("remote image read");
    return value;
}
void identity(HANDLE process, const Module& module, const FileImage& image) {
    auto dos = remoteRead<IMAGE_DOS_HEADER>(process, module.base);
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 4096) fail("remote DOS identity");
    auto nt = remoteRead<IMAGE_NT_HEADERS64>(process, module.base + dos.e_lfanew);
    if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
        || nt.FileHeader.TimeDateStamp != image.nt->FileHeader.TimeDateStamp
        || nt.OptionalHeader.SizeOfImage != image.nt->OptionalHeader.SizeOfImage
        || module.size != image.nt->OptionalHeader.SizeOfImage) fail("running/file PE identity mismatch");
    const auto directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (directory.VirtualAddress >= module.size || directory.Size > module.size - directory.VirtualAddress
        || directory.Size > 65536) fail("remote debug bounds");
    bool found = false;
    for (DWORD i = 0; i < directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY); ++i) {
        auto debug = remoteRead<IMAGE_DEBUG_DIRECTORY>(process, module.base + directory.VirtualAddress + i * sizeof(IMAGE_DEBUG_DIRECTORY));
        if (debug.Type != IMAGE_DEBUG_TYPE_CODEVIEW) continue;
        if (debug.SizeOfData < sizeof(Rsds) || debug.AddressOfRawData >= module.size || sizeof(Rsds) > module.size - debug.AddressOfRawData) fail("remote RSDS bounds");
        auto rsds = remoteRead<Rsds>(process, module.base + debug.AddressOfRawData);
        if (memcmp(&rsds, &image.pdb, sizeof(rsds))) fail("running/file RSDS mismatch");
        found = true;
    }
    if (!found) fail("running RSDS missing");
}
struct Search { const char* exact; DWORD64 address{}; unsigned matches{}, candidates{}; };
BOOL CALLBACK symbol(PSYMBOL_INFO info, ULONG, void* user) {
    auto& search = *static_cast<Search*>(user);
    char undecorated[4096]{};
    if (!UnDecorateSymbolName(info->Name, undecorated, sizeof(undecorated), UNDNAME_COMPLETE)) return TRUE;
    if (search.candidates++ < 20) std::cerr << "Symbol candidate: " << info->Name << " => " << undecorated << "\n";
    const std::string spelling(undecorated), expected(search.exact);
    // Some system DbgHelp versions append the redundant x64 member-this
    // pointer qualifier. Accept precisely that suffix; preserve all ABI types.
    if (spelling == expected || spelling == expected + " __ptr64") {
        search.address = info->Address; ++search.matches;
    }
    return TRUE;
}
void resolve(HANDLE process, const Module& module, const std::wstring& symbols,
             const char* mask, const char* exact, prototype::Image& result) {
    FileImage image(module.path); identity(process, module, image);
    HANDLE session = GetCurrentProcess();
    SymSetOptions(SYMOPT_EXACT_SYMBOLS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    if (!SymInitializeW(session, symbols.c_str(), FALSE)) fail("SymInitialize");
    const auto base = SymLoadModuleExW(session, nullptr, module.path.c_str(), nullptr, module.base, module.size, nullptr, 0);
    if (!base) { SymCleanup(session); fail("SymLoadModuleEx; preload matching PDB cache when symsrv.dll is unavailable"); }
    Search search{exact};
    const bool enumerated = SymEnumSymbols(session, base, mask, symbol, &search) != FALSE;
    IMAGEHLP_MODULE64 info{}; info.SizeOfStruct = sizeof(info);
    const bool matching = SymGetModuleInfo64(session, base, &info) && info.SymType == SymPdb
        && !info.PdbUnmatched && !info.DbgUnmatched && info.PdbAge == image.pdb.age
        && memcmp(&info.PdbSig70, &image.pdb.guid, sizeof(GUID)) == 0;
    SymCleanup(session);
    if (!enumerated || !matching || search.matches != 1 || search.address < base
        || search.address - base > MAXDWORD || !image.executable(DWORD(search.address - base))) {
        std::cerr << "Symbol diagnostic: enumerated=" << enumerated << " identity=" << matching
                  << " SymType=" << info.SymType << " PdbUnmatched=" << info.PdbUnmatched << " DbgUnmatched=" << info.DbgUnmatched
                  << " age(actual/expected)=" << info.PdbAge << "/" << image.pdb.age
                  << " guidEqual=" << (memcmp(&info.PdbSig70, &image.pdb.guid, sizeof(GUID)) == 0)
                  << " exactMatches=" << search.matches << " candidates=" << search.candidates << "\nExpected ABI: " << exact << "\n";
        fail("exact ABI/PDB symbol unavailable, ambiguous, or non-executable; unsupported build");
    }
    wcsncpy_s(result.path, module.path.c_str(), _TRUNCATE);
    result.timestamp = image.nt->FileHeader.TimeDateStamp; result.size = image.nt->OptionalHeader.SizeOfImage;
    result.rva = DWORD(search.address - base);
    std::wcout << L"Resolved " << module.name << L" RVA=0x" << std::hex << result.rva << std::dec
               << L" exact PDB GUID/age + PE identity verified\n";
}
prototype::Config probe(const Windows& bars, const std::wstring& symbols) {
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, bars.pid));
    if (!process.value) fail("OpenProcess read-only probe");
    const auto list = modules(bars.pid);
    for (const auto& m : list) if (_wcsicmp(m.name.c_str(), L"taskbar.dll") == 0 || _wcsicmp(m.name.c_str(), L"Taskbar.View.dll") == 0)
        std::wcout << L"Module " << m.path << L" base=0x" << std::hex << m.base << std::dec << L" size=" << m.size << L"\n";
    prototype::Config config; config.targetPid = bars.pid; config.ownerPid = GetCurrentProcessId();
    FILETIME exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &config.ownerCreation, &exit, &kernel, &user)) fail("owner identity");
    resolve(process, moduleNamed(list, L"taskbar.dll"), symbols, "*Unhide*",
        "public: virtual void __cdecl TrayUI::Unhide(enum TrayCommon::TrayUnhideFlags,enum TrayCommon::UnhideRequest)", config.images[0]);
    resolve(process, moduleNamed(list, L"taskbar.dll"), symbols, "*Unhide*",
        "private: void __cdecl CSecondaryTray::_Unhide(enum TrayCommon::TrayUnhideFlags,enum TrayCommon::UnhideRequest)", config.images[1]);
    resolve(process, moduleNamed(list, L"Taskbar.View.dll"), symbols, "*ShouldTaskbarBeExpanded*",
        "public: bool __cdecl winrt::Taskbar::implementation::ViewCoordinator::ShouldTaskbarBeExpanded(unsigned __int64,bool)", config.images[2]);
    config.windowCount = DWORD(bars.bars.size());
    for (size_t i = 0; i < bars.bars.size(); ++i) config.windows[i] = reinterpret_cast<UINT_PTR>(bars.bars[i]);
    return config;
}
void* allocateWrite(HANDLE process, const void* data, SIZE_T bytes) {
    void* memory = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!memory) fail("VirtualAllocEx");
    SIZE_T written{};
    if (!WriteProcessMemory(process, memory, data, bytes, &written) || written != bytes) fail("WriteProcessMemory");
    return memory;
}
DWORD remoteCall(HANDLE process, ULONG_PTR function, void* argument) {
    Handle thread(CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(function), argument, 0, nullptr));
    if (!thread.value) fail("CreateRemoteThread");
    // A timeout must not free argument memory: the remote thread may still read it.
    if (WaitForSingleObject(thread, 15000) != WAIT_OBJECT_0) fail("remote call timed out; remote memory intentionally retained");
    DWORD result{}; if (!GetExitCodeThread(thread, &result)) fail("remote result");
    return result;
}
void inject(HANDLE process, DWORD pid, HANDLE mapping) {
    const auto dll = (std::filesystem::path(ownPath()).parent_path() / L"IslandTaskbarPrototype.dll").wstring();
    HMODULE local = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!local) fail("local prototype DLL metadata");
    const auto start = reinterpret_cast<ULONG_PTR>(GetProcAddress(local, "StartSession")) - reinterpret_cast<ULONG_PTR>(local);
    FreeLibrary(local);
    if (!start || start > MAXDWORD) fail("StartSession export");
    const auto load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE loadModule{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(load), &loadModule)) fail("LoadLibrary module");
    wchar_t loadPath[MAX_PATH]{}; GetModuleFileNameW(loadModule, loadPath, MAX_PATH);
    const auto before = modules(pid);
    const auto& remoteModule = moduleNamed(before, std::filesystem::path(loadPath).filename().c_str());
    FileImage loader(loadPath); identity(process, remoteModule, loader);
    const auto loadRva = reinterpret_cast<ULONG_PTR>(load) - reinterpret_cast<ULONG_PTR>(loadModule);
    if (loadRva > MAXDWORD || !loader.executable(DWORD(loadRva))) fail("LoadLibrary RVA");
    void* remotePath = allocateWrite(process, dll.c_str(), (dll.size() + 1) * sizeof(wchar_t));
    remoteCall(process, remoteModule.base + loadRva, remotePath);
    VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    const auto after = modules(pid);
    const auto& injected = moduleNamed(after, L"IslandTaskbarPrototype.dll");
    if (_wcsicmp(injected.path.c_str(), dll.c_str())) fail("injected DLL path mismatch");
    HANDLE remoteMapping{}, remoteOwner{};
    if (!DuplicateHandle(GetCurrentProcess(), mapping, process, &remoteMapping, FILE_MAP_ALL_ACCESS, FALSE, 0)
        || !DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), process, &remoteOwner,
                            SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0)) fail("duplicate session handles");
    prototype::StartArgs args{reinterpret_cast<UINT_PTR>(remoteMapping), reinterpret_cast<UINT_PTR>(remoteOwner)};
    void* remoteArgs = allocateWrite(process, &args, sizeof(args));
    const auto result = remoteCall(process, injected.base + start, remoteArgs);
    VirtualFreeEx(process, remoteArgs, 0, MEM_RELEASE);
    if (result != ERROR_SUCCESS) { SetLastError(result); fail("DLL StartSession rejected session (one session per Explorer lifetime)"); }
}
bool run(const Windows& bars, prototype::Config config, ULONGLONG deadline) {
    if (guardianConflict()) fail("IslandDesktop/guardian is running; stop it before live prototype to avoid ownership conflict");
    Handle process(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_DUP_HANDLE | SYNCHRONIZE, FALSE, bars.pid));
    if (!process.value) fail("OpenProcess live");
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(prototype::Shared), nullptr));
    auto* shared = static_cast<prototype::Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(prototype::Shared)));
    if (!shared) fail("session mapping");
    shared->config = config;
    try { inject(process, bars.pid, mapping); } catch (...) { UnmapViewOfFile(shared); throw; }
    const auto readyDeadline = GetTickCount64() + 15000;
    while (prototype::read(&shared->phase) < prototype::ready && GetTickCount64() < readyDeadline
           && WaitForSingleObject(process, 0) == WAIT_TIMEOUT && !prototype::read(&stopping)) Sleep(25);
    if (prototype::read(&shared->phase) != prototype::ready) {
        InterlockedExchange(&shared->stop, 1); auto error = prototype::read(&shared->error);
        UnmapViewOfFile(shared); SetLastError(error); fail("DLL not ready");
    }
    std::wcout << L"READY: 3 pre-call hooks installed, initial DWM cloak applied. Full unlock suppression UNVERIFIED.\n";
    auto nextLog = GetTickCount64();
    while (!prototype::read(&stopping) && GetTickCount64() < deadline && WaitForSingleObject(process, 100) == WAIT_TIMEOUT) {
        if (GetTickCount64() >= nextLog) {
            std::wcout << L"blocked=" << prototype::read(&shared->blocked[0]) << L"," << prototype::read(&shared->blocked[1])
                       << L"," << prototype::read(&shared->blocked[2]) << L" phase=" << prototype::read(&shared->phase) << L"\n";
            nextLog = GetTickCount64() + 2000;
        }
        if (prototype::read(&shared->phase) != prototype::ready) break;
    }
    const bool died = WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
    InterlockedExchange(&shared->stop, 1);
    auto restoreDeadline = GetTickCount64() + 10000;
    while (!died && prototype::read(&shared->phase) != prototype::restored && prototype::read(&shared->phase) != prototype::failed
           && GetTickCount64() < restoreDeadline) Sleep(25);
    const auto phase = prototype::read(&shared->phase), error = prototype::read(&shared->error);
    std::wcout << L"STOP phase=" << phase << L" error=" << error << L" uncloaked=" << prototype::read(&shared->restoredWindows)
               << L" ExplorerExited=" << died << L"; DLL remains inert and resident.\n";
    UnmapViewOfFile(shared);
    if (!died && (phase != prototype::restored || error)) { SetLastError(error); fail("restoration incomplete"); }
    return died;
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        bool live = false; unsigned seconds = 60;
        auto cache = std::filesystem::path(ownPath()).parent_path() / L"symbols";
        std::wstring symbols = L"srv*" + cache.wstring() + L"*https://msdl.microsoft.com/download/symbols";
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg(argv[i]);
            if (arg == L"--live") live = true;
            else if (arg == L"--seconds" && i + 1 < argc) seconds = std::stoul(argv[++i]);
            else if (arg == L"--symbols" && i + 1 < argc) symbols = argv[++i];
            else if (arg == L"--self-test") {
                auto test = std::filesystem::path(ownPath()).parent_path() / L"windows_taskbar_prototype_test.exe";
                std::wstring command = L"\"" + test.wstring() + L"\"";
                STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
                if (!CreateProcessW(test.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) fail("private self-test");
                Handle child(pi.hProcess), thread(pi.hThread);
                if (WaitForSingleObject(child, 30000) != WAIT_OBJECT_0) fail("private self-test timed out");
                DWORD result{}; GetExitCodeProcess(child, &result); return int(result);
            } else fail("usage: taskbar-prototype [--live] [--seconds 1..300] [--symbols cache-path] [--self-test]");
        }
        if (seconds < 1 || seconds > 300) fail("duration must be 1..300 seconds");
        OSVERSIONINFOW os{sizeof(os)};
        using RtlVersion = LONG (WINAPI*)(OSVERSIONINFOW*);
        if (auto rtl = reinterpret_cast<RtlVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) rtl(&os);
        std::wcout << L"Windows " << os.dwMajorVersion << L"." << os.dwMinorVersion << L"." << os.dwBuildNumber
                   << L" x64; mode=" << (live ? L"LIVE" : L"READ-ONLY PROBE") << L"\nSymbols: " << symbols << L"\n";
        SetConsoleCtrlHandler(console, TRUE);
        const auto deadline = GetTickCount64() + seconds * 1000ULL;
        DWORD previous{};
        do {
            auto bars = taskbars();
            if (!bars.pid) {
                if (!live) fail("Explorer taskbars absent");
                Sleep(200); continue;
            }
            if (bars.pid == previous) fail("cannot rearm same Explorer process; resident prototype is one-shot");
            previous = bars.pid;
            std::wcout << L"Explorer pid=" << bars.pid << L" taskbars=" << bars.bars.size() << L" guardianConflict=" << guardianConflict() << L"\n";
            auto config = probe(bars, symbols);
            if (!live) { std::wcout << L"PROBE READY: exact symbols found; no injection, cloak, taskbar/autohide/workarea changes.\n"; break; }
            if (!run(bars, config, deadline)) break;
            std::wcout << L"Explorer exited. Awaiting fresh Explorer; startup/pre-injection flash cannot be prevented.\n";
        } while (!prototype::read(&stopping) && GetTickCount64() < deadline);
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL CLOSED: " << e.what() << "\n"; return 1; }
}
