#include "appstate.h"
#include "shellcontroller.h"
#include "kwinpointer.h"
#include <QCursor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingReply>
#include <QEnterEvent>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QQuickItem>
#include <QtGui/qguiapplication_platform.h>
#include <xcb/shape.h>
#include <cstdlib>
#include <QScreen>
#include <QTemporaryDir>
#include <QtTest>

class FakeScript final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Script")
public:
    QString endpoint;
    QPointF previous;
    quint64 movement = 0;
    void publish(QPointF p, const QDBusConnection &bus = QDBusConnection::sessionBus()) {
        if (p != previous) ++movement;
        previous = p;
        const auto geometry = QGuiApplication::primaryScreen()->geometry();
        const QJsonObject value{{"movement", qint64(movement)}, {"x", p.x()}, {"y", p.y()}, {"screen", QGuiApplication::primaryScreen()->name()},
            {"left", 0}, {"top", 0}, {"width", geometry.width()*2}, {"height", geometry.height()*2}};
        auto message = QDBusMessage::createMethodCall(QDBusConnection::sessionBus().baseService(), endpoint,
            "io.github.iwtkmsss.IslandDesktop.Pointer", "Update");
        message << QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact));
        bus.asyncCall(message);
    }
public slots:
    void run() { publish({200, 100}); }
};
class FakeScripting final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Scripting")
public:
    FakeScript script;
    QString loaded, code;
public slots:
    int loadScript(const QString &path, const QString &plugin) {
        loaded = plugin; QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return -1;
        code = QString::fromUtf8(file.readAll());
        const auto marker = code.indexOf("const objectPath = \"");
        if (marker < 0) return -1;
        script.endpoint = code.mid(marker).section('"', 1, 1);
        return 7;
    }
    bool unloadScript(const QString &plugin) { if (loaded == plugin) loaded.clear(); return true; }
};
class LinuxPointerTest final : public QObject {
    Q_OBJECT
private slots:
    void compositorUpdatesMappingAndCleanup() {
        auto bus = QDBusConnection::sessionBus();
        FakeScripting kwin;
        QVERIFY(bus.registerService("org.kde.KWin"));
        QVERIFY(bus.registerObject("/Scripting", &kwin, QDBusConnection::ExportAllSlots));
        QVERIFY(bus.registerObject("/Scripting/Script7", &kwin.script, QDBusConnection::ExportAllSlots));
        {
            KWinPointer pointer("isolated-pointer-test"); pointer.setEnabled(true);
            QTRY_VERIFY_WITH_TIMEOUT(pointer.available(), 3000);
            QCOMPARE(pointer.position(), QPointF(100, 50)); // Compositor output is twice Qt's logical size.
            QCOMPARE(pointer.movementSerial(), quint64(0));
            QVERIFY(kwin.code.contains("workspace.cursorPosChanged.connect"));
            QVERIFY(!kwin.code.contains("@SERVICE@"));
            kwin.script.publish({400, 300});
            QTRY_COMPARE(pointer.movementSerial(), quint64(1));
            QCOMPARE(pointer.position(), QPointF(200, 150));
            kwin.script.publish({400, 300}); QTest::qWait(60);
            QCOMPARE(pointer.movementSerial(), quint64(1));
            ++kwin.script.movement; // Coalesced round-trip: cursor ends at the same point.
            kwin.script.publish({400, 300}); QTRY_COMPARE(pointer.movementSerial(), quint64(2));
            auto outsider = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "outsider");
            kwin.script.publish({10, 10}, outsider); QTest::qWait(60);
            QCOMPARE(pointer.position(), QPointF(200, 150));
            QDBusConnection::disconnectFromBus("outsider");
            pointer.setEnabled(false); QVERIFY(!pointer.available()); QVERIFY(kwin.loaded.isEmpty());
            pointer.setEnabled(true); QTRY_VERIFY_WITH_TIMEOUT(pointer.available(), 3000);
            QVERIFY(bus.unregisterService("org.kde.KWin"));
            QTRY_VERIFY(!pointer.available());
            QVERIFY(bus.registerService("org.kde.KWin"));
            QTRY_VERIFY_WITH_TIMEOUT(pointer.available(), 3000);
        }
        QVERIFY(kwin.loaded.isEmpty());
        bus.unregisterObject("/Scripting"); bus.unregisterObject("/Scripting/Script7"); bus.unregisterService("org.kde.KWin");
    }
    void crownAcceptsDoubleClickOutsideRectangularBody() {
        QTemporaryDir directory; AppState state(directory.filePath("crown.ini"));
        QQuickWindow window;window.setGeometry(0,0,340,220);
        window.setProperty("shellX",20.0);window.setProperty("shellY",20.0);
        window.setProperty("shellWidth",300.0);window.setProperty("shellHeight",160.0);
        window.setProperty("crownHeight",10.0);
        ShellController shell(&state);shell.attach(&window);window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::mouseDClick(&window,Qt::LeftButton,Qt::NoModifier,{170,14});
        QVERIFY(state.passThroughActive());
    }
    void interactiveControlsDoNotStartPassThrough() {
        QTemporaryDir directory; AppState state(directory.filePath("controls.ini"));
        QQuickWindow window; window.setGeometry(0,0,300,200);
        window.setProperty("shellX",0.0);window.setProperty("shellY",0.0);
        window.setProperty("shellWidth",300.0);window.setProperty("shellHeight",200.0);
        auto *control=new QQuickItem(window.contentItem());control->setPosition({10,10});control->setSize({100,100});
        control->setProperty("islandInteractive",true);
        auto *decoration=new QQuickItem(control);decoration->setSize({80,80});
        ShellController shell(&state);shell.attach(&window);window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::mouseDClick(&window,Qt::LeftButton,Qt::NoModifier,{40,40});
        QVERIFY(!state.passThroughActive());
        QTest::mouseDClick(&window,Qt::LeftButton,Qt::NoModifier,{150,150});
        QVERIFY(state.passThroughActive());
    }
    void doubleClickPassThroughMasksNativeInputAndRestores() {
        QTemporaryDir directory; AppState state(directory.filePath("gesture.ini"));
        state.setSection("home"); state.setPassThroughSeconds(1);
        QQuickWindow window; window.setFlags(Qt::Tool | Qt::FramelessWindowHint);
        window.setGeometry(200,100,300,200);
        window.setProperty("pointerBridgeKey", "native-pass-through-test");
        window.setProperty("shellX", 20.0); window.setProperty("shellY", 16.0);
        window.setProperty("shellWidth", 148.0); window.setProperty("shellHeight", 50.0);
        ShellController shell(&state); shell.attach(&window); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto inputIsEmpty = [&] {
            if (QGuiApplication::platformName() == "xcb") {
                auto *native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
                if (!native || !native->connection()) return false;
                auto *reply = xcb_shape_get_rectangles_reply(native->connection(),
                    xcb_shape_get_rectangles(native->connection(), window.winId(), XCB_SHAPE_SK_INPUT), nullptr);
                if (!reply) return false;
                const bool empty = xcb_shape_get_rectangles_rectangles_length(reply) == 0;
                std::free(reply); return empty;
            }
            return window.mask().intersected(QRegion(QRect(0,0,window.width(),window.height()))).isEmpty();
        };
        QVERIFY(!inputIsEmpty());
        QTest::mouseDClick(&window, Qt::LeftButton, Qt::NoModifier, {2,2});
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, {2,2});
        QVERIFY(!state.passThroughActive()); // Transparent frame outside the capsule.
        shell.setPinned(true); QVERIFY(state.expanded());
        QTest::mouseDClick(&window, Qt::LeftButton, Qt::NoModifier, {70,40});
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, {70,40});
        QVERIFY(state.passThroughActive()); QVERIFY(!state.expanded()); QVERIFY(!shell.pinned());
        QVERIFY(inputIsEmpty());
        shell.open(); shell.setPinned(true);
        QVERIFY(!state.expanded()); QVERIFY(!shell.pinned());
        QTRY_VERIFY_WITH_TIMEOUT(!state.passThroughActive(), 1800);
        QVERIFY(!inputIsEmpty()); QVERIFY(!state.expanded());
        QTest::mouseDClick(&window, Qt::LeftButton, Qt::NoModifier, {70,40});
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, {70,40});
        QVERIFY(state.passThroughActive()); window.hide();
        QTRY_VERIFY_WITH_TIMEOUT(!state.passThroughActive(), 1800);
        QVERIFY(!window.isVisible()); // Expiration never undoes a manual tray hide.
        QCOMPARE(state.section(), "home");
    }

    void nativeLeaveClosesEvenIfXWaylandCoordinatesFreeze() {
        QCOMPARE(QGuiApplication::platformName(), "offscreen"); // No physical cursor movement.
        QTemporaryDir directory; AppState state(directory.filePath("settings.ini"));
        state.setSection("home");
        QQuickWindow window; window.setGeometry(200, 100, 300, 200);
        window.setProperty("shellX", 20.0); window.setProperty("shellWidth", 148.0); window.setProperty("shellHeight", 50.0);
        ShellController shell(&state); shell.attach(&window); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCursor::setPos(250, 140);
        const auto enter = [&] {
            QEnterEvent event({50,40}, {50,40}, {250,140}); QCoreApplication::sendEvent(&window, &event);
        };
        const auto leave = [&] { QEvent event(QEvent::Leave); QCoreApplication::sendEvent(&window, &event); };
        enter(); QTRY_VERIFY_WITH_TIMEOUT(state.expanded(), 1200);
        const auto selection = state.section();
        leave(); // QCursor intentionally retains the last in-island coordinates.
        QTRY_VERIFY_WITH_TIMEOUT(!state.expanded(), 1800);
        QCOMPARE(state.section(), selection);
        enter(); QTRY_VERIFY_WITH_TIMEOUT(state.expanded(), 1200);
        shell.setPinned(true); leave(); QTest::qWait(850); QVERIFY(state.expanded());
        shell.setPinned(false); QTRY_VERIFY_WITH_TIMEOUT(!state.expanded(), 1800);
        enter(); QTRY_VERIFY_WITH_TIMEOUT(state.expanded(), 1200);
        shell.close(); QTest::qWait(450); QVERIFY(!state.expanded()); // Escape-style close remains guarded.
        leave(); enter(); QTRY_VERIFY_WITH_TIMEOUT(state.expanded(), 1200); // Crossing between polls still releases the guard.
        shell.close(); leave();
        shell.open(); enter(); leave();
        QTRY_VERIFY_WITH_TIMEOUT(!state.expanded(), 1000); // Arrival cancels explicit-open travel grace.
    }
};
QTEST_MAIN(LinuxPointerTest)
#include "linux_pointer_test.moc"
