#include "tasklist.h"
#include <QtTest>

using IslandDesktop::TaskList;
using IslandDesktop::TaskWindow;
using Kind = TaskList::ActionKind;

namespace {
TaskWindow window(quintptr id, const QString &app, bool minimized = false, const QString &title = QString()) {
    return {id, app, title.isEmpty() ? QStringLiteral("w%1").arg(id) : title, minimized};
}
QStringList keys(const TaskList &list) {
    QStringList result;
    for (int r = 0; r < list.rowCount(); ++r) result.append(list.row(r)->appKey);
    return result;
}
bool isLauncher(const TaskList &list, int row) {
    return list.row(row) && list.row(row)->pinned && list.row(row)->windows.isEmpty();
}
struct Recorder : TaskList::Observer {
    QStringList log;
    void beginReset() override { log << "reset"; }
    void beginInsert(int first, int last) override { log << QStringLiteral("insert %1 %2").arg(first).arg(last); }
    void beginRemove(int row) override { log << QStringLiteral("remove %1").arg(row); }
    void beginMove(int from, int to) override { log << QStringLiteral("move %1 %2").arg(from).arg(to); }
    void rowChanged(int row) override { log << QStringLiteral("changed %1").arg(row); }
};
}

class TaskListTest final : public QObject {
    Q_OBJECT
private slots:
    void windowClickMinimizesRestoresAndLaunches() {
        TaskList list;
        list.setLaunchers({"exe:a"});
        auto action = list.click(0, 0);
        QCOMPARE(action.kind, Kind::Launch);
        QCOMPARE(action.appKey, QString("exe:a"));
        QCOMPARE(list.state(0, 0).count, 0);
        QCOMPARE(list.state(0, 0).active, -1);

        list.update({window(1, "exe:a")});
        QVERIFY(!isLauncher(list, 0));
        // Active (also when the island displaced it: the controller passes it) -> minimize.
        action = list.click(0, 1);
        QCOMPARE(action.kind, Kind::Minimize);
        QCOMPARE(action.window, quintptr(1));
        QCOMPARE(list.state(0, 1).count, 1);
        QCOMPARE(list.state(0, 1).active, 0);
        // Not active -> activate.
        action = list.click(0, 0);
        QCOMPARE(action.kind, Kind::Activate);
        QCOMPARE(action.window, quintptr(1));
        QCOMPARE(list.click(0, 99).kind, Kind::Activate);
        QCOMPARE(list.state(0, 99).active, -1);
        // Minimized -> restore, regardless of the remembered active window.
        list.update({window(1, "exe:a", true)});
        QCOMPARE(list.click(0, 0).kind, Kind::Restore);
        QCOMPARE(list.click(0, 1).kind, Kind::Restore);
        // Invalid rows do nothing.
        QCOMPARE(list.click(-1, 0).kind, Kind::None);
        QCOMPARE(list.click(5, 0).kind, Kind::None);
        QCOMPARE(list.state(5, 0).count, 0);
        // Closing the window turns the pinned row back into a launcher.
        list.update({});
        QVERIFY(isLauncher(list, 0));
        QCOMPARE(list.click(0, 1).kind, Kind::Launch);
    }

    void groupClickUsesRecentThenCyclesWithoutMenu() {
        TaskList list;
        list.update({window(10, "app"), window(11, "app"), window(12, "app")});
        QCOMPARE(list.rowCount(), 1);
        const QList<quintptr> order{10, 11, 12};
        QCOMPARE(list.row(0)->windows, order);
        list.noteActivated(12);
        list.noteActivated(11);
        auto action = list.click(0, 0);
        QCOMPARE(action.kind, Kind::Activate);
        QCOMPARE(action.window, quintptr(11));  // most recently activated
        for (const quintptr active : {11, 12, 10, 11}) {
            // MRU changes must not reorder the cycle while a window is active.
            list.noteActivated(active);
            action = list.click(0, active);
            QCOMPARE(action.kind, Kind::Activate);
            QCOMPARE(action.window, order[(order.indexOf(active) + 1) % 3]);
            QCOMPARE(list.state(0, active).active, int(order.indexOf(active)));
        }
        // Z-order changes do not reorder the cycle either.
        list.update({window(12, "app"), window(11, "app"), window(10, "app")});
        QCOMPARE(list.row(0)->windows, order);
        // Focus moved to the island: the remembered window continues the cycle.
        QCOMPARE(list.click(0, 10).window, quintptr(11));
        // Nothing remembered and nothing active: the most recent one again.
        QCOMPARE(list.click(0, 0).window, quintptr(11));
        QCOMPARE(list.state(0, 0).count, 3);

        // Never activated: fall back to the top-most window in z-order.
        TaskList fresh;
        fresh.update({window(10, "app"), window(11, "app"), window(12, "app")});
        QCOMPARE(fresh.click(0, 0).window, quintptr(10));
        fresh.update({window(12, "app"), window(10, "app"), window(11, "app")});
        QCOMPARE(fresh.click(0, 0).window, quintptr(12));
        // Shrinking groups.
        fresh.update({window(11, "app"), window(10, "app")});
        QCOMPARE(fresh.click(0, 0).window, quintptr(11));
        QCOMPARE(fresh.click(0, 11).window, quintptr(10));
        fresh.update({window(11, "app")});
        QCOMPARE(fresh.click(0, 0).kind, Kind::Activate);
        QCOMPARE(fresh.click(0, 11).kind, Kind::Minimize);
        fresh.update({});
        QCOMPARE(fresh.rowCount(), 0);
        QCOMPARE(fresh.click(0, 0).kind, Kind::None);
        // A remembered activation of a closed window is forgotten.
        TaskList recent;
        recent.update({window(1, "app"), window(2, "app")});
        recent.noteActivated(2);
        recent.update({window(1, "app")});
        recent.update({window(3, "app"), window(1, "app")});
        QCOMPARE(recent.click(0, 0).window, quintptr(3));
        recent.noteActivated(99);  // unknown windows are ignored
        QCOMPARE(recent.click(0, 0).window, quintptr(3));
    }

    void launcherOrderSurvivesRestart() {
        TaskList first;
        first.update({window(1, "exe:x"), window(2, "aumid:y")});
        QVERIFY(first.pin(0));
        QVERIFY(first.pin(1));
        QVERIFY(!first.pin(1));
        QCOMPARE(first.launchers(), QStringList({"exe:x", "aumid:y"}));
        QVERIFY(first.move(0, 1));
        QCOMPARE(first.launchers(), QStringList({"aumid:y", "exe:x"}));

        TaskList second;
        second.setLaunchers(first.launchers());
        QCOMPARE(keys(second), QStringList({"aumid:y", "exe:x"}));
        QVERIFY(isLauncher(second, 0));
        QVERIFY(isLauncher(second, 1));
        QVERIFY(second.unpin(0));  // a launcher without windows disappears
        QCOMPARE(second.launchers(), QStringList({"exe:x"}));
        QCOMPARE(keys(second), QStringList({"exe:x"}));
        QVERIFY(!second.pin(0));
        // Duplicates and empty keys are dropped when seeding.
        TaskList third;
        third.setLaunchers({"a", "", "a", "b"});
        QCOMPARE(third.launchers(), QStringList({"a", "b"}));
        // Unpinning a row with windows keeps the row.
        first.update({window(1, "exe:x")});
        QCOMPARE(keys(first), QStringList({"aumid:y", "exe:x"}));
        QVERIFY(first.unpin(1));
        QCOMPARE(keys(first), QStringList({"aumid:y", "exe:x"}));
        QCOMPARE(first.launchers(), QStringList({"aumid:y"}));
        QVERIFY(!first.move(0, 2));
        QVERIFY(!first.move(1, 1));
    }

    void rowsStayInPlace() {
        TaskList list;
        Recorder recorder;
        list.setObserver(&recorder);
        list.setLaunchers({"p"});
        QCOMPARE(recorder.log, QStringList({"reset"}));
        recorder.log.clear();

        QVERIFY(list.update({window(1, "a"), window(2, "p"), window(3, "b")}));
        QCOMPARE(keys(list), QStringList({"p", "a", "b"}));
        QCOMPARE(recorder.log, QStringList({"changed 0", "insert 1 2"}));
        recorder.log.clear();
        QVERIFY(!list.update({window(3, "b"), window(1, "a"), window(2, "p")}));  // z-order only
        QVERIFY(recorder.log.isEmpty());

        // An unpinned application's row disappears with its last window; a new
        // application is appended at the end.
        QVERIFY(list.update({window(2, "p"), window(3, "b"), window(4, "c")}));
        QCOMPARE(keys(list), QStringList({"p", "b", "c"}));
        QCOMPARE(recorder.log, QStringList({"remove 1", "insert 2 2"}));
        recorder.log.clear();

        // A pinned application becomes a launcher in place.
        list.update({window(3, "b"), window(4, "c")});
        QCOMPARE(keys(list), QStringList({"p", "b", "c"}));
        QVERIFY(isLauncher(list, 0));
        QCOMPARE(recorder.log, QStringList({"changed 0"}));
        recorder.log.clear();

        // A launcher that gets a window keeps its position.
        list.update({window(4, "c"), window(5, "p"), window(3, "b")});
        QCOMPARE(keys(list), QStringList({"p", "b", "c"}));
        QCOMPARE(list.row(0)->windows, QList<quintptr>({5}));
        recorder.log.clear();

        // Title changes are reported as row changes, not structure changes.
        list.update({window(4, "c"), window(5, "p", false, "renamed"), window(3, "b")});
        QCOMPARE(recorder.log, QStringList({"changed 0"}));
        QCOMPARE(list.window(5).title, QString("renamed"));
        recorder.log.clear();

        // A second window joins its application's row at the end.
        list.update({window(6, "b"), window(4, "c"), window(5, "p", false, "renamed"), window(3, "b")});
        QCOMPARE(keys(list), QStringList({"p", "b", "c"}));
        QCOMPARE(list.row(1)->windows, QList<quintptr>({3, 6}));
        QCOMPARE(recorder.log, QStringList({"changed 1"}));
        recorder.log.clear();

        QVERIFY(list.move(0, 2));
        QCOMPARE(keys(list), QStringList({"b", "c", "p"}));
        QCOMPARE(recorder.log, QStringList({"move 0 2"}));
        recorder.log.clear();
        QVERIFY(list.pin(0));
        QCOMPARE(recorder.log, QStringList({"changed 0"}));
        QCOMPARE(list.launchers(), QStringList({"b", "p"}));
        // Invalid snapshot entries are ignored.
        recorder.log.clear();
        list.update({window(0, "x"), window(7, ""), window(6, "b"), window(6, "b"), window(4, "c"),
                     window(5, "p", false, "renamed"), window(3, "b")});
        QCOMPARE(keys(list), QStringList({"b", "c", "p"}));
        QVERIFY(recorder.log.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TaskListTest)
#include "tasklist_test.moc"
