# Isolated Windows x64 taskbar prototype

This is a Qt-free diagnostic experiment, not production integration. No Windhawk installation is used. Source code is independent of the GPL taskbar-keyboard mod; only factual Microsoft C++ symbol signatures were consulted. MinHook 1.3.4 is vendored unmodified at commit `c3fcafdc10146beb5919319d0683e44e3c30d537`; retain `vendor/minhook/LICENSE.txt` when redistributing binaries.

Build from a Visual Studio x64 development shell (or the repository's configured compiler environment):

```powershell
./scripts/taskbar-prototype.ps1 -Build -SelfTest
./scripts/taskbar-prototype.ps1 -Symbols 'C:\absolute\matching-pdb-directory;C:\absolute\other-matching-pdb-directory'
./scripts/taskbar-prototype.ps1 -Live -Seconds 60 -Symbols 'C:\absolute\matching-pdb-directory;C:\absolute\other-matching-pdb-directory'
```

The standalone CMake source directory is this folder; it is deliberately absent from root production CMake. Default invocation is a read-only probe: lists the actual Explorer PID/taskbars/modules/build, resolves three complete ABI signatures, and checks PDB GUID+age, PE timestamp/size, remote RSDS, and executable section bounds. It neither injects nor changes a taskbar. The default `srv*...*https://msdl.microsoft.com/download/symbols` path needs a compatible `symsrv.dll`; alternatively preload the exact PDBs with `scripts/taskbar-symbols.py` and supply both full PDB parent directories separated by a semicolon. Missing symbols/modules/identity fail closed. No arbitrary offsets/build guesses are used. This version supports the modern `taskbar.dll` + `Taskbar.View.dll` layout only; legacy ExplorerExtensions layouts are rejected.

Only explicit `--live` injects the sibling DLL into real Explorer. Stop Island Desktop, including its guardian, before live use; the host refuses while any `IslandDesktop.exe` is present. Real taskbars are selected by class, Windows Explorer executable path, session, and PID, never by the first Shell_TrayWnd (Island has its own).

The DLL blocks `TrayUI::Unhide`, `CSecondaryTray::_Unhide` before their originals, and returns false from `ViewCoordinator::ShouldTaskbarBeExpanded` while active. It applies DWM cloak once to initially uncloaked taskbars; it never polls SW_HIDE, changes auto-hide/registry, or adjusts the work area. Taskbar reserved desktop space therefore remains. The host/DLL use anonymous process-bound mappings and duplicated handles; DLL validates owner executable path, creation time, SID/session, and target image. A guard thread watches host death and graceful stop, disables blocking and hooks, then removes only its marked cloaks. Initially cloaked bars stay cloaked; ordinary window visibility stays unchanged. Diagnostics include three block counts, restore count, phase, and restore failures.

Ctrl+C or the 1–300 second deadline requests restoration; killing the host also triggers the in-Explorer guard. The DLL and MinHook trampolines remain pinned/resident until Explorer exits to avoid freeing callbacks under execution. **One session per Explorer process lifetime:** a second live run is refused; do not restart Explorer automatically. A live host can attach to a fresh Explorer PID after Explorer itself restarts, within its deadline, but cannot prevent any startup/pre-injection flash. Monitors/taskbars created after initial snapshot are outside this experiment. Host timeout may leave bounded remote allocation/duplicated handles resident because freeing memory still used by a timed-out remote thread is unsafe.

`--self-test` runs only a private executable with private window classes and three private hook functions. It verifies pre-call suppression, restored original calls, owner-process death, one cloak removal, and preservation of a pre-existing cloak. It never touches real taskbars. It requires interactive DWM. No Win+L, Explorer restart, full modern taskbar suppression, or production stability is established by these tests. Hook readiness alone is not proof all reveal/animation paths are blocked; a shell component may remove cloak or use another path. Run coordinated live checks separately and record observed results.
