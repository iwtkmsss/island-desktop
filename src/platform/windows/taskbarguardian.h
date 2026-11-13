#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace TaskbarNative {
// Test policies are supplied only by the native test executable, never parsed
// by IslandDesktop. Production always targets verified Explorer taskbar windows.
struct Policy {
    std::wstring testClass;
    DWORD testOwner = 0;
    std::wstring mutexName = L"Local\\IslandDesktop.TaskbarGuardian";
};

bool hideWindows(const std::wstring &marker, const Policy &policy = {});
bool restoreWindows(const std::wstring &marker, const Policy &policy = {});
// Returns -1 for a normal application invocation. No QApplication is required.
int guardianEntry(int argc, char **argv, const Policy &policy = {});

struct GuardianProcess {
    HANDLE process = nullptr, stop = nullptr, ready = nullptr;
    GuardianProcess() = default;
    ~GuardianProcess();
    GuardianProcess(const GuardianProcess &) = delete;
    GuardianProcess &operator=(const GuardianProcess &) = delete;
    bool start(const std::wstring &executable, const std::wstring &marker,
        const std::vector<std::wstring> &testArguments = {});
    void close();
};
}
