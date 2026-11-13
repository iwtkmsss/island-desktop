#pragma once
#include <windows.h>
#include <cstdint>

namespace prototype {
constexpr DWORD magic = 0x49544250, version = 1;
enum Phase : LONG { idle, starting, ready, stopping, restored, failed };
struct Image {
    wchar_t path[MAX_PATH];
    DWORD timestamp, size, rva;
};
// Anonymous mapping and process handles are duplicated directly into Explorer.
// No named objects, pointers, strings from windows, or unbounded IPC payloads.
struct Config {
    DWORD signature = magic, revision = version, bytes = sizeof(Config);
    DWORD ownerPid = 0, targetPid = 0, privateTest = 0;
    FILETIME ownerCreation{};
    Image images[3]{};
    DWORD windowCount = 0;
    UINT_PTR windows[32]{};
};
struct Shared {
    Config config{};
    volatile LONG phase = idle, error = 0, stop = 0;
    volatile LONG blocked[3]{}, restoredWindows = 0;
};
struct StartArgs { UINT_PTR mapping, owner; };
using Start = DWORD (WINAPI*)(void*);
inline LONG read(volatile LONG* p) { return InterlockedCompareExchange(p, 0, 0); }
inline bool equalTime(FILETIME a, FILETIME b) {
    return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}
}

