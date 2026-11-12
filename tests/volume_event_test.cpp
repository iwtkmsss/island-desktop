#include "volumechangetracker.h"
#include <QtTest>

class VolumeEventTest final : public QObject {
    Q_OBJECT
private slots:
    void startupDoesNotProduceFlyout() {
        VolumeChangeTracker tracker;
        QVERIFY(!tracker.observe("speakers", 42, false));
        QVERIFY(!tracker.observe("speakers", 42, false));
    }
    void levelAndMuteChangesAreEvents() {
        VolumeChangeTracker tracker;
        tracker.observe("speakers", 42, false);
        QVERIFY(tracker.observe("speakers", 44, false));
        QVERIFY(!tracker.observe("speakers", 44, false));
        QVERIFY(tracker.observe("speakers", 44, true));
        QVERIFY(tracker.observe("speakers", 44, false));
    }
    void switchingDefaultEndpointEstablishesNewBaseline() {
        VolumeChangeTracker tracker;
        tracker.observe("speakers", 42, false);
        QVERIFY(!tracker.observe("headphones", 15, true));
        QVERIFY(!tracker.observe("headphones", 15, true));
        QVERIFY(tracker.observe("headphones", 16, true));
    }
    void reconnectDoesNotReportTheDisconnectedLevelAsChange() {
        VolumeChangeTracker tracker;
        tracker.observe("speakers", 42, false);
        tracker.reset();
        QVERIFY(!tracker.observe("speakers", 77, true));
        QVERIFY(tracker.observe("speakers", 77, false));
    }
};

QTEST_GUILESS_MAIN(VolumeEventTest)
#include "volume_event_test.moc"
