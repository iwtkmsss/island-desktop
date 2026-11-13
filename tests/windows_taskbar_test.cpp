#include "taskbarcontroller.h"
#include "taskbarguardian.h"
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <cstdio>
#include <cstdlib>

namespace {
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
