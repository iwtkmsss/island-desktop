#include "taskbarcontroller.h"
#include "taskbarguardian.h"
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace {
namespace Area = IslandDesktop::WorkArea;
constexpr wchar_t privateClass[] = L"IslandDesktopPrivateTaskbarTest";
std::wstring marker() {
    return (QStringLiteral("IslandDesktop.HiddenTaskbar.")
        + QUuid::createUuid().toString(QUuid::WithoutBraces)).toStdWString();
}
TaskbarNative::Policy policy(DWORD owner, const std::wstring &tag) {
    return {privateClass, owner, L"Local\\" + tag};
}
struct Window {
    HWND hwnd = nullptr;
    explicit Window(bool visible = true) {
        hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, privateClass, L"Private taskbar test", WS_POPUP,
            -30000, -30000, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (visible) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
    ~Window() { if (hwnd) DestroyWindow(hwnd); }
};
struct Guardian {
    TaskbarNative::GuardianProcess child;
    ~Guardian() { if (child.process) { SetEvent(child.stop); WaitForSingleObject(child.process, 3000); } }
};
std::wstring executable() {
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, 32768); return path;
}
class FakeWorkAreas final : public TaskbarNative::WorkAreaOperations {
public:
    std::vector<TaskbarNative::MonitorWorkArea> values;
    int writes = 0, preparations = 0, cleanups = 0;
    bool failWrite = false, crashAfterWrite = false;
    std::vector<TaskbarNative::MonitorWorkArea> monitors() override { return values; }
    void prepare(const TaskbarNative::MonitorWorkArea &, TaskbarNative::WorkRect, bool) override { ++preparations; }
    bool set(const TaskbarNative::MonitorWorkArea &monitor, TaskbarNative::WorkRect desired) override {
        if (failWrite) return false;
        for (auto &value : values) {
            if (value.device == monitor.device && value.monitor == monitor.monitor && value.work == monitor.work) {
                value.work = desired; ++writes;
                if (crashAfterWrite) throw std::runtime_error("simulated guardian death after SPI");
                return true;
            }
        }
        return false;
    }
    void finishRestore() override { ++cleanups; }
};
}

class WindowsTaskbarTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        // Even a persisted true preference must never start production control.
        TaskbarController::setControlAllowed(false);
        WNDCLASSW cls{}; cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = privateClass;
        QVERIFY(RegisterClassW(&cls));
    }
    void onlyVisibleOwnedWindowsAreRestored() {
        const auto tag = marker(); const auto target = policy(GetCurrentProcessId(), tag);
        Window visible, hidden(false);
        QVERIFY(visible.hwnd && hidden.hwnd);
        QVERIFY(TaskbarNative::hideWindows(tag, target));
        QTRY_VERIFY(!IsWindowVisible(visible.hwnd));
        QVERIFY(GetPropW(visible.hwnd, tag.c_str()));
        QVERIFY(!GetPropW(hidden.hwnd, tag.c_str()));
        QVERIFY(TaskbarNative::restoreWindows(tag, target));
        QTRY_VERIFY(IsWindowVisible(visible.hwnd));
        QVERIFY(!IsWindowVisible(hidden.hwnd));
        QVERIFY(!GetPropW(visible.hwnd, tag.c_str()));
    }
    void wrongOwnerAndReusedWindowAreUntouched() {
        const auto tag = marker(); Window window;
        QVERIFY(TaskbarNative::hideWindows(tag, policy(GetCurrentProcessId() + 1, tag)));
        QVERIFY(IsWindowVisible(window.hwnd));
        const auto target = policy(GetCurrentProcessId(), tag);
        QVERIFY(TaskbarNative::hideWindows(tag, target));
        QTRY_VERIFY(!IsWindowVisible(window.hwnd));
        DestroyWindow(window.hwnd); window.hwnd = nullptr;
        Window replacement(false);
        QVERIFY(TaskbarNative::restoreWindows(tag, target));
        QVERIFY(!IsWindowVisible(replacement.hwnd));
        QVERIFY(!GetPropW(replacement.hwnd, tag.c_str()));
    }
    void guardianRehidesAndDiscoversReplacementWindows() {
        const auto tag = marker(); Window first;
        Guardian guardian;
        QVERIFY(guardian.child.start(executable(), tag, {std::to_wstring(GetCurrentProcessId())}));
        QTRY_COMPARE(WaitForSingleObject(guardian.child.ready, 0), DWORD(WAIT_OBJECT_0));
        QTRY_VERIFY(!IsWindowVisible(first.hwnd));
        ShowWindow(first.hwnd, SW_SHOWNOACTIVATE);
        QTRY_VERIFY_WITH_TIMEOUT(!IsWindowVisible(first.hwnd), 1500);
        Window added;
        QTRY_VERIFY_WITH_TIMEOUT(!IsWindowVisible(added.hwnd), 1500);
        SetEvent(guardian.child.stop);
        QTRY_COMPARE(WaitForSingleObject(guardian.child.process, 0), DWORD(WAIT_OBJECT_0));
        QTRY_VERIFY(IsWindowVisible(first.hwnd)); QTRY_VERIFY(IsWindowVisible(added.hwnd));
        DWORD code = 99; QVERIFY(GetExitCodeProcess(guardian.child.process, &code)); QCOMPARE(code, DWORD(0));
    }
    void parentDeathRestoresWithoutParentCleanup() {
        const auto tag = marker(); Window window;
        QProcess parent;
        parent.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--test-taskbar-parent"),
            QString::fromStdWString(tag), QString::number(GetCurrentProcessId())});
        QVERIFY(parent.waitForStarted(3000));
        QTRY_VERIFY_WITH_TIMEOUT(parent.canReadLine(), 5000);
        const auto guardianId = parent.readLine().trimmed().toULong();
        QVERIFY(guardianId);
        HANDLE guardian = OpenProcess(SYNCHRONIZE, FALSE, guardianId);
        QVERIFY(guardian);
        QTRY_VERIFY(!IsWindowVisible(window.hwnd));
        parent.kill(); QVERIFY(parent.waitForFinished(3000));
        QTRY_VERIFY_WITH_TIMEOUT(IsWindowVisible(window.hwnd), 3000);
        QTRY_COMPARE(WaitForSingleObject(guardian, 0), DWORD(WAIT_OBJECT_0));
        CloseHandle(guardian);
        QVERIFY(!GetPropW(window.hwnd, tag.c_str()));
    }
    void preferenceSurvivesRecreationWithoutSystemControl() {
        QTemporaryDir directory; const auto file = directory.filePath("settings.ini");
        {
            TaskbarController controller(file);
            QVERIFY(controller.available()); QVERIFY(!controller.hidden());
            controller.setHidden(true); QVERIFY(controller.hidden()); QVERIFY(controller.error().isEmpty());
        }
        {
            TaskbarController controller(file);
            QVERIFY(controller.hidden()); QVERIFY(controller.error().isEmpty());
            controller.setHidden(false);
        }
        TaskbarController restored(file); QVERIFY(!restored.hidden());
    }
    void preferenceWriteFailureIsReported() {
        QTemporaryDir directory;
        // An existing directory cannot be used as an INI file.
        TaskbarController controller(directory.path());
        controller.setHidden(true);
        QVERIFY(!controller.error().isEmpty());
    }
    void stripGeometryPreservesOtherAppbars() {
        const Area::Rect monitor{0,0,1920,1080}, work{40,20,1920,1020};
        const auto bottom = Area::taskbarStrip(monitor, work, {0,1020,1920,1080});
        QVERIFY(bottom); QVERIFY(bottom->side == Area::Edge::Bottom);
        QVERIFY(Area::withEdge(work, bottom->side, bottom->after) == (Area::Rect{40,20,1920,1080}));
        const auto restored = Area::restore(monitor, {80,30,1900,1080}, *bottom);
        QVERIFY(restored); QVERIFY(*restored == (Area::Rect{80,30,1900,1020}));
        QVERIFY(!Area::restore(monitor, {40,20,1920,1000}, *bottom)); // External change to our edge.
        QVERIFY(!Area::taskbarStrip(monitor, {0,0,1920,1000}, {0,1040,1920,1080})); // Disjoint stacked appbar.
        const auto stacked = Area::taskbarStrip(monitor, {0,0,1920,1000}, {0,1000,1920,1040});
        QVERIFY(stacked); QCOMPARE(stacked->after, 1040); // Preserve outer appbar's 40 px.
        QVERIFY(!Area::taskbarStrip(monitor, monitor, {0,1078,1920,1126})); // Auto-hide reserves no strip.
    }
    void initiallyHiddenTaskbarReleasesOnlyFakeWorkArea() {
        const auto tag = marker(); const auto target = policy(GetCurrentProcessId(), tag);
        Window window(false); QVERIFY(window.hwnd);
        const auto previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        MONITORINFO info{}; info.cbSize = sizeof(info);
        const bool read = GetMonitorInfoW(MonitorFromPoint({0,0}, MONITOR_DEFAULTTOPRIMARY), &info) != FALSE;
        if (read) SetWindowPos(window.hwnd, nullptr, info.rcMonitor.left, info.rcMonitor.bottom - 40,
            info.rcMonitor.right - info.rcMonitor.left, 40, SWP_NOACTIVATE | SWP_NOZORDER);
        if (previous) SetThreadDpiAwarenessContext(previous);
        QVERIFY(read);
        const Area::Rect monitor{info.rcMonitor.left,info.rcMonitor.top,info.rcMonitor.right,info.rcMonitor.bottom};
        auto before = monitor; before.bottom -= 40;
        FakeWorkAreas fake; fake.values = {{L"private-fake-monitor", monitor, before}};
        TaskbarNative::WorkAreaJournal journal; QVERIFY(journal.create(tag));
        QVERIFY(TaskbarNative::hideWindows(tag, target, &journal, &fake));
        QVERIFY(!IsWindowVisible(window.hwnd)); QVERIFY(!GetPropW(window.hwnd, tag.c_str()));
        QVERIFY(fake.values[0].work == monitor);
        QVERIFY(TaskbarNative::restoreWindows(tag, target));
        QVERIFY(journal.restore(fake));
        QVERIFY(fake.values[0].work == before);
        QVERIFY(!IsWindowVisible(window.hwnd)); QVERIFY(!GetPropW(window.hwnd, tag.c_str()));
    }
    void stripsHandleAllEdgesAndNegativePhysicalCoordinates() {
        const Area::Rect monitor{-2560,-1440,0,0};
        const auto top = Area::taskbarStrip(monitor, {-2560,-1380,0,0}, {-2560,-1440,0,-1380});
        QVERIFY(top); QVERIFY(top->side == Area::Edge::Top); QCOMPARE(top->after, -1440);
        const auto left = Area::taskbarStrip(monitor, {-2500,-1440,0,0}, {-2560,-1440,-2500,0});
        QVERIFY(left); QVERIFY(left->side == Area::Edge::Left); QCOMPARE(left->after, -2560);
        const auto right = Area::taskbarStrip(monitor, {-2560,-1440,-60,0}, {-60,-1440,0,0});
        QVERIFY(right); QVERIFY(right->side == Area::Edge::Right); QCOMPARE(right->after, 0);
    }
    void maximizedReflowKeepsBordersAndIgnoresUnrelatedFrames() {
        const Area::Rect before{-2560,-1440,0,-60}, after{-2560,-1440,0,0};
        const auto decorated = Area::reflow(before, after, before, {-2569,-1449,9,-51});
        QVERIFY(decorated); QVERIFY(*decorated == (Area::Rect{-2569,-1449,9,9}));
        const auto frameless = Area::reflow(before, after, before, before);
        QVERIFY(frameless); QVERIFY(*frameless == after);
        QVERIFY(!Area::reflow(before, after, after, after)); // Already full monitor/fullscreen.
        QVERIFY(!Area::reflow(before, after, {-2000,-1000,-200,-100}, before));
        QVERIFY(!Area::windowedBeforeRelease({0,0,1920,1080}, {0,0,1920,1079}, {0,0,1920,1080}));
        QVERIFY(Area::windowedBeforeRelease({0,0,1920,1080}, {0,0,1920,1020}, {0,0,1920,1020}));
    }
    void sharedJournalRestoresAfterGuardianViewDies() {
        const auto tag = marker();
        TaskbarNative::WorkAreaJournal parent;
        QVERIFY(parent.create(tag));
        FakeWorkAreas fake;
        fake.values = {{L"display-primary", {0,0,1920,1080}, {0,0,1920,1020}},
            {L"display-left", {-2560,-1440,0,0}, {-2560,-1440,0,-48}}};
        {
            TaskbarNative::WorkAreaJournal guardian; QVERIFY(guardian.open(tag));
            QVERIFY(guardian.release(fake.values[0], {0,1020,1920,1080}, fake));
            QVERIFY(guardian.release(fake.values[1], {-2560,-48,0,0}, fake));
        }
        fake.values[0].work.left = 40; // Another appbar appeared while ours was active.
        QVERIFY(parent.restore(fake));
        QVERIFY(fake.values[0].work == (Area::Rect{40,0,1920,1020}));
        QVERIFY(fake.values[1].work == (Area::Rect{-2560,-1440,0,-48}));
        QCOMPARE(fake.preparations, 2); QCOMPARE(fake.cleanups, 1);
    }
    void journalCoversEitherSideOfSpiAndExplorerReassertion() {
        const auto tag = marker(); TaskbarNative::WorkAreaJournal parent; QVERIFY(parent.create(tag));
        FakeWorkAreas fake; fake.values = {{L"display", {0,0,1920,1080}, {0,0,1920,1020}}};
        {
            TaskbarNative::WorkAreaJournal guardian; QVERIFY(guardian.open(tag));
            fake.crashAfterWrite = true;
            bool crashed = false;
            try { guardian.release(fake.values[0], {0,1020,1920,1080}, fake); }
            catch (const std::runtime_error &) { crashed = true; }
            QVERIFY(crashed);
        }
        fake.crashAfterWrite = false;
        QVERIFY(parent.restore(fake)); QCOMPARE(fake.values[0].work.bottom, 1020);
        const auto otherTag = marker(); TaskbarNative::WorkAreaJournal journal; QVERIFY(journal.create(otherTag));
        fake.failWrite = true;
        QVERIFY(!journal.release(fake.values[0], {0,1020,1920,1080}, fake));
        fake.failWrite = false;
        QVERIFY(journal.release(fake.values[0], {0,1020,1920,1080}, fake));
        fake.values[0].work.bottom = 1020; // Explorer reasserts the original reservation.
        QVERIFY(journal.release(fake.values[0], {0,1020,1920,1080}, fake));
        QCOMPARE(fake.values[0].work.bottom, 1080);
        fake.values[0].work.bottom = 1000; // New Explorer taskbar size after restart.
        QVERIFY(journal.release(fake.values[0], {0,1000,1920,1080}, fake));
        QVERIFY(journal.restore(fake)); QCOMPARE(fake.values[0].work.bottom, 1000);
    }
    void journalNeverOverwritesDifferentExternalStateOrTopology() {
        const auto tag = marker(); TaskbarNative::WorkAreaJournal journal; QVERIFY(journal.create(tag));
        FakeWorkAreas fake; fake.values = {{L"display", {0,0,1920,1080}, {0,0,1920,1020}}};
        QVERIFY(journal.release(fake.values[0], {0,1020,1920,1080}, fake));
        fake.values[0].work.bottom = 950;
        QVERIFY(journal.restore(fake)); QCOMPARE(fake.values[0].work.bottom, 950);
        const auto secondTag = marker(); TaskbarNative::WorkAreaJournal second; QVERIFY(second.create(secondTag));
        fake.values[0].work.bottom = 1020;
        QVERIFY(second.release(fake.values[0], {0,1020,1920,1080}, fake));
        fake.values[0] = {L"display", {0,0,2560,1440}, {0,0,2560,1380}};
        QVERIFY(second.restore(fake)); QCOMPARE(fake.values[0].work.bottom, 1380);
    }
    void laterBaselineSupersedesHistoryAcrossRestoreRetry() {
        const auto tag = marker(); TaskbarNative::WorkAreaJournal journal; QVERIFY(journal.create(tag));
        FakeWorkAreas fake; fake.values = {{L"display", {0,0,1920,1080}, {0,0,1920,1000}}};
        QVERIFY(journal.release(fake.values[0], {0,1000,1920,1050}, fake));
        QCOMPARE(fake.values[0].work.bottom, 1050);
        QVERIFY(journal.release(fake.values[0], {0,1050,1920,1080}, fake));
        fake.failWrite = true;
        QVERIFY(!journal.restore(fake)); QCOMPARE(fake.values[0].work.bottom, 1080);
        fake.failWrite = false;
        QVERIFY(journal.restore(fake)); QCOMPARE(fake.values[0].work.bottom, 1050);
        QVERIFY(journal.restore(fake)); QCOMPARE(fake.values[0].work.bottom, 1050);
    }
    void knownWindowedMaximizedUsesOnlyActiveSessionAndStyle() {
        const auto tag = marker(); Window window;
        const auto style = GetWindowLongPtrW(window.hwnd, GWL_STYLE) | WS_MAXIMIZE;
        SetWindowLongPtrW(window.hwnd, GWL_STYLE, style);
        const auto name = tag + L".ReflowStyle";
        QVERIFY(SetPropW(window.hwnd, name.c_str(), reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(style))));
        TaskbarNative::setActiveWorkAreaMarker(tag);
        QVERIFY(TaskbarNative::isKnownWindowedMaximized(window.hwnd));
        SetWindowLongPtrW(window.hwnd, GWL_STYLE, style & ~LONG_PTR(WS_MAXIMIZE));
        QVERIFY(!TaskbarNative::isKnownWindowedMaximized(window.hwnd));
        SetWindowLongPtrW(window.hwnd, GWL_STYLE, style | WS_CAPTION);
        QVERIFY(!TaskbarNative::isKnownWindowedMaximized(window.hwnd));
        TaskbarNative::setActiveWorkAreaMarker({});
        QVERIFY(!TaskbarNative::isKnownWindowedMaximized(window.hwnd));
    }
    void lockedSessionBlocksAllControlUntilGeometrySettles() {
        IslandDesktop::TaskbarSessionControl gate;
        gate.observe(false, 1, 0);
        QVERIFY(!gate.active(5000)); QVERIFY(!gate.cleanupReady(5000));
        QVERIFY(!gate.allowHide(10,5000)); QVERIFY(!gate.allowWork(1,5000));
        gate.observe(true,1,6000);
        gate.observe(true,2,7000); // Explorer changes geometry during unlock.
        gate.observe(true,2,8499); QVERIFY(!gate.active(8499));
        gate.observe(true,2,8500); QVERIFY(gate.active(8500)); QVERIFY(gate.cleanupReady(8500));
        gate.observe(false,2,8600);
        QVERIFY(!gate.active(10000)); QVERIFY(!gate.cleanupReady(10000));
        gate.observe(true,2,11000); QVERIFY(!gate.active(11000));
        gate.observe(true,2,12500); QVERIFY(gate.active(12500));
        gate.observe(true,3,12600);
        QVERIFY(!gate.allowWork(3,12600)); QVERIFY(!gate.cleanupReady(12600));
        gate.observe(true,3,14100); QVERIFY(gate.cleanupReady(14100));
    }
    void workAreaFeedbackHasBoundedRetriesAndStillAllowsRecovery() {
        IslandDesktop::TaskbarSessionControl gate;
        gate.observe(true,7,0); gate.observe(true,7,1500);
        QVERIFY(gate.allowWork(7,1500)); QVERIFY(!gate.allowWork(7,2000));
        QVERIFY(gate.allowWork(7,3500)); QVERIFY(gate.allowWork(7,5500));
        QVERIFY(!gate.allowWork(7,7500)); QVERIFY(gate.failed());
        QVERIFY(!gate.active(7500)); QVERIFY(gate.cleanupReady(7500));
        gate.observe(false,7,7600); QVERIFY(!gate.cleanupReady(9000));
        gate.observe(true,7,10000); gate.observe(true,7,11500);
        QVERIFY(!gate.active(11500)); QVERIFY(gate.cleanupReady(11500));
    }
    void explorerShowBurstCoolsDownThenFailsWithoutInfiniteFight() {
        IslandDesktop::TaskbarSessionControl gate;
        gate.observe(true,1,0); gate.observe(true,1,1500);
        QVERIFY(gate.allowHide(10,1500)); QVERIFY(!gate.allowHide(10,1550)); // Hide already queued.
        QVERIFY(gate.allowHide(10,1650)); QVERIFY(gate.allowHide(10,1800));
        QVERIFY(!gate.allowHide(10,1900)); QVERIFY(!gate.failed());
        gate.observe(true,1,4000); QVERIFY(gate.active(4000));
        QVERIFY(gate.allowHide(10,4000)); QVERIFY(gate.allowHide(10,4150)); QVERIFY(gate.allowHide(10,4300));
        QVERIFY(!gate.allowHide(10,4450)); QVERIFY(gate.failed());
        gate.observe(true,1,6500);
        QVERIFY(!gate.active(6500)); QVERIFY(gate.cleanupReady(6500));
    }
    void restoreOnlyHandoffRetainsMappingAndVisibilityOwnership() {
        const auto tag = marker(); Window window;
        Guardian original;
        QVERIFY(original.child.start(executable(),tag,{std::to_wstring(GetCurrentProcessId())}));
        QTRY_COMPARE(WaitForSingleObject(original.child.ready,0),DWORD(WAIT_OBJECT_0));
        QTRY_VERIFY_WITH_TIMEOUT(!IsWindowVisible(window.hwnd),5000);
        QVERIFY(TerminateProcess(original.child.process,99));
        QCOMPARE(WaitForSingleObject(original.child.process,3000),DWORD(WAIT_OBJECT_0));
        Guardian recovery;
        QVERIFY(recovery.child.start(executable(),tag,{std::to_wstring(GetCurrentProcessId())},true));
        original.child.close(); // Child inherited backing even before it runs guardianEntry.
        QTRY_COMPARE(WaitForSingleObject(recovery.child.ready,0),DWORD(WAIT_OBJECT_0));
        QTRY_VERIFY_WITH_TIMEOUT(IsWindowVisible(window.hwnd),5000);
        QTRY_COMPARE(WaitForSingleObject(recovery.child.process,0),DWORD(WAIT_OBJECT_0));
        QVERIFY(!GetPropW(window.hwnd,tag.c_str()));
    }
};

int main(int argc, char **argv) {
    if (argc >= 7 && std::string(argv[1]) == "--taskbar-guardian") {
        const auto owner = static_cast<DWORD>(std::strtoul(argv[6], nullptr, 10));
        const std::string raw(argv[5]); const std::wstring tag(raw.begin(), raw.end());
        return TaskbarNative::guardianEntry(argc, argv, policy(owner, tag));
    }
    if (argc == 4 && std::string(argv[1]) == "--test-taskbar-parent") {
        const std::string raw(argv[2]); const std::wstring tag(raw.begin(), raw.end());
        TaskbarNative::GuardianProcess guardian;
        if (!guardian.start(executable(), tag, {std::to_wstring(std::strtoul(argv[3], nullptr, 10))})
            || WaitForSingleObject(guardian.ready, 5000) != WAIT_OBJECT_0) return 1;
        std::printf("%lu\n", GetProcessId(guardian.process)); std::fflush(stdout);
        Sleep(INFINITE); // The test forcibly terminates only this simulated parent.
        return 0;
    }
    QCoreApplication app(argc, argv);
    WindowsTaskbarTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "windows_taskbar_test.moc"
