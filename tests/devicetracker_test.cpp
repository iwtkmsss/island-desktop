#include "devicetracker.h"
#include <QtTest>

namespace {
DeviceInfo dev(const QString &id, bool connected, const QString &name = QString(), int battery = -1,
    const QString &kind = QStringLiteral("other")) {
    DeviceInfo d;
    d.id = id;
    d.name = name.isEmpty() ? id : name;
    d.kind = kind;
    d.connected = connected;
    d.battery = battery;
    return d;
}
}

class DeviceTrackerTest final : public QObject {
    Q_OBJECT
private slots:
    void firstUpdateIsBaselineAndReturnsNothing() {
        DeviceTracker t;
        QVERIFY(t.update({dev("a", true), dev("b", false)}).isEmpty());
        QVERIFY(t.update({dev("a", true), dev("b", false)}).isEmpty());
    }
    void baselineOfEmptySnapshotStillCounts() {
        DeviceTracker t;
        QVERIFY(t.update({}).isEmpty());
        const auto c = t.update({dev("a", true)});
        QCOMPARE(c.size(), 1);
        QVERIFY(c[0].connected);
    }
    void connectTransitionIsReported() {
        DeviceTracker t;
        t.update({dev("a", false, "Buds")});
        const auto c = t.update({dev("a", true, "Buds", 80)});
        QCOMPARE(c.size(), 1);
        QCOMPARE(c[0].device.id, "a");
        QCOMPARE(c[0].device.name, "Buds");
        QCOMPARE(c[0].device.battery, 80);
        QVERIFY(c[0].connected);
    }
    void disconnectTransitionIsReported() {
        DeviceTracker t;
        t.update({dev("a", true, "Buds")});
        const auto c = t.update({dev("a", false, "Buds")});
        QCOMPARE(c.size(), 1);
        QVERIFY(!c[0].connected);
    }
    void unchangedStateProducesNothing() {
        DeviceTracker t;
        t.update({dev("a", true), dev("b", false)});
        QVERIFY(t.update({dev("a", true), dev("b", false)}).isEmpty());
    }
    void newDeviceAlreadyConnectedIsAConnect() {
        DeviceTracker t;
        t.update({dev("a", true)});
        const auto c = t.update({dev("a", true), dev("n", true, "New")});
        QCOMPARE(c.size(), 1);
        QCOMPARE(c[0].device.id, "n");
        QVERIFY(c[0].connected);
    }
    void newDeviceNotConnectedProducesNothing() {
        DeviceTracker t;
        t.update({dev("a", true)});
        QVERIFY(t.update({dev("a", true), dev("n", false)}).isEmpty());
    }
    void disappearedConnectedDeviceIsADisconnectWithLastKnownData() {
        DeviceTracker t;
        t.update({dev("a", true, "Old name", 50, "headphones")});
        t.update({dev("a", true, "Buds", 40, "headphones")});
        const auto c = t.update({});
        QCOMPARE(c.size(), 1);
        QVERIFY(!c[0].connected);
        QCOMPARE(c[0].device.name, "Buds");
        QCOMPARE(c[0].device.kind, "headphones");
        QCOMPARE(c[0].device.battery, 40);
        // Reported once only.
        QVERIFY(t.update({}).isEmpty());
    }
    void disappearedDisconnectedDeviceProducesNothing() {
        DeviceTracker t;
        t.update({dev("a", false)});
        QVERIFY(t.update({}).isEmpty());
    }
    void nameOrBatteryChangeAloneProducesNothing() {
        DeviceTracker t;
        t.update({dev("a", true, "X", 10)});
        QVERIFY(t.update({dev("a", true, "Y", 90)}).isEmpty());
    }
    void reconnectAfterDisconnectIsReportedAgain() {
        DeviceTracker t;
        t.update({dev("a", true)});
        QCOMPARE(t.update({dev("a", false)}).size(), 1);
        const auto c = t.update({dev("a", true)});
        QCOMPARE(c.size(), 1);
        QVERIFY(c[0].connected);
    }
    void duplicateIdsFirstWins() {
        DeviceTracker t;
        t.update({dev("a", false, "first"), dev("a", true, "second")});
        // Baseline recorded the first (disconnected) entry, so no change now.
        QVERIFY(t.update({dev("a", false, "first"), dev("a", true, "second")}).isEmpty());
        const auto c = t.update({dev("a", true, "first"), dev("a", false, "second")});
        QCOMPARE(c.size(), 1);
        QCOMPARE(c[0].device.name, "first");
        QVERIFY(c[0].connected);
    }
    void resetStartsANewBaseline() {
        DeviceTracker t;
        t.update({dev("a", false)});
        t.reset();
        QVERIFY(t.update({dev("a", true)}).isEmpty());
        QCOMPARE(t.update({dev("a", false)}).size(), 1);
    }
    void multipleChangesInOneSnapshot() {
        DeviceTracker t;
        t.update({dev("a", true), dev("b", false), dev("c", true)});
        const auto c = t.update({dev("a", false), dev("b", true)});  // c disappears
        QCOMPARE(c.size(), 3);
        QHash<QString, bool> byId;
        for (const auto &x : c) byId.insert(x.device.id, x.connected);
        QCOMPARE(byId.value("a"), false);
        QCOMPARE(byId.value("b"), true);
        QCOMPARE(byId.value("c"), false);
    }

    void kindFromClass_data() {
        QTest::addColumn<quint32>("cod");
        QTest::addColumn<QString>("kind");
        QTest::newRow("headset") << 0x240404u << "headphones";   // audio/video, wearable headset
        QTest::newRow("speaker") << 0x240414u << "headphones";
        QTest::newRow("keyboard") << 0x002540u << "keyboard";    // peripheral, keyboard
        QTest::newRow("combo") << 0x0025C0u << "keyboard";       // keyboard/pointing combo
        QTest::newRow("mouse") << 0x002580u << "mouse";          // peripheral, pointing
        QTest::newRow("gamepad") << 0x002508u << "gamepad";      // peripheral, gamepad
        QTest::newRow("joystick") << 0x002504u << "gamepad";
        QTest::newRow("phone") << 0x5A020Cu << "phone";          // smartphone
        QTest::newRow("computer") << 0x02010Cu << "computer";    // laptop
        QTest::newRow("uncategorized") << 0x1F00u << "other";
        QTest::newRow("zero") << 0u << "other";
        QTest::newRow("peripheral-other") << 0x002500u << "other";
    }
    void kindFromClass() {
        QFETCH(quint32, cod);
        QFETCH(QString, kind);
        QCOMPARE(deviceKindFromClass(cod), kind);
    }
};

QTEST_GUILESS_MAIN(DeviceTrackerTest)
#include "devicetracker_test.moc"
