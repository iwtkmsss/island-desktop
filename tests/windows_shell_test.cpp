#include "appstate.h"
#include "shellcontroller.h"
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <windows.h>

class WindowsShellTest final : public QObject {
    Q_OBJECT
    static HWND prepare(QQuickWindow &window, ShellController &shell) {
        window.setGeometry(0, 0, 300, 200);
        window.setProperty("shellX", 0.0); window.setProperty("shellY", 0.0);
        window.setProperty("shellWidth", 300.0); window.setProperty("shellHeight", 200.0);
        const auto hwnd = reinterpret_cast<HWND>(window.winId());
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & ~LONG_PTR(WS_EX_TRANSPARENT));
        SetWindowRgn(hwnd, CreateRectRgn(0, 0, 300, 200), FALSE);
        shell.attach(&window);
        // The native policy is driven explicitly: no cursor movement or foreign
        // fullscreen state participates, and no real window needs to be shown.
        shell.ticker_.stop();
        return hwnd;
    }
    static int regionKind(HWND hwnd) {
        auto region = CreateRectRgn(0, 0, 0, 0);
        const auto kind = GetWindowRgn(hwnd, region);
        DeleteObject(region);
        return kind;
    }
    static bool transparent(HWND hwnd) { return (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0; }
    static void doubleClick(QQuickWindow &window) {
        QMouseEvent event(QEvent::MouseButtonDblClick, QPointF(150, 100), QPointF(150, 100),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &event);
    }
private slots:
    void interactiveDescendantsUnderDecoration_data() {
        QTest::addColumn<bool>("visible"); QTest::addColumn<bool>("enabled");
        QTest::addColumn<qreal>("opacity"); QTest::addColumn<bool>("clipped");
        QTest::addColumn<bool>("transformed"); QTest::addColumn<bool>("expectPassThrough");
        QTest::newRow("zero-size-container-overflow") << true << true << qreal(1) << false << false << false;
        QTest::newRow("transformed-control") << true << true << qreal(1) << false << true << false;
        QTest::newRow("hidden-ancestor") << false << true << qreal(1) << false << false << true;
        QTest::newRow("disabled-ancestor") << true << false << qreal(1) << false << false << true;
        QTest::newRow("transparent-ancestor") << true << true << qreal(0) << false << false << true;
        QTest::newRow("clipped-overflow") << true << true << qreal(1) << true << false << true;
    }
    void interactiveDescendantsUnderDecoration() {
        QFETCH(bool, visible); QFETCH(bool, enabled); QFETCH(qreal, opacity);
        QFETCH(bool, clipped); QFETCH(bool, transformed); QFETCH(bool, expectPassThrough);
        QTemporaryDir directory; AppState state(directory.filePath("controls.ini"));
        QQuickWindow window; ShellController shell(&state);
        prepare(window, shell);
        // Deliberately zero-sized: an overflowing child remains interactive
        // unless the container clips, including through a scene transform.
        auto *container = new QQuickItem(window.contentItem());
        container->setVisible(visible); container->setEnabled(enabled);
        container->setOpacity(opacity); container->setClip(clipped);
        auto *control = new QQuickItem(container);
        control->setPosition({120, 70}); control->setSize({60, 60});
        control->setProperty("islandInteractive", true);
        if (transformed) {
            container->setPosition({300, -50}); container->setScale(1.5); container->setRotation(90);
            // The transformed control contains scene (150,100).
            control->setPosition({80, 80});
        }
        auto *decoration = new QQuickItem(window.contentItem());
        decoration->setSize({300, 200}); decoration->setZ(100);
        shell.setPinned(true);
        doubleClick(window);
        QCOMPARE(state.passThroughActive(), expectPassThrough);
        QCOMPARE(state.expanded(), !expectPassThrough);
        QCOMPARE(shell.pinned(), !expectPassThrough);
    }
    void doubleClickPreservesDrawingAndExpiresWhileHidden() {
        QTemporaryDir directory; AppState state(directory.filePath("pass.ini"));
        state.setPassThroughSeconds(1); state.setSection("home");
        QQuickWindow window; ShellController shell(&state);
        const auto hwnd = prepare(window, shell);
        shell.setPinned(true);
        doubleClick(window);
        QVERIFY(state.passThroughActive()); QVERIFY(!state.expanded()); QVERIFY(!shell.pinned());
        QVERIFY(transparent(hwnd)); QCOMPARE(regionKind(hwnd), SIMPLEREGION);
        shell.open(); shell.setPinned(true);
        QVERIFY(!state.expanded()); QVERIFY(!shell.pinned());
        window.hide();
        QTRY_VERIFY_WITH_TIMEOUT(!state.passThroughActive(), 1800);
        QVERIFY(!window.isVisible()); QVERIFY(!state.expanded());
        QVERIFY(!transparent(hwnd)); QCOMPARE(regionKind(hwnd), SIMPLEREGION);
        QCOMPARE(state.section(), QStringLiteral("home"));
    }
    void fullscreenExitDuringPassThroughRestoresDrawingOnly() {
        QTemporaryDir directory; AppState state(directory.filePath("overlap.ini"));
        state.setPassThroughSeconds(1);
        QQuickWindow window; ShellController shell(&state);
        const auto hwnd = prepare(window, shell);
        state.startPassThrough();
        shell.setSuppressed(true);
        QCOMPARE(regionKind(hwnd), NULLREGION); QVERIFY(transparent(hwnd));
        shell.setSuppressed(false);
        QVERIFY(state.passThroughActive());
        QCOMPARE(regionKind(hwnd), SIMPLEREGION); QVERIFY(transparent(hwnd));
        QTRY_VERIFY_WITH_TIMEOUT(!state.passThroughActive(), 1800);
        QCOMPARE(regionKind(hwnd), SIMPLEREGION); QVERIFY(!transparent(hwnd));
    }
    void expiryDuringFullscreenDoesNotRevealSurface() {
        QTemporaryDir directory; AppState state(directory.filePath("fullscreen.ini"));
        state.setPassThroughSeconds(1);
        QQuickWindow window; ShellController shell(&state);
        const auto hwnd = prepare(window, shell);
        shell.setSuppressed(true);
        state.startPassThrough();
        QTRY_VERIFY_WITH_TIMEOUT(!state.passThroughActive(), 1800);
        QVERIFY(shell.suppressed()); QCOMPARE(regionKind(hwnd), NULLREGION); QVERIFY(transparent(hwnd));
        shell.setSuppressed(false);
        QCOMPARE(regionKind(hwnd), SIMPLEREGION); QVERIFY(!transparent(hwnd));
    }
};

QTEST_MAIN(WindowsShellTest)
#include "windows_shell_test.moc"
