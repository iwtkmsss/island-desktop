#pragma once

#include "countdown.h"
#include "eventqueue.h"
#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QSettings>
#include <QTimer>

class AppState final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString section READ section WRITE setSection NOTIFY sectionChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged)
    Q_PROPERTY(bool artworkGlow READ artworkGlow WRITE setArtworkGlow NOTIFY artworkGlowChanged)
    Q_PROPERTY(bool eyesEnabled READ eyesEnabled WRITE setEyesEnabled NOTIFY eyesEnabledChanged)
    Q_PROPERTY(int passThroughSeconds READ passThroughSeconds WRITE setPassThroughSeconds NOTIFY passThroughSecondsChanged)
    Q_PROPERTY(bool passThroughActive READ passThroughActive NOTIFY passThroughActiveChanged)
    Q_PROPERTY(bool expanded READ expanded WRITE setExpanded NOTIFY expandedChanged)
    Q_PROPERTY(bool settingsOpen READ settingsOpen WRITE setSettingsOpen NOTIFY settingsOpenChanged)
    Q_PROPERTY(QString clockTime READ clockTime NOTIFY clockChanged)
    Q_PROPERTY(QString clockSeconds READ clockSeconds NOTIFY clockChanged)
    Q_PROPERTY(QString clockDate READ clockDate NOTIFY clockChanged)
    Q_PROPERTY(QString clockWeekday READ clockWeekday NOTIFY clockChanged)
    Q_PROPERTY(QString clockWeekdayShort READ clockWeekdayShort NOTIFY clockChanged)
    Q_PROPERTY(QString timerText READ timerText NOTIFY countdownChanged)
    Q_PROPERTY(QString timerState READ timerState NOTIFY countdownChanged)
    Q_PROPERTY(int timerMinutes READ timerMinutes NOTIFY countdownChanged)
    Q_PROPERTY(double timerProgress READ timerProgress NOTIFY countdownChanged)
    Q_PROPERTY(QString eventTitle READ eventTitle NOTIFY eventChanged)
    Q_PROPERTY(QString eventKind READ eventKind NOTIFY eventChanged)
    Q_PROPERTY(int eventVolume READ eventVolume NOTIFY eventChanged)
    Q_PROPERTY(bool eventMuted READ eventMuted NOTIFY eventChanged)
    Q_PROPERTY(QString eventIcon READ eventIcon NOTIFY eventChanged)
    Q_PROPERTY(int pendingEvents READ pendingEvents NOTIFY eventChanged)
    Q_PROPERTY(QString timeView READ timeView WRITE setTimeView NOTIFY timeViewChanged)

public:
    explicit AppState(const QString &settingsFile, QObject *parent = nullptr);
    QString section() const { return section_; }
    QString theme() const { return theme_; }
    bool reducedMotion() const { return reducedMotion_; }
    bool artworkGlow() const { return artworkGlow_; }
    bool eyesEnabled() const { return eyesEnabled_; }
    int passThroughSeconds() const { return passThroughSeconds_; }
    bool passThroughActive() const { return passThroughActive_; }
    bool expanded() const { return expanded_; }
    bool settingsOpen() const { return settingsOpen_; }
    QString clockTime() const { return now_.toString("HH:mm"); }
    QString clockSeconds() const { return now_.toString("ss"); }
    QString clockDate() const;
    QString clockWeekday() const;
    QString clockWeekdayShort() const;
    QString timerText() const;
    QString timerState() const;
    int timerMinutes() const { return static_cast<int>(countdown_.duration() / 60000); }
    double timerProgress() const;
    QString eventTitle() const { const auto *e = events_.current(); return e ? e->title : QString(); }
    QString eventKind() const { const auto *e = events_.current(); return e ? e->kind : QString(); }
    QString eventIcon() const { const auto *e = events_.current(); return e ? e->icon : QString(); }
    int eventVolume() const { const auto *e = events_.current(); return e ? qBound(0, e->level, 100) : 0; }
    bool eventMuted() const { const auto *e = events_.current(); return e && e->muted; }
    int pendingEvents() const { return events_.pending(); }
    QString timeView() const { return timeView_; }

    void setSection(const QString &section);
    void setTimeView(const QString &view);
    void postEvent(const IslandEvent &event);
    void setTheme(const QString &theme);
    void setReducedMotion(bool enabled);
    void setArtworkGlow(bool enabled);
    void setEyesEnabled(bool enabled);
    void setPassThroughSeconds(int seconds);
    Q_INVOKABLE void startPassThrough();
    void setExpanded(bool expanded);
    void setSettingsOpen(bool open);
    void showTemporaryEvent(const QString &title, int durationMs = 8000);
    void showVolumeEvent(int volume, bool muted, int durationMs = 1800);

    Q_INVOKABLE void setTimerMinutes(int minutes);
    Q_INVOKABLE void toggleTimer();
    Q_INVOKABLE void resetTimer();
    Q_INVOKABLE void dismissEvent();

signals:
    void sectionChanged();
    void timeViewChanged();
    void themeChanged();
    void reducedMotionChanged();
    void artworkGlowChanged();
    void eyesEnabledChanged();
    void expandedChanged();
    void passThroughSecondsChanged();
    void passThroughActiveChanged();
    void settingsOpenChanged();
    void clockChanged();
    void countdownChanged();
    void eventChanged();
    void timerFinished();

private:
    void persist(const QString &key, const QVariant &value);
    void tick();
    void updateTicker();
    QSettings settings_;
    void removeTimerEvents();
    void postTimerFinished();
    void applyEventChange(bool currentChanged, int pendingBefore);
    QString section_ = "home";
    QString timeView_ = "clock";
    QString theme_ = "dark";
    bool reducedMotion_ = false;
    bool artworkGlow_ = true;
    bool eyesEnabled_ = true;
    int passThroughSeconds_ = 5;
    bool passThroughActive_ = false;
    qint64 passThroughUntil_ = 0;
    QTimer passThroughTimer_;
    bool expanded_ = false;
    bool settingsOpen_ = false;
    QDateTime now_;
    QElapsedTimer monotonic_;
    QTimer clockTicker_;
    QTimer activityTicker_;
    QTimer eventExpiry_;
    Countdown countdown_;
    EventQueue events_;
};

