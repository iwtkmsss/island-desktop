#include "appstate.h"
#include <QDebug>
#include <QLocale>
#include <QStringList>
#include <limits>

namespace {
// Maps a current or legacy section id to a section and an optional time view.
bool mapSection(const QString &id, QString &section, QString &view) {
    view.clear();
    if (id == "time" || id == "media" || id == "devices") {
        section = id;
    } else if (id == "clock" || id == "timer") {
        section = QStringLiteral("time");
        view = id;
    } else if (id == "music" || id == "volume") {
        section = QStringLiteral("media");
    } else {
        return false;
    }
    return true;
}

bool validTimeView(const QString &view) { return view == "clock" || view == "timer"; }
}

AppState::AppState(const QString &settingsFile, QObject *parent)
    : QObject(parent), settings_(settingsFile, QSettings::IniFormat), now_(QDateTime::currentDateTime()) {
    const auto savedTimeView = settings_.value("ui/timeView", timeView_).toString();
    if (validTimeView(savedTimeView))
        timeView_ = savedTimeView;
    const auto savedSection = settings_.value("ui/section", section_).toString();
    QString mappedSection, mappedView;
    if (mapSection(savedSection, mappedSection, mappedView)) {
        section_ = mappedSection;
        if (!mappedView.isEmpty() && !validTimeView(settings_.value("ui/timeView").toString()))
            timeView_ = mappedView;
    }
    const auto savedTheme = settings_.value("ui/theme", theme_).toString();
    if (savedTheme == "light" || savedTheme == "dark")
        theme_ = savedTheme;
    reducedMotion_ = settings_.value("ui/reducedMotion", false).toBool();
    const int savedMinutes = settings_.value("timer/minutes", 25).toInt();
    if (savedMinutes >= 1 && savedMinutes <= 180)
        countdown_.configure(static_cast<qint64>(savedMinutes) * 60000);
    monotonic_.start();
    clockTicker_.setInterval(1000);
    connect(&clockTicker_, &QTimer::timeout, this, [this] {
        now_ = QDateTime::currentDateTime();
        emit clockChanged();
    });
    clockTicker_.start();
    activityTicker_.setInterval(100);
    connect(&activityTicker_, &QTimer::timeout, this, &AppState::tick);
    eventExpiry_.setSingleShot(true);
    connect(&eventExpiry_, &QTimer::timeout, this, [this] {
        const int pendingBefore = events_.pending();
        const bool changed = events_.advance(monotonic_.elapsed());
        // The timer may fire marginally early; keep waiting for the real deadline.
        applyEventChange(changed || events_.current() != nullptr, pendingBefore);
    });
}

QString AppState::clockDate() const {
    return QLocale(QLocale::Ukrainian, QLocale::Ukraine).toString(now_.date(), "d MMMM yyyy");
}

QString AppState::clockWeekday() const {
    auto day = QLocale(QLocale::Ukrainian, QLocale::Ukraine).dayName(now_.date().dayOfWeek());
    if (!day.isEmpty())
        day[0] = day[0].toUpper();
    return day;
}

QString AppState::clockWeekdayShort() const {
    static const QStringList days{QStringLiteral("Пн"), QStringLiteral("Вт"), QStringLiteral("Ср"),
        QStringLiteral("Чт"), QStringLiteral("Пт"), QStringLiteral("Сб"), QStringLiteral("Нд")};
    return days.at(now_.date().dayOfWeek() - 1);
}

QString AppState::timerText() const {
    const auto seconds = (countdown_.remaining(monotonic_.elapsed()) + 999) / 1000;
    return QStringLiteral("%1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString AppState::timerState() const {
    switch (countdown_.state()) {
    case Countdown::State::Running: return "running";
    case Countdown::State::Paused: return "paused";
    case Countdown::State::Finished: return "finished";
    default: return "idle";
    }
}

double AppState::timerProgress() const {
    return 1.0 - static_cast<double>(countdown_.remaining(monotonic_.elapsed())) / countdown_.duration();
}

void AppState::persist(const QString &key, const QVariant &value) {
    settings_.setValue("schemaVersion", 2);
    settings_.setValue(key, value);
    settings_.sync();
    if (settings_.status() != QSettings::NoError)
        qWarning() << "Could not save settings to" << settings_.fileName();
}

void AppState::setSection(const QString &section) {
    QString mapped, view;
    if (!mapSection(section, mapped, view))
        return;
    setSettingsOpen(false);
    if (!view.isEmpty())
        setTimeView(view);
    if (section_ == mapped)
        return;
    section_ = mapped;
    persist("ui/section", section_);
    emit sectionChanged();
}

void AppState::setTimeView(const QString &view) {
    if (!validTimeView(view) || timeView_ == view)
        return;
    timeView_ = view;
    persist("ui/timeView", timeView_);
    emit timeViewChanged();
}

void AppState::setTheme(const QString &theme) {
    if ((theme != "dark" && theme != "light") || theme_ == theme)
        return;
    theme_ = theme;
    persist("ui/theme", theme_);
    emit themeChanged();
}

void AppState::setReducedMotion(bool enabled) {
    if (reducedMotion_ == enabled)
        return;
    reducedMotion_ = enabled;
    persist("ui/reducedMotion", enabled);
    emit reducedMotionChanged();
}

void AppState::setExpanded(bool expanded) {
    if (expanded_ == expanded)
        return;
    expanded_ = expanded;
    if (!expanded)
        setSettingsOpen(false);
    emit expandedChanged();
}

void AppState::setSettingsOpen(bool open) {
    if (settingsOpen_ == open)
        return;
    settingsOpen_ = open;
    if (open)
        setExpanded(true);
    emit settingsOpenChanged();
}

void AppState::setTimerMinutes(int minutes) {
    if (minutes < 1 || minutes > 180)
        return;
    if (countdown_.configure(static_cast<qint64>(minutes) * 60000)) {
        persist("timer/minutes", minutes);
        removeTimerEvents();
        emit countdownChanged();
        updateTicker();
    }
}

void AppState::toggleTimer() {
    const auto now = monotonic_.elapsed();
    if (countdown_.advance(now)) {
        postTimerFinished();
        emit timerFinished();
    } else if (countdown_.state() == Countdown::State::Running) {
        countdown_.pause(now);
    } else {
        removeTimerEvents();
        countdown_.start(now);
    }
    emit countdownChanged();
    updateTicker();
}

void AppState::resetTimer() {
    countdown_.reset();
    removeTimerEvents();
    emit countdownChanged();
    updateTicker();
}

void AppState::applyEventChange(bool currentChanged, int pendingBefore) {
    if (currentChanged) {
        if (events_.current()) {
            const auto remaining = events_.deadline() - monotonic_.elapsed();
            eventExpiry_.start(static_cast<int>(qBound(qint64{1}, remaining, qint64{std::numeric_limits<int>::max()})));
        } else {
            eventExpiry_.stop();
        }
    }
    if (currentChanged || events_.pending() != pendingBefore)
        emit eventChanged();
}

void AppState::postEvent(const IslandEvent &event) {
    const int pendingBefore = events_.pending();
    applyEventChange(events_.post(event, monotonic_.elapsed()), pendingBefore);
}

void AppState::removeTimerEvents() {
    const int pendingBefore = events_.pending();
    applyEventChange(events_.removeKind(QStringLiteral("timer"), monotonic_.elapsed()), pendingBefore);
}

void AppState::postTimerFinished() {
    IslandEvent event;
    event.kind = QStringLiteral("timer");
    event.title = QStringLiteral("Час вийшов");
    event.icon = QStringLiteral("check");
    event.priority = 100;
    event.durationMs = 8000;
    event.coalesceKey = QStringLiteral("timer");
    postEvent(event);
}

void AppState::showTemporaryEvent(const QString &title, int durationMs) {
    IslandEvent event;
    event.kind = QStringLiteral("notice");
    event.title = title;
    event.icon = QStringLiteral("check");
    event.priority = 50;
    event.durationMs = durationMs;
    postEvent(event);
}

void AppState::showVolumeEvent(int volume, bool muted, int durationMs) {
    IslandEvent event;
    event.kind = QStringLiteral("volume");
    event.level = qBound(0, volume, 100);
    event.muted = muted;
    event.title = muted ? QStringLiteral("Звук вимкнено")
                        : QStringLiteral("Гучність %1%").arg(event.level);
    event.icon = muted ? QStringLiteral("speaker-off") : QStringLiteral("speaker");
    event.priority = 10;
    event.durationMs = durationMs;
    event.coalesceKey = QStringLiteral("volume");
    event.transient = true;
    postEvent(event);
}

void AppState::dismissEvent() {
    const int pendingBefore = events_.pending();
    applyEventChange(events_.dismiss(monotonic_.elapsed()), pendingBefore);
}

void AppState::updateTicker() {
    const bool needed = countdown_.state() == Countdown::State::Running;
    if (needed && !activityTicker_.isActive())
        activityTicker_.start();
    else if (!needed)
        activityTicker_.stop();
}

void AppState::tick() {
    if (countdown_.state() == Countdown::State::Running) {
        if (countdown_.advance(monotonic_.elapsed())) {
            postTimerFinished();
            emit timerFinished();
        }
        emit countdownChanged();
    }
    updateTicker();
}

