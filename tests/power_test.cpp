#include "eventtext.h"
#include "powertracker.h"
#include <QtTest>

namespace {
PowerSnapshot battery(int percent) { return {true, false, percent}; }
PowerSnapshot ac(int percent) { return {true, true, percent}; }
PowerSnapshot none() { return {false, true, -1}; }
}

class PowerTest final : public QObject {
    Q_OBJECT
private slots:
    void firstSnapshotIsBaseline() {
        PowerTracker t;
        QCOMPARE(t.update(battery(50)), PowerEvent::None);
        QCOMPARE(t.update(battery(50)), PowerEvent::None);
    }
    void baselineAlreadyLowDoesNotFire() {
        PowerTracker t;
        QCOMPARE(t.update(battery(15)), PowerEvent::None);
        QCOMPARE(t.update(battery(14)), PowerEvent::None);
    }
    void noBatteryIsIgnoredAndResetsBaseline() {
        PowerTracker t;
        QCOMPARE(t.update(none()), PowerEvent::None);
        QCOMPARE(t.update(none()), PowerEvent::None);
        // A battery appearing later is a new baseline, even though it differs on AC.
        QCOMPARE(t.update(ac(80)), PowerEvent::None);
        QCOMPARE(t.update(none()), PowerEvent::None);
        QCOMPARE(t.update(battery(80)), PowerEvent::None);
        QCOMPARE(t.update(ac(80)), PowerEvent::Plugged);
    }
    void pluggedAndUnplugged() {
        PowerTracker t;
        t.update(ac(90));
        QCOMPARE(t.update(battery(90)), PowerEvent::Unplugged);
        QCOMPARE(t.update(battery(89)), PowerEvent::None);
        QCOMPARE(t.update(ac(89)), PowerEvent::Plugged);
        QCOMPARE(t.update(ac(90)), PowerEvent::None);
    }
    void lowFiresOnceOnCrossing() {
        PowerTracker t;
        t.update(battery(23));
        QCOMPARE(t.update(battery(21)), PowerEvent::None);
        QCOMPARE(t.update(battery(20)), PowerEvent::Low);
        QCOMPARE(t.update(battery(19)), PowerEvent::None);
        QCOMPARE(t.update(battery(18)), PowerEvent::None);
    }
    void criticalFiresOnceOnCrossing() {
        PowerTracker t;
        t.update(battery(12));
        QCOMPARE(t.update(battery(10)), PowerEvent::Critical);
        QCOMPARE(t.update(battery(9)), PowerEvent::None);
    }
    void jumpingPastBothThresholdsIsCritical() {
        PowerTracker t;
        t.update(battery(30));
        QCOMPARE(t.update(battery(8)), PowerEvent::Critical);
        QCOMPARE(t.update(battery(7)), PowerEvent::None);
    }
    void lowThenCriticalBothFire() {
        PowerTracker t;
        t.update(battery(22));
        QCOMPARE(t.update(battery(20)), PowerEvent::Low);
        QCOMPARE(t.update(battery(11)), PowerEvent::None);
        QCOMPARE(t.update(battery(10)), PowerEvent::Critical);
    }
    void rearmsOnlyAfterHysteresis() {
        PowerTracker t;
        t.update(battery(22));
        QCOMPARE(t.update(battery(20)), PowerEvent::Low);
        // Rising to 25 is not above threshold + 5.
        QCOMPARE(t.update(battery(25)), PowerEvent::None);
        QCOMPARE(t.update(battery(20)), PowerEvent::None);
        QCOMPARE(t.update(battery(26)), PowerEvent::None);
        QCOMPARE(t.update(battery(20)), PowerEvent::Low);
    }
    void criticalRearmsAfterHysteresis() {
        PowerTracker t;
        t.update(battery(12));
        QCOMPARE(t.update(battery(10)), PowerEvent::Critical);
        QCOMPARE(t.update(battery(15)), PowerEvent::None);
        QCOMPARE(t.update(battery(10)), PowerEvent::None);
        QCOMPARE(t.update(battery(16)), PowerEvent::None);
        QCOMPARE(t.update(battery(10)), PowerEvent::Critical);
    }
    void pluggingInRearms() {
        PowerTracker t;
        t.update(battery(22));
        QCOMPARE(t.update(battery(20)), PowerEvent::Low);
        QCOMPARE(t.update(ac(20)), PowerEvent::Plugged);
        QCOMPARE(t.update(battery(20)), PowerEvent::Unplugged);
        // The level was already low when unplugging, so it is not announced again...
        QCOMPARE(t.update(battery(19)), PowerEvent::None);
        // ...but a later genuine crossing is.
        QCOMPARE(t.update(ac(19)), PowerEvent::Plugged);
        QCOMPARE(t.update(ac(40)), PowerEvent::None);
        QCOMPARE(t.update(battery(40)), PowerEvent::Unplugged);
        QCOMPARE(t.update(battery(19)), PowerEvent::Low);
    }
    void acChangeWinsOverSimultaneousThreshold() {
        PowerTracker t;
        t.update(ac(30));
        QCOMPARE(t.update(battery(18)), PowerEvent::Unplugged);
        // Still applicable on the next update.
        QCOMPARE(t.update(battery(18)), PowerEvent::Low);
        QCOMPARE(t.update(battery(17)), PowerEvent::None);
    }
    void simultaneousPluggedDoesNotFireThreshold() {
        PowerTracker t;
        t.update(battery(30));
        QCOMPARE(t.update(ac(15)), PowerEvent::Plugged);
        QCOMPARE(t.update(ac(14)), PowerEvent::None);
    }
    void deferredThresholdDroppedWhenNoLongerApplicable() {
        PowerTracker t;
        t.update(ac(30));
        QCOMPARE(t.update(battery(18)), PowerEvent::Unplugged);
        QCOMPARE(t.update(ac(18)), PowerEvent::Plugged);
        QCOMPARE(t.update(ac(19)), PowerEvent::None);
    }
    void unknownPercentNeverFires() {
        PowerTracker t;
        t.update(battery(30));
        QCOMPARE(t.update(battery(-1)), PowerEvent::None);
        QCOMPARE(t.update(battery(-1)), PowerEvent::None);
        QCOMPARE(t.update(battery(5)), PowerEvent::None);
    }

    void pluggedText() {
        const auto e = powerIslandEvent(PowerEvent::Plugged, 64);
        QCOMPARE(e.title, QStringLiteral("Зарядку під'єднано — 64%"));
        QCOMPARE(e.icon, "plug");
        QCOMPARE(e.kind, "power");
        QCOMPARE(e.priority, 60);
        QCOMPARE(e.durationMs, 4000);
        QCOMPARE(e.coalesceKey, "power");
        QVERIFY(!e.transient);
        QCOMPARE(powerIslandEvent(PowerEvent::Plugged, -1).title, QStringLiteral("Зарядку під'єднано"));
    }
    void unpluggedText() {
        const auto e = powerIslandEvent(PowerEvent::Unplugged, 100);
        QCOMPARE(e.title, QStringLiteral("Зарядку від'єднано — 100%"));
        QCOMPARE(e.icon, "battery");
        QCOMPARE(e.kind, "power");
        QCOMPARE(e.priority, 60);
        QCOMPARE(e.durationMs, 4000);
        QCOMPARE(e.coalesceKey, "power");
        QCOMPARE(powerIslandEvent(PowerEvent::Unplugged, -1).title, QStringLiteral("Зарядку від'єднано"));
    }
    void lowText() {
        const auto e = powerIslandEvent(PowerEvent::Low, 20);
        QCOMPARE(e.title, QStringLiteral("Батарея 20%"));
        QCOMPARE(e.icon, "battery");
        QCOMPARE(e.kind, "power");
        QCOMPARE(e.priority, 80);
        QCOMPARE(e.durationMs, 5000);
        QCOMPARE(e.coalesceKey, "power");
        QVERIFY(!e.transient);
    }
    void criticalText() {
        const auto e = powerIslandEvent(PowerEvent::Critical, 10);
        QCOMPARE(e.title, QStringLiteral("Батарея 10% — підключіть зарядку"));
        QCOMPARE(e.icon, "battery");
        QCOMPARE(e.kind, "power");
        QCOMPARE(e.priority, 90);
        QCOMPARE(e.durationMs, 8000);
        QCOMPARE(e.coalesceKey, "power");
        QVERIFY(!e.transient);
    }
    void deviceConnectedWithBattery() {
        const auto e = deviceIslandEvent("id1", "Навушники", true, 80);
        QCOMPARE(e.title, QStringLiteral("Навушники підключено — 80%"));
        QCOMPARE(e.icon, "bluetooth");
        QCOMPARE(e.kind, "device");
        QCOMPARE(e.priority, 60);
        QCOMPARE(e.durationMs, 4000);
        QCOMPARE(e.coalesceKey, "device:id1");
        QVERIFY(!e.transient);
    }
    void deviceConnectedWithoutBattery() {
        QCOMPARE(deviceIslandEvent("id1", "Миша", true, -1).title, QStringLiteral("Миша підключено"));
    }
    void deviceDisconnected() {
        const auto e = deviceIslandEvent("id2", "Миша", false, 50);
        QCOMPARE(e.title, QStringLiteral("Миша відключено"));
        QCOMPARE(e.coalesceKey, "device:id2");
        QVERIFY(!e.transient);
    }
    void deviceEmptyName() {
        QCOMPARE(deviceIslandEvent("x", "", true, -1).title, QStringLiteral("Пристрій підключено"));
        QCOMPARE(deviceIslandEvent("x", "", false, -1).title, QStringLiteral("Пристрій відключено"));
        QCOMPARE(deviceIslandEvent("x", "", true, 7).title, QStringLiteral("Пристрій підключено — 7%"));
    }
};
QTEST_GUILESS_MAIN(PowerTest)
#include "power_test.moc"
