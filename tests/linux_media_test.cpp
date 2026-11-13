#include "mediacontroller.h"
#include "startupcontroller.h"
#include "bluetoothcontroller.h"
#include <QDBusMetaType>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>

class TestPlayer final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ status)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double Rate READ rate)
    Q_PROPERTY(bool CanControl READ yes)
    Q_PROPERTY(bool CanPlay READ yes)
    Q_PROPERTY(bool CanPause READ yes)
    Q_PROPERTY(bool CanGoNext READ yes)
    Q_PROPERTY(bool CanGoPrevious READ yes)
    Q_PROPERTY(bool CanSeek READ yes)
public:
    bool playing = true; int commands = 0; qlonglong seekPosition = -1;
    QString seekTrack;
    QString status() const { return playing ? "Playing" : "Paused"; }
    QVariantMap metadata() const { return {{"xesam:title", "Linux test track"}, {"xesam:artist", QStringList{"Test artist"}},
        {"mpris:length", qlonglong(120000000)}, {"mpris:trackid", QVariant::fromValue(QDBusObjectPath("/test/track"))}}; }
    qlonglong position() const { return 12000000; }
    double rate() const { return 1; }
    bool yes() const { return true; }
public slots:
    void PlayPause() { ++commands; playing = !playing; }
    void Next() { ++commands; }
    void Previous() { ++commands; }
    void SetPosition(const QDBusObjectPath &track, qlonglong position) { ++commands; seekTrack = track.path(); seekPosition = position; }
};
using TestInterfaces = QMap<QString, QVariantMap>;
using TestObjects = QMap<QDBusObjectPath, TestInterfaces>;
Q_DECLARE_METATYPE(TestInterfaces)
Q_DECLARE_METATYPE(TestObjects)
class TestBluez final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.ObjectManager")
public:
    bool connected = false;
public slots:
    TestObjects GetManagedObjects() const {
        return {{QDBusObjectPath("/org/bluez/hci0/dev_00_11_22_33_44_55"),
            {{"org.bluez.Device1", {{"Alias", "Test headphones"}, {"Paired", true},
                {"Connected", connected}, {"Class", quint32(0x0400)}}},
             {"org.bluez.Battery1", {{"Percentage", quint8(74)}}}}}};
    }
};
class LinuxMediaTest final : public QObject {
    Q_OBJECT
private slots:
    void mprisCommandsAndSourcePersistence() {
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.isConnected());
        TestPlayer player;
        const QString service = "org.mpris.MediaPlayer2.island_test";
        QVERIFY(bus.registerService(service));
        QVERIFY(bus.registerObject("/org/mpris/MediaPlayer2", &player, QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSlots));
        QTemporaryDir dir;
        const auto file = dir.filePath("settings.ini");
        {
            MediaController media(file);
            QTRY_VERIFY_WITH_TIMEOUT(media.available(), 6000);
            QCOMPARE(media.title(), "Linux test track"); QCOMPARE(media.artist(), "Test artist");
            QCOMPARE(media.durationMs(), 120000); QVERIFY(media.playing()); QVERIFY(media.canSeek());
            media.togglePlayback(); QTRY_COMPARE(player.commands, 1); QTRY_VERIFY(!media.busy());
            QTRY_VERIFY(!media.playing());
            media.seek(0.5); QTRY_COMPARE(player.seekPosition, 60000000); QCOMPARE(player.seekTrack, "/test/track");
            QTRY_VERIFY(!media.busy());
            media.next(); QTRY_COMPARE(player.commands, 3); QTRY_VERIFY(!media.busy());
            media.previous(); QTRY_COMPARE(player.commands, 4); QTRY_VERIFY(!media.busy());
            media.selectSource("org.mpris.MediaPlayer2.missing");
            QVERIFY(!media.available()); QVERIFY(media.title().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!media.busy(), 6000); QVERIFY(!media.available());
            QVERIFY(!media.error().isEmpty());
        }
        {
            MediaController media(file);
            QCOMPARE(media.selectedSource(), "org.mpris.MediaPlayer2.missing");
            QTest::qWait(1200); QVERIFY(!media.available());
            media.selectSource(service); QTRY_VERIFY_WITH_TIMEOUT(media.available(), 6000);
            bus.unregisterService(service); QTRY_VERIFY_WITH_TIMEOUT(!media.available(), 6000);
            QVERIFY(!media.title().size());
        }
        bus.unregisterObject("/org/mpris/MediaPlayer2");
    }
    void bluezBaselineEventsAndReconnect() {
        qDBusRegisterMetaType<TestInterfaces>(); qDBusRegisterMetaType<TestObjects>();
        // test-dbus.sh points the system bus role to our private dbus-run-session.
        QCOMPARE(qEnvironmentVariable("DBUS_SYSTEM_BUS_ADDRESS"), qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"));
        auto bus = QDBusConnection::systemBus();
        TestBluez bluez;
        QVERIFY(bus.registerService("org.bluez"));
        QVERIFY(bus.registerObject("/", &bluez, QDBusConnection::ExportAllSlots));
        {
            BluetoothController bluetooth;
            QSignalSpy events(&bluetooth, &BluetoothController::deviceEvent);
            QTRY_VERIFY_WITH_TIMEOUT(bluetooth.available(), 4000);
            QCOMPARE(bluetooth.devices().size(), 1); QCOMPARE(events.count(), 0);
            QCOMPARE(bluetooth.connectedCount(), 0);
            bluez.connected = true; bluetooth.refresh();
            QTRY_COMPARE_WITH_TIMEOUT(bluetooth.connectedCount(), 1, 4000);
            QCOMPARE(events.count(), 1);
            const auto device = bluetooth.devices().first().toMap();
            QCOMPARE(device.value("battery").toInt(), 74);
            QCOMPARE(device.value("kind").toString(), "headphones");
            QVERIFY(bus.unregisterService("org.bluez")); bluetooth.refresh();
            QTRY_VERIFY_WITH_TIMEOUT(!bluetooth.available(), 4000);
            QVERIFY(bluetooth.devices().isEmpty()); QCOMPARE(events.count(), 1);
            QVERIFY(bus.registerService("org.bluez")); bluetooth.refresh();
            QTRY_COMPARE_WITH_TIMEOUT(bluetooth.connectedCount(), 1, 4000);
            QCOMPARE(events.count(), 1); // Reconnection establishes a fresh baseline.
        }
        bus.unregisterObject("/"); bus.unregisterService("org.bluez");
    }
    void autostartIsOptInAndPersistent() {
        QTemporaryDir dir;
        const auto path = dir.filePath("autostart/island.desktop");
        StartupController startup(path);
        QVERIFY(startup.available()); QVERIFY(!startup.enabled()); QVERIFY(!QFile::exists(path));
        startup.setEnabled(true); QVERIFY(startup.enabled()); QVERIFY(startup.error().isEmpty());
        StartupController restored(path); QVERIFY(restored.enabled());
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto content = file.readAll(); QVERIFY(content.contains("[Desktop Entry]")); QVERIFY(content.contains("Exec=\""));
        file.close(); startup.setEnabled(false); QVERIFY(!startup.enabled()); QVERIFY(!QFile::exists(path));
        startup.setEnabled(false); QVERIFY(startup.error().isEmpty());
    }
};
QTEST_GUILESS_MAIN(LinuxMediaTest)
#include "linux_media_test.moc"
