#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "taskbarworkarea.h"
#include "../../core/taskbarsession.h"

namespace TaskbarNative {
// Test policies are supplied only by the native test executable, never parsed
// by IslandDesktop. Production always targets verified Explorer taskbar windows.
struct Policy {
    std::wstring testClass;
    DWORD testOwner = 0;
    std::wstring mutexName = L"Local\\IslandDesktop.TaskbarGuardian";
};

bool hideWindows(const std::wstring &marker, const Policy &policy = {},
    WorkAreaJournal *journal = nullptr, WorkAreaOperations *operations = nullptr,
    IslandDesktop::TaskbarSessionControl *session = nullptr);
bool restoreWindows(const std::wstring &marker, const Policy &policy = {});
struct ControlSnapshot { std::uint64_t geometry = 0; bool reservation = false; };
ControlSnapshot controlSnapshot(const Policy &policy, WorkAreaOperations *operations);
// Returns -1 for a normal application invocation. No QApplication is required.
int guardianEntry(int argc, char **argv, const Policy &policy = {});

struct GuardianProcess {
    HANDLE process = nullptr, stop = nullptr, ready = nullptr;
    std::unique_ptr<WorkAreaJournal> workArea;
    GuardianProcess() = default;
    ~GuardianProcess();
    GuardianProcess(const GuardianProcess &) = delete;
    GuardianProcess &operator=(const GuardianProcess &) = delete;
    bool start(const std::wstring &executable, const std::wstring &marker,
        const std::vector<std::wstring> &testArguments = {}, bool restoreOnly = false);
    void close();
};
}
