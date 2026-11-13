<#
.SYNOPSIS
Passively records the real Explorer taskbars and monitor work areas as JSONL.
.DESCRIPTION
Reads window state only. Never hides windows, changes work areas, moves input,
or restarts Explorer. Island's imitation Shell_TrayWnd is excluded by ownership.
Out-of-context WinEvents capture show/hide/cloak notifications between samples;
each event reports state at callback time, which can already differ from event time.
This is window-state evidence, NOT proof that no composed frame briefly flashed.
Monitor/window coordinates are read in this process's existing DPI context.
The output file is replaced, so choose a new OutputPath to retain earlier evidence.
.EXAMPLE
./scripts/observe-taskbar-prototype.ps1 -DurationSeconds 30 -OutputPath artifacts/taskbar-prototype/observe.jsonl
#>
param(
    [ValidateRange(1, 3600)][int]$DurationSeconds = 30,
    [string]$OutputPath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'artifacts/taskbar-prototype/observe.jsonl'),
    [ValidateRange(1, 10000)][int]$SamplingIntervalMs = 50
)

$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This observer requires Windows.' }

if (-not ('IslandTaskbarObservation.Observer' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace IslandTaskbarObservation {
    public sealed class Observer : IDisposable {
        [StructLayout(LayoutKind.Sequential)] public struct Rect {
            public int Left, Top, Right, Bottom;
            public override string ToString() { return Left + "," + Top + "," + Right + "," + Bottom; }
        }
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] struct MonitorInfo {
            public int Size; public Rect Monitor, Work; public uint Flags;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string Device;
        }
        [StructLayout(LayoutKind.Sequential)] struct Message {
            public IntPtr Window; public uint Id; public UIntPtr WParam; public IntPtr LParam;
            public uint Time; public int X, Y; public uint Private;
        }
        delegate bool EnumWindowsProc(IntPtr window, IntPtr data);
        delegate bool EnumMonitorsProc(IntPtr monitor, IntPtr dc, ref Rect rect, IntPtr data);
        delegate void WinEventProc(IntPtr hook, uint evt, IntPtr window, int obj, int child, uint thread, uint time);
        [DllImport("user32.dll")] static extern IntPtr GetShellWindow();
        [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder name, int count);
        [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc callback, IntPtr data);
        [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
        [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window, out Rect rect);
        [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr window, uint attribute, out uint value, uint size);
        [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool QueryFullProcessImageName(IntPtr process, uint flags, StringBuilder name, ref uint size);
        [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
        [DllImport("user32.dll")] static extern bool EnumDisplayMonitors(IntPtr dc, IntPtr clip, EnumMonitorsProc callback, IntPtr data);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool GetMonitorInfo(IntPtr monitor, ref MonitorInfo info);
        [DllImport("user32.dll", SetLastError = true)] static extern IntPtr SetWinEventHook(uint min, uint max, IntPtr module, WinEventProc callback, uint pid, uint thread, uint flags);
        [DllImport("user32.dll")] static extern bool UnhookWinEvent(IntPtr hook);
        [DllImport("user32.dll")] static extern bool PeekMessage(out Message msg, IntPtr window, uint min, uint max, uint remove);
        [DllImport("user32.dll")] static extern bool TranslateMessage(ref Message msg);
        [DllImport("user32.dll")] static extern IntPtr DispatchMessage(ref Message msg);
        [DllImport("user32.dll")] static extern uint MsgWaitForMultipleObjectsEx(uint count, IntPtr handles, uint ms, uint mask, uint flags);

        readonly Queue<object> pending = new Queue<object>();
        readonly List<IntPtr> hooks = new List<IntPtr>();
        readonly WinEventProc callback;
        string previous;
        public Observer() { callback = OnEvent; }
        static string Handle(IntPtr window) { return "0x" + window.ToInt64().ToString("X"); }
        static uint ShellPid() {
            uint pid; GetWindowThreadProcessId(GetShellWindow(), out pid);
            if (pid == 0) return 0;
            IntPtr process = OpenProcess(0x1000, false, pid); // QUERY_LIMITED_INFORMATION
            if (process == IntPtr.Zero) return 0;
            try {
                var path = new StringBuilder(32768); uint size = (uint)path.Capacity;
                string expected = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "explorer.exe");
                return QueryFullProcessImageName(process, 0, path, ref size) &&
                    String.Equals(path.ToString(), expected, StringComparison.OrdinalIgnoreCase) ? pid : 0;
            } finally { CloseHandle(process); }
        }
        static object WindowState(IntPtr window, uint shellPid, out string signature) {
            signature = null;
            if (shellPid == 0) return null;
            uint pid; GetWindowThreadProcessId(window, out pid);
            if (pid != shellPid) return null;
            var name = new StringBuilder(128); GetClassName(window, name, name.Capacity);
            string cls = name.ToString();
            if (cls != "Shell_TrayWnd" && cls != "Shell_SecondaryTrayWnd") return null;
            Rect rect; bool rectRead = GetWindowRect(window, out rect);
            uint cloak; int hr = DwmGetWindowAttribute(window, 14, out cloak, 4); // DWMWA_CLOAKED
            bool visible = IsWindowVisible(window);
            signature = Handle(window) + ":" + pid + ":" + cls + ":" + visible + ":" + hr + ":" + cloak + ":" + rectRead + ":" + rect;
            return new { hwnd = Handle(window), pid = pid, windowClass = cls, visible = visible,
                cloakBits = hr == 0 ? (uint?)cloak : null, cloakHresult = hr,
                rect = rectRead ? (object)rect : null };
        }
        void Record(string kind, object state) {
            pending.Enqueue(new { utc = DateTime.UtcNow.ToString("O"), kind = kind, state = state });
        }
        public void Start() {
            // Constants: https://learn.microsoft.com/en-us/windows/win32/winauto/event-constants
            // WINEVENT_OUTOFCONTEXT = 0. Global hooks also observe Explorer restarts.
            foreach (uint evt in new uint[] { 0x8002, 0x8003, 0x8017, 0x8018 }) {
                IntPtr hook = SetWinEventHook(evt, evt, IntPtr.Zero, callback, 0, 0, 0);
                if (hook == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "SetWinEventHook failed");
                hooks.Add(hook);
            }
            Sample("initial");
        }
        void OnEvent(IntPtr hook, uint evt, IntPtr window, int obj, int child, uint thread, uint time) {
            if (obj != 0 || child != 0 || window == IntPtr.Zero) return; // OBJID_WINDOW, CHILDID_SELF
            string ignored;
            object state = WindowState(window, ShellPid(), out ignored);
            if (state == null) return;
            string name = evt == 0x8002 ? "show" : evt == 0x8003 ? "hide" : evt == 0x8017 ? "cloaked" : "uncloaked";
            Record("winevent", new { eventName = name, eventId = evt, eventTimeMs = time, window = state });
        }
        public void Sample(string kind) {
            uint shellPid = ShellPid();
            var windows = new SortedDictionary<string, object>();
            var signatures = new SortedDictionary<string, string>();
            EnumWindowsProc enumWindows = delegate(IntPtr window, IntPtr data) {
                string signature; object state = WindowState(window, shellPid, out signature);
                if (state != null) { windows.Add(Handle(window), state); signatures.Add(Handle(window), signature); }
                return true;
            };
            if (!EnumWindows(enumWindows, IntPtr.Zero)) throw new InvalidOperationException("EnumWindows failed");
            var monitors = new SortedDictionary<string, object>();
            var monitorSignatures = new SortedDictionary<string, string>();
            bool monitorReadFailed = false;
            EnumMonitorsProc enumMonitors = delegate(IntPtr monitor, IntPtr dc, ref Rect rect, IntPtr data) {
                var info = new MonitorInfo(); info.Size = Marshal.SizeOf(typeof(MonitorInfo));
                if (!GetMonitorInfo(monitor, ref info)) { monitorReadFailed = true; return true; }
                string key = Handle(monitor);
                monitors.Add(key, new { hmonitor = key, device = info.Device, primary = (info.Flags & 1) != 0, bounds = info.Monitor, workArea = info.Work });
                monitorSignatures.Add(key, key + ":" + info.Device + ":" + info.Flags + ":" + info.Monitor + ":" + info.Work);
                return true;
            };
            if (!EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, enumMonitors, IntPtr.Zero) || monitorReadFailed)
                throw new InvalidOperationException("Monitor topology could not be read completely");
            string current = shellPid + "|" + String.Join("|", signatures.Values) + "|" + String.Join("|", monitorSignatures.Values);
            if (kind != "change" || previous != current)
                Record(kind, new { shellPid = shellPid, shellVerified = shellPid != 0, taskbars = new List<object>(windows.Values), monitors = new List<object>(monitors.Values) });
            previous = current;
        }
        public void Pump(int milliseconds) {
            MsgWaitForMultipleObjectsEx(0, IntPtr.Zero, (uint)milliseconds, 0x04FF, 0x0004); // QS_ALLINPUT, MWMO_INPUTAVAILABLE
            Message msg;
            while (PeekMessage(out msg, IntPtr.Zero, 0, 0, 1)) { TranslateMessage(ref msg); DispatchMessage(ref msg); }
        }
        public object[] Drain() { var result = pending.ToArray(); pending.Clear(); return result; }
        public void Dispose() { foreach (IntPtr hook in hooks) UnhookWinEvent(hook); hooks.Clear(); GC.KeepAlive(callback); }
    }
}
'@
}

$fullOutputPath = [IO.Path]::GetFullPath($OutputPath)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($fullOutputPath)) | Out-Null
$writer = [IO.StreamWriter]::new($fullOutputPath, $false, [Text.UTF8Encoding]::new($false))
$observer = [IslandTaskbarObservation.Observer]::new()
try {
    $writer.AutoFlush = $true
    $observer.Start()
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $nextSampleMs = 0L
    while ($timer.Elapsed.TotalSeconds -lt $DurationSeconds) {
        if ($timer.ElapsedMilliseconds -ge $nextSampleMs) {
            $observer.Sample('change')
            $nextSampleMs = $timer.ElapsedMilliseconds + $SamplingIntervalMs
        }
        foreach ($record in $observer.Drain()) { $writer.WriteLine(($record | ConvertTo-Json -Depth 10 -Compress)) }
        $remainingMs = [Math]::Max(0, $DurationSeconds * 1000 - $timer.ElapsedMilliseconds)
        $waitMs = [Math]::Max(0, [Math]::Min($nextSampleMs - $timer.ElapsedMilliseconds, $remainingMs))
        $observer.Pump([int]$waitMs)
    }
    $observer.Pump(0)
    $observer.Sample('final')
    foreach ($record in $observer.Drain()) { $writer.WriteLine(($record | ConvertTo-Json -Depth 10 -Compress)) }
} finally {
    $observer.Dispose()
    $writer.Dispose()
}
Write-Host "Taskbar observation saved to $fullOutputPath"
