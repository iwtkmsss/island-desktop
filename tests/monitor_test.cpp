#include "monitorcontroller.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class MonitorTest : public QObject {
    Q_OBJECT
private:
    static QVariantMap option(const MonitorController &controller, const QString &id) {
        for (const auto &entry : controller.options()) {
            const auto row = entry.toMap();
            if (row.value("id").toString() == id) return row;
        }
        return {};
    }

private slots:
    void automaticDefaultsToPrimaryAndFollowsPrimaryChanges() {
        QTemporaryDir dir;
        QList<MonitorInfo> topology{{"secondary", "Side screen", false}, {"primary", "Main screen", true}};
        MonitorController controller(dir.filePath("settings.ini"), [&] { return topology; });
        QVERIFY(controller.selectedId().isEmpty());
        QCOMPARE(controller.activeId(), "primary");
        const auto automatic = controller.options().first().toMap();
        QCOMPARE(automatic.value("id").toString(), QString{});
        QCOMPARE(automatic.value("label").toString(), QStringLiteral("Автоматично — основний екран"));
        QSignalSpy changed(&controller, &MonitorController::activeMonitorChanged);
        topology[0].primary = true;
        topology[1].primary = false;
        controller.position();
        QTRY_COMPARE(controller.activeId(), "secondary");
        QCOMPARE(changed.count(), 1);
        QVERIFY(controller.selectedId().isEmpty());
    }

    void explicitChoiceSurvivesPrimaryChangeReorderAndRestart() {
        QTemporaryDir dir;
        const auto path = dir.filePath("settings.ini");
        QList<MonitorInfo> topology{{"first", "Main", true}, {"second", "Studio", false}};
        {
            QSettings unrelated(path, QSettings::IniFormat);
            unrelated.setValue("ui/section", "music");
        }
        {
            MonitorController controller(path, [&] { return topology; });
            controller.selectMonitor("second");
            QTRY_COMPARE(controller.activeId(), "second");
        }
        topology = {{"second", "Studio", true}, {"first", "Main", false}};
        MonitorController restored(path, [&] { return topology; });
        QCOMPARE(restored.selectedId(), "second");
        QCOMPARE(restored.activeId(), "second");
        topology = {{"first", "Main", true}, {"second", "Studio", false}};
        restored.position();
        QTest::qWait(80);
        QCOMPARE(restored.activeId(), "second");
        QSettings saved(path, QSettings::IniFormat);
        QCOMPARE(saved.value("display/monitorId").toString(), "second");
        QCOMPARE(saved.value("ui/section").toString(), "music");
    }

    void missingChoiceFallsBackButRestoresAfterReconnectAndRestart() {
        QTemporaryDir dir;
        const auto path = dir.filePath("settings.ini");
        QList<MonitorInfo> topology{{"a", "Main", true}, {"b", "Studio", false}};
        {
            MonitorController controller(path, [&] { return topology; });
            controller.selectMonitor("b");
        }
        topology.removeLast();
        MonitorController controller(path, [&] { return topology; });
        QCOMPARE(controller.selectedId(), "b");
        QCOMPARE(controller.activeId(), "a");
        QCOMPARE(option(controller, "b").value("label").toString(), QStringLiteral("Studio — недоступний"));
        QCOMPARE(controller.options().size(), 3);
        topology.clear();
        controller.position();
        QTRY_VERIFY(controller.activeId().isEmpty());
        QCOMPARE(controller.selectedId(), "b");
        QCOMPARE(controller.options().size(), 2); // Auto + unavailable preference.
        topology = {{"b", "Studio", false}, {"a", "Main", true}};
        controller.position();
        QTRY_COMPARE(controller.activeId(), "b");
        QCOMPARE(option(controller, "b").value("label").toString(), "Studio");
        QSettings saved(path, QSettings::IniFormat);
        QCOMPARE(saved.value("display/monitorId").toString(), "b");
    }

    void explicitAutomaticClearsMissingChoiceAndPersists() {
        QTemporaryDir dir;
        const auto path = dir.filePath("settings.ini");
        QList<MonitorInfo> topology{{"a", "Main", true}, {"b", "Studio", false}};
        {
            MonitorController controller(path, [&] { return topology; });
            QSignalSpy selected(&controller, &MonitorController::selectedIdChanged);
            controller.selectMonitor("invalid-id");
            QCOMPARE(selected.count(), 0);
            controller.selectMonitor("b");
            QCOMPARE(selected.count(), 1);
            QTRY_COMPARE(controller.activeId(), "b");
            controller.selectMonitor("b");
            QCOMPARE(selected.count(), 1);
            topology.removeLast();
            controller.position();
            QTRY_COMPARE(controller.activeId(), "a");
            controller.selectMonitor({});
            QVERIFY(option(controller, "b").isEmpty());
            QCOMPARE(controller.options().size(), 2);
        }
        topology.append({"b", "Studio", false});
        MonitorController restored(path, [&] { return topology; });
        QVERIFY(restored.selectedId().isEmpty());
        QCOMPARE(restored.activeId(), "a");
    }

    void topologyBurstIsCoalescedWithoutTransientFallback() {
        QTemporaryDir dir;
        QList<MonitorInfo> topology{{"a", "Main", true}, {"b", "Studio", false}};
        int reads = 0;
        MonitorController controller(dir.filePath("settings.ini"), [&] { ++reads; return topology; });
        controller.selectMonitor("b");
        QTRY_COMPARE(controller.activeId(), "b");
        const int before = reads;
        QSignalSpy active(&controller, &MonitorController::activeMonitorChanged);
        QSignalSpy options(&controller, &MonitorController::optionsChanged);
        topology.clear();
        controller.position();
        QTest::qWait(10);
        topology = {{"a", "Main", true}};
        controller.position();
        QTest::qWait(10);
        topology = {{"b", "Studio resized", true}, {"a", "Main", false}};
        for (int i = 0; i < 20; ++i) controller.position();
        QCOMPARE(reads, before);
        QTRY_COMPARE(reads, before + 1);
        QTest::qWait(80);
        QCOMPARE(reads, before + 1);
        QCOMPARE(active.count(), 0);
        QCOMPARE(options.count(), 1);
        QCOMPARE(controller.activeId(), "b");
        QVERIFY(option(controller, "b").value("label").toString().contains("resized"));
    }

    void identityUsesDeviceMetadataAndNotEnumerationOrResolution() {
        const auto first = monitorId("Vendor", "Model", "serial-a", "DISPLAY1");
        const auto second = monitorId("Vendor", "Model", "serial-b", "DISPLAY2");
        QVERIFY(first != second);
        QCOMPARE(first, monitorId("Vendor", "Model", "serial-a", "DISPLAY9"));
        QVERIFY(monitorId("Vendor", "Model", {}, "DISPLAY1")
            != monitorId("Vendor", "Model", {}, "DISPLAY2"));
        QVERIFY(monitorId("Vendor", "Model", "00000000", "DISPLAY1")
            != monitorId("Vendor", "Model", "00000000", "DISPLAY2"));
        QList<MonitorInfo> topology{{first, "Main (1920 × 1080)", true}, {second, "Side", false}};
        QCOMPARE(resolvedMonitorId(topology, second), second);
        topology = {{second, "Side resized", true}, {first, "Main (1280 × 720)", false}};
        QCOMPARE(resolvedMonitorId(topology, first), first);
    }

    void dockChoicePersistsWithoutChangingMonitorOrSection() {
        QTemporaryDir dir;
        const auto path = dir.filePath("dock.ini");
        const QList<MonitorInfo> topology{{"screen", "Main", true}};
        {
            MonitorController monitor(path, [&] { return topology; });
            QCOMPARE(monitor.dockEdge(), "top");
            monitor.selectMonitor("screen");
            QSignalSpy changed(&monitor, &MonitorController::dockEdgeChanged);
            for (const auto &edge : {"left", "right", "bottom", "top", "left"}) monitor.setDockEdge(edge);
            QCOMPARE(changed.count(), 5);
            monitor.setDockEdge("left"); monitor.setDockEdge("invalid");
            QCOMPARE(changed.count(), 5);
            QCOMPARE(monitor.selectedId(), "screen");
        }
        MonitorController restored(path, [&] { return topology; });
        QCOMPARE(restored.dockEdge(), "left");
        QCOMPARE(restored.selectedId(), "screen");
    }

    void dockFrameTouchesAllFourPhysicalScreenEdges() {
        const QRect screen(-1920, -200, 1920, 1080);
        const QSize frame(560, 444);
        const auto top = dockFrameRect(screen, frame, "top");
        const auto bottom = dockFrameRect(screen, frame, "bottom");
        const auto left = dockFrameRect(screen, frame, "left");
        const auto right = dockFrameRect(screen, frame, "right");
        QCOMPARE(top.top(), screen.top()); QCOMPARE(bottom.bottom(), screen.bottom());
        QCOMPARE(left.left(), screen.left()); QCOMPARE(right.right(), screen.right());
        for (const auto &rect : {top, bottom, left, right}) {
            QCOMPARE(rect.size(), frame); QVERIFY(screen.contains(rect));
        }
    }

    void fixedFrameUsesLogicalMonitorCoordinatesIncludingNegativeOrigins() {
        const QSize frame(560, 444);
        QCOMPARE(monitorFrameRect(QRect(-1920, -200, 1920, 1040), frame), QRect(-1240, -198, 560, 444));
        QCOMPARE(monitorFrameRect(QRect(0, 40, 1280, 680), frame), QRect(360, 42, 560, 444));
        QCOMPARE(resolvedMonitorId({}, "disconnected"), QString{});
        QCOMPARE(resolvedMonitorId({{"remaining", "Only screen", false}}, "disconnected"), "remaining");
    }
};

QTEST_GUILESS_MAIN(MonitorTest)
#include "monitor_test.moc"
