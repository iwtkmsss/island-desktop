#include "cursorgaze.h"
#include <QtTest>

class CursorGazeTest : public QObject {
    Q_OBJECT
private slots:
    void directionAndScreenScale() {
        CursorGaze gaze;
        const QPointF center(1000, 500);
        const QSizeF screen(2000, 1000);
        QVERIFY(!gaze.sample(center, center, screen));
        QCOMPARE(gaze.activity(), quint64(0));
        QVERIFY(gaze.sample({1250, 375}, center, screen));
        QCOMPARE(gaze.x(), qreal(0.5));
        QCOMPARE(gaze.y(), qreal(-0.5));
        QVERIFY(gaze.sample({750, 625}, center, screen));
        QCOMPARE(gaze.x(), qreal(-0.5));
        QCOMPARE(gaze.y(), qreal(0.5));
        QCOMPARE(gaze.activity(), quint64(2));
    }

    void farMovementStillNotifiesAtSaturation() {
        CursorGaze gaze;
        const QPointF center(500, 100);
        const QSizeF screen(1920, 1080);
        QVERIFY(gaze.sample({2000, 1000}, center, screen));
        QCOMPARE(gaze.activity(), quint64(0));
        for (int step = 1; step <= 150; ++step) {
            // 12 seconds of far-away movement at the 80 ms polling interval.
            QVERIFY(gaze.sample({qreal(2000 + step), 1000}, center, screen));
            QVERIFY(gaze.x() > 0 && gaze.y() > 0);
            QVERIFY(std::abs(std::hypot(gaze.x(), gaze.y()) - 1) < 0.00001);
            QCOMPARE(gaze.activity(), quint64(step));
        }
        QVERIFY(gaze.sample({-2000, -1000}, center, screen));
        QVERIFY(gaze.x() < 0 && gaze.y() < 0);
    }

    void farCornerStillTracksChangingDirection() {
        CursorGaze gaze;
        gaze.sample({2500, 1500}, {1000, 500}, {2000, 1000});
        const auto beforeX = gaze.x(), beforeY = gaze.y();
        QVERIFY(gaze.sample({3500, 1500}, {1000, 500}, {2000, 1000}));
        QVERIFY(gaze.x() > beforeX);
        QVERIFY(gaze.y() < beforeY);
        QCOMPARE(gaze.activity(), quint64(1));
    }

    void stationaryDoesNotKeepEyesAwake() {
        CursorGaze gaze;
        gaze.sample({1500, 600}, {500, 100}, {1920, 1080});
        for (int poll = 0; poll < 150; ++poll)
            QVERIFY(!gaze.sample({1500, 600}, {500, 100}, {1920, 1080}));
        QCOMPARE(gaze.activity(), quint64(0));
    }

    void geometryAndMonitorChangesDoNotFakeActivity() {
        CursorGaze gaze;
        gaze.sample({700, 200}, {500, 100}, {1920, 1080});
        QVERIFY(gaze.sample({700, 200}, {600, 150}, {1920, 1080}));
        QCOMPARE(gaze.activity(), quint64(0));
        QVERIFY(gaze.sample({700, 200}, {600, 150}, {2560, 1440}));
        QCOMPARE(gaze.activity(), quint64(0));
        QVERIFY(gaze.sample({701, 200}, {600, 150}, {2560, 1440}));
        QCOMPARE(gaze.activity(), quint64(1));
    }

    void resetIgnoresMovementWhileHiddenAndKeepsSerial() {
        CursorGaze gaze;
        gaze.sample({500, 100}, {500, 100}, {1920, 1080});
        gaze.sample({600, 200}, {500, 100}, {1920, 1080});
        QVERIFY(gaze.reset());
        QCOMPARE(gaze.x(), qreal(0));
        QCOMPARE(gaze.y(), qreal(0));
        QCOMPARE(gaze.activity(), quint64(1));
        QVERIFY(!gaze.reset());
        gaze.sample({2000, 1200}, {500, 100}, {1920, 1080});
        QCOMPARE(gaze.activity(), quint64(1));
        QVERIFY(!gaze.sample({2000, 1200}, {500, 100}, {1920, 1080}));
        QVERIFY(gaze.sample({2001, 1200}, {500, 100}, {1920, 1080}));
        QCOMPARE(gaze.activity(), quint64(2));
    }
};

QTEST_GUILESS_MAIN(CursorGazeTest)
#include "cursorgaze_test.moc"
