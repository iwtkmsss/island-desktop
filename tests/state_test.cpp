#include "appstate.h"
#include "countdown.h"
#include "shellinteraction.h"
#include "borrowed/mediatimeline.h"
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class StateTest : public QObject {
    Q_OBJECT
private slots:
    void volumeFlyoutReturnsToUserSelection() {
        QTemporaryDir dir;
        AppState state(dir.filePath("settings.ini"));
        state.setSection("music");
        state.showVolumeEvent(42, false, 10);
        QCOMPARE(state.eventKind(), "volume");
        QCOMPARE(state.eventVolume(), 42);
        QCOMPARE(state.section(), "media");
        state.showVolumeEvent(51, true, 10);
        QVERIFY(state.eventMuted());
        QCOMPARE(state.eventIcon(), "speaker-off");
        QTRY_VERIFY(state.eventTitle().isEmpty());
        QCOMPARE(state.section(), "media");
        QVERIFY(state.eventKind().isEmpty());
    }
    void volumeDoesNotReplaceTimerCompletion() {
        QTemporaryDir dir;
        AppState state(dir.filePath("settings.ini"));
        state.setSection("timer");
        state.showTemporaryEvent("Час вийшов");
        state.showVolumeEvent(90, false);
        QCOMPARE(state.eventTitle(), "Час вийшов");
        QCOMPARE(state.eventKind(), "notice");
        QCOMPARE(state.section(), "time");
        QCOMPARE(state.timeView(), "timer");
        state.dismissEvent();
        QVERIFY(state.eventTitle().isEmpty());
        state.showVolumeEvent(140, false);
        QCOMPARE(state.eventVolume(), 100);
    }
    void timerCompletionReplacesVolumeEventAndVolumeDoesNotReturn() {
        QTemporaryDir dir;
        AppState state(dir.filePath("settings.ini"));
        state.showVolumeEvent(30, false);
        QCOMPARE(state.eventKind(), "volume");
        IslandEvent done;
        done.kind = "timer";
        done.title = "Час вийшов";
        done.icon = "check";
        done.priority = 100;
        done.durationMs = 8000;
        done.coalesceKey = "timer";
        state.postEvent(done);
        QCOMPARE(state.eventKind(), "timer");
        QCOMPARE(state.pendingEvents(), 0);
        state.dismissEvent();
        QVERIFY(state.eventKind().isEmpty());
    }
    void queuedNoticeShowsAfterTimerEventIsDismissed() {
        QTemporaryDir dir;
        AppState state(dir.filePath("settings.ini"));
        IslandEvent done;
        done.kind = "timer";
        done.title = "Час вийшов";
        done.priority = 100;
        done.durationMs = 8000;
        state.postEvent(done);
        state.showTemporaryEvent("Notice");
        QCOMPARE(state.eventKind(), "timer");
        QCOMPARE(state.pendingEvents(), 1);
        QSignalSpy spy(&state, &AppState::eventChanged);
        state.dismissEvent();
        QCOMPARE(state.eventKind(), "notice");
        QCOMPARE(state.eventTitle(), "Notice");
        QCOMPARE(state.pendingEvents(), 0);
        QVERIFY(spy.count() >= 1);
    }
    void resetTimerKeepsUnrelatedNotice() {
        QTemporaryDir dir;
        AppState state(dir.filePath("settings.ini"));
        state.showTemporaryEvent("Notice");
        state.resetTimer();
        QCOMPARE(state.eventTitle(), "Notice");
        IslandEvent done;
        done.kind = "timer";
        done.title = "Час вийшов";
        done.priority = 100;
        state.postEvent(done);
        state.resetTimer();
        QCOMPARE(state.eventKind(), "notice");
    }
    void legacySavedSectionIsMigrated() {
        QTemporaryDir dir;
        const auto path = dir.filePath("settings.ini");
        { QSettings file(path, QSettings::IniFormat);
          file.setValue("ui/section", "timer"); }
        AppState state(path);
        QCOMPARE(state.section(), "time");
        QCOMPARE(state.timeView(), "timer");
        { QSettings file(path, QSettings::IniFormat);
          file.setValue("ui/section", "volume"); }
        AppState other(path);
        QCOMPARE(other.section(), "media");
    }
    void timeViewPersistsAndRejectsInvalidValues() {
        QTemporaryDir dir;
        const auto path = dir.filePath("settings.ini");
        {
            AppState state(path);
            QCOMPARE(state.timeView(), "clock");
            state.setTimeView("timer");
            state.setTimeView("bogus");
            QCOMPARE(state.timeView(), "timer");
        }
        AppState restored(path);
        QCOMPARE(restored.timeView(), "timer");
        QCOMPARE(restored.section(), "time");
    }
    void soundSectionPersistsWithoutPersistingFlyouts() {
        QTemporaryDir dir;
        const auto path = dir.filePath("settings.ini");
        {
            AppState state(path);
            state.setSection("volume");
            state.showVolumeEvent(50, true);
        }
        AppState restored(path);
        QCOMPARE(restored.section(), "media");
        QVERIFY(restored.eventTitle().isEmpty());
        QVERIFY(restored.eventKind().isEmpty());
    }
    void briefHoverDoesNotOpenButIntentionalHoverDoes() {
        ShellInteraction shell;
        using Action = ShellInteraction::Action;
        QCOMPARE(shell.update({true, false, false, false}, 0), Action::None);
        QCOMPARE(shell.update({false, false, false, false}, 150), Action::None);
        QCOMPARE(shell.update({true, false, false, false}, 200), Action::None);
        QCOMPARE(shell.update({true, false, false, false}, 459), Action::None);
        QCOMPARE(shell.update({true, false, false, false}, 460), Action::Open);
    }
    void leavingAndReturningCancelsClose() {
        ShellInteraction shell;
        using Action = ShellInteraction::Action;
        shell.update({false, true, false, false}, 0);
        QCOMPARE(shell.update({true, true, false, false}, 600), Action::None);
        shell.update({false, true, false, false}, 700);
        QCOMPARE(shell.update({false, true, false, false}, 1319), Action::None);
        QCOMPARE(shell.update({false, true, false, false}, 1320), Action::Close);
    }
    void pinAndDraggingPreventAutomaticClose() {
        ShellInteraction shell;
        using Action = ShellInteraction::Action;
        shell.update({false, true, true, false}, 0);
        QCOMPARE(shell.update({false, true, true, false}, 5000), Action::None);
        QCOMPARE(shell.update({false, true, false, true}, 6000), Action::None);
        shell.update({false, true, false, false}, 6100);
        QCOMPARE(shell.update({false, true, false, false}, 6720), Action::Close);
    }
    void explicitCloseRequiresLeavingBeforeReopening() {
        ShellInteraction shell;
        using Action = ShellInteraction::Action;
        shell.suppressUntilLeave();
        shell.update({true, false, false, false}, 0);
        QCOMPARE(shell.update({true, false, false, false}, 5000), Action::None);
        shell.update({false, false, false, false}, 5100);
        shell.update({true, false, false, false}, 5200);
        QCOMPARE(shell.update({true, false, false, false}, 5460), Action::Open);
    }
    void mediaTimelineRejectsJitterButConfirmsRealSeek() {
        MediaTimeline timeline;
        QCOMPARE(timeline.accept(30000, 180000, true, 1, "track", 0), 30000);
        QCOMPARE(timeline.accept(29000, 180000, true, 1, "track", 1000), 31000);
        QCOMPARE(timeline.accept(32000, 180000, true, 1, "track", 2000), 32000);
        QCOMPARE(timeline.accept(10000, 180000, true, 1, "track", 3000), 33000);
        QCOMPARE(timeline.accept(11000, 180000, true, 1, "track", 4000), 11000);
    }
    void newTrackAndPausedPositionDoNotInheritPreviousTimeline() {
        MediaTimeline timeline;
        timeline.accept(90000, 180000, true, 1, "first", 0);
        QCOMPARE(timeline.accept(0, 180000, true, 1, "second", 1000), 0);
        QCOMPARE(timeline.accept(0, 180000, false, 1, "second", 2000), 0);
        QCOMPARE(timeline.accept(1000, 180000, false, 1, "second", 5000), 1000);
    }
    void countdownUsesDeadline() {
        Countdown timer;
        QVERIFY(timer.configure(5000));
        timer.start(100);
        QCOMPARE(timer.remaining(2100), 3000);
        QVERIFY(!timer.advance(5099));
        QVERIFY(timer.advance(5100));
        QCOMPARE(timer.remaining(9000), 0);
        QVERIFY(!timer.advance(9000)); // Completion fires once, even after a delayed UI tick.
    }
    void pauseAndResumeKeepRemainingTime() {
        Countdown timer;
        timer.configure(10000);
        timer.start(0);
        timer.pause(3500);
        QCOMPARE(timer.remaining(50000), 6500);
        timer.start(50000);
        QCOMPARE(timer.remaining(51000), 5500);
        QVERIFY(timer.advance(56500));
        timer.start(60000);
        QCOMPARE(timer.remaining(60000), 10000);
        timer.reset();
        QCOMPARE(timer.state(), Countdown::State::Idle);
    }
    void invalidDurationsAndRunningChangesAreRejected() {
        Countdown timer;
        QVERIFY(!timer.configure(0));
        QVERIFY(!timer.configure(181 * 60000));
        timer.start(0);
        QVERIFY(!timer.configure(5000));
        QCOMPARE(timer.duration(), 25 * 60000);
    }
    void selectionSurvivesCollapseAndTemporaryEvent() {
        QTemporaryDir dir;
        AppState state(dir.filePath("settings.ini"));
        state.setSection("timer");
        state.setExpanded(true);
        state.setExpanded(false);
        QCOMPARE(state.section(), "time");
        QCOMPARE(state.timeView(), "timer");
        state.showTemporaryEvent("Event");
        QCOMPARE(state.section(), "time");
        state.dismissEvent();
        QCOMPARE(state.section(), "time");
        state.setSettingsOpen(true);
        QVERIFY(state.expanded());
        state.setExpanded(false);
        QVERIFY(!state.settingsOpen());
        QCOMPARE(state.section(), "time");
        QCOMPARE(state.timeView(), "timer");
    }
    void preferencesSurviveRestart() {
        QTemporaryDir dir;
        const auto path = dir.filePath("settings.ini");
        {
            AppState state(path);
            state.setSection("media");
            state.setTheme("light");
            state.setReducedMotion(true);
            state.setTimerMinutes(15);
            state.toggleTimer();
        }
        AppState restored(path);
        QCOMPARE(restored.section(), "media");
        QCOMPARE(restored.theme(), "light");
        QVERIFY(restored.reducedMotion());
        QCOMPARE(restored.timerMinutes(), 15);
        QCOMPARE(restored.timerText(), "15:00");
        QCOMPARE(restored.timerState(), "idle");
        QVERIFY(!restored.expanded());
    }
    void unknownSettingsFallBackToUsableDefaults() {
        QTemporaryDir dir;
        const auto path = dir.filePath("settings.ini");
        { QSettings file(path, QSettings::IniFormat);
          file.setValue("ui/section", "removed-module");
          file.setValue("ui/theme", "purple");
          file.setValue("timer/minutes", -42); }
        AppState state(path);
        QCOMPARE(state.section(), "time");
        QCOMPARE(state.theme(), "dark");
        QCOMPARE(state.timerMinutes(), 25);
        state.setSection("unknown");
        state.setTheme("unknown");
        QCOMPARE(state.section(), "time");
        QCOMPARE(state.theme(), "dark");
    }
    void devicesSectionPersistsAndSurvivesTemporaryEvent() {
        QTemporaryDir dir;
        const auto path = dir.filePath("settings.ini");
        {
            AppState state(path);
            state.setSection("devices");
            QCOMPARE(state.section(), "devices");
            state.showTemporaryEvent("Event", 10);
            QCOMPARE(state.section(), "devices");
            QTRY_VERIFY(state.eventTitle().isEmpty());
            QCOMPARE(state.section(), "devices");
        }
        AppState restored(path);
        QCOMPARE(restored.section(), "devices");
    }
    void eventExpiresWithoutChangingSelection() {
        QTemporaryDir dir;
        AppState state(dir.filePath("settings.ini"));
        state.setSection("music");
        state.showTemporaryEvent("Event", 10);
        QTRY_VERIFY(state.eventTitle().isEmpty());
        QCOMPARE(state.section(), "media");
    }
};
QTEST_GUILESS_MAIN(StateTest)
#include "state_test.moc"
