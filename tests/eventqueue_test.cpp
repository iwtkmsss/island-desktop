#include "eventqueue.h"
#include <QtTest>

namespace {
IslandEvent make(const QString &title, int priority, int durationMs = 1000, const QString &key = QString(),
    bool transient = false, const QString &kind = QStringLiteral("notice")) {
    IslandEvent e;
    e.kind = kind;
    e.title = title;
    e.priority = priority;
    e.durationMs = durationMs;
    e.coalesceKey = key;
    e.transient = transient;
    return e;
}
}

class EventQueueTest final : public QObject {
    Q_OBJECT
private slots:
    void emptyQueueHasNoCurrent() {
        EventQueue q;
        QVERIFY(q.current() == nullptr);
        QCOMPARE(q.deadline(), qint64(-1));
        QCOMPARE(q.pending(), 0);
        QVERIFY(!q.advance(100));
        QVERIFY(!q.dismiss(100));
    }
    void durationIsClampedAndFirstEventBecomesCurrent() {
        EventQueue q;
        QVERIFY(q.post(make("a", 50, -5), 100));
        QVERIFY(q.current() != nullptr);
        QCOMPARE(q.current()->title, "a");
        QCOMPARE(q.deadline(), qint64(101));
    }
    void sameCoalesceKeyReplacesCurrentAndRestartsDeadline() {
        EventQueue q;
        q.post(make("a", 10, 1000, "k", true), 0);
        QVERIFY(q.post(make("b", 10, 1000, "k", true), 500));
        QCOMPARE(q.current()->title, "b");
        QCOMPARE(q.deadline(), qint64(1500));
        QCOMPARE(q.pending(), 0);
    }
    void sameCoalesceKeyReplacesQueuedEvent() {
        EventQueue q;
        q.post(make("cur", 80), 0);
        q.post(make("a", 50, 1000, "k"), 0);
        q.post(make("b", 50, 1000, "k"), 0);
        QCOMPARE(q.pending(), 1);
        QVERIFY(q.dismiss(10));
        QCOMPARE(q.current()->title, "b");
    }
    void higherPriorityPreemptsAndDropsTransientCurrent() {
        EventQueue q;
        q.post(make("vol", 10, 1800, "volume", true), 0);
        QVERIFY(q.post(make("timer", 100, 8000), 100));
        QCOMPARE(q.current()->title, "timer");
        QCOMPARE(q.deadline(), qint64(8100));
        QCOMPARE(q.pending(), 0);
        QVERIFY(q.advance(8100));
        QVERIFY(q.current() == nullptr);
    }
    void preemptedNonTransientIsRequeuedFirstInItsPriority() {
        EventQueue q;
        q.post(make("cur", 50, 1000), 0);
        q.post(make("same", 50, 1000), 0);
        q.post(make("low", 20, 1000), 0);
        QVERIFY(q.post(make("high", 90, 500), 100));
        QCOMPARE(q.current()->title, "high");
        QCOMPARE(q.pending(), 3);
        QVERIFY(q.advance(600));
        QCOMPARE(q.current()->title, "cur");
        QCOMPARE(q.deadline(), qint64(1600)); // full duration again
        QVERIFY(q.advance(1600));
        QCOMPARE(q.current()->title, "same");
        QVERIFY(q.advance(2600));
        QCOMPARE(q.current()->title, "low");
    }
    void lowerOrEqualPriorityIsQueuedOrDropped() {
        EventQueue q;
        q.post(make("cur", 50), 0);
        QVERIFY(!q.post(make("equal", 50), 0));
        QCOMPARE(q.pending(), 1);
        QVERIFY(!q.post(make("transient", 10, 1000, QString(), true), 0));
        QCOMPARE(q.pending(), 1);
        QCOMPARE(q.current()->title, "cur");
    }
    void queueOrderIsPriorityDescendingThenFifo() {
        EventQueue q;
        q.post(make("cur", 100), 0);
        q.post(make("l1", 20), 0);
        q.post(make("m1", 40), 0);
        q.post(make("l2", 20), 0);
        q.post(make("m2", 40), 0);
        QStringList order;
        qint64 now = 0;
        while (q.dismiss(now)) {
            if (q.current())
                order << q.current()->title;
            ++now;
        }
        QCOMPARE(order, QStringList({"m1", "m2", "l1", "l2"}));
    }
    void expiryHappensExactlyAtDeadline() {
        EventQueue q;
        q.post(make("a", 50, 1000), 0);
        q.post(make("b", 50, 500), 0);
        QVERIFY(!q.advance(999));
        QCOMPARE(q.current()->title, "a");
        QVERIFY(q.advance(1000));
        QCOMPARE(q.current()->title, "b");
        QCOMPARE(q.deadline(), qint64(1500));
        QVERIFY(q.advance(1500));
        QVERIFY(q.current() == nullptr);
        QCOMPARE(q.deadline(), qint64(-1));
    }
    void overflowDropsOldestOfLowestPriority() {
        EventQueue q;
        q.post(make("cur", 100), 0);
        for (int i = 0; i < EventQueue::capacity; ++i)
            q.post(make(QString("n%1").arg(i), 50), 0);
        QCOMPARE(q.pending(), EventQueue::capacity);
        q.post(make("extra", 50), 0);
        QCOMPARE(q.pending(), EventQueue::capacity);
        q.dismiss(1);
        QCOMPARE(q.current()->title, "n1"); // n0 was the oldest lowest-priority one
        // Preempting re-queues n1 (queue is full again), then "minor" is the lowest and is dropped.
        QVERIFY(q.post(make("urgent", 70), 2));
        QCOMPARE(q.pending(), EventQueue::capacity);
        QVERIFY(!q.post(make("minor", 30), 2));
        QCOMPARE(q.pending(), EventQueue::capacity);
        QVERIFY(q.dismiss(3));
        QCOMPARE(q.current()->title, "n1");
        QVERIFY(q.removeKind("notice", 4));
        QVERIFY(q.current() == nullptr);
        QCOMPARE(q.pending(), 0);
    }
    void removeKindDropsCurrentAndQueued() {
        EventQueue q;
        q.post(make("t1", 100, 1000, QString(), false, "timer"), 0);
        q.post(make("n1", 50), 0);
        q.post(make("t2", 60, 1000, QString(), false, "timer"), 0);
        QVERIFY(q.removeKind("timer", 10));
        QCOMPARE(q.current()->title, "n1");
        QCOMPARE(q.deadline(), qint64(1010));
        QCOMPARE(q.pending(), 0);
    }
    void removeKindOfQueuedOnlyKeepsCurrent() {
        EventQueue q;
        q.post(make("n1", 50), 0);
        q.post(make("t", 40, 1000, QString(), false, "timer"), 0);
        QCOMPARE(q.pending(), 1);
        QVERIFY(!q.removeKind("timer", 5));
        QCOMPARE(q.current()->title, "n1");
        QCOMPARE(q.deadline(), qint64(1000));
        QCOMPARE(q.pending(), 0);
        QVERIFY(!q.removeKind("absent", 5));
    }
    void clearRemovesEverything() {
        EventQueue q;
        q.post(make("a", 50), 0);
        q.post(make("b", 50), 0);
        q.clear();
        QVERIFY(q.current() == nullptr);
        QCOMPARE(q.pending(), 0);
        QCOMPARE(q.deadline(), qint64(-1));
    }
};

QTEST_GUILESS_MAIN(EventQueueTest)
#include "eventqueue_test.moc"
