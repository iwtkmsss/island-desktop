#include "appstate.h"
#include <QDebug>
#include <QLocale>
#include <QStringList>

AppState::AppState(const QString &settingsFile, QObject *parent)
    : QObject(parent), settings_(settingsFile, QSettings::IniFormat), now_(QDateTime::currentDateTime()) {
    const auto savedSection = settings_.value("ui/section", section_).toString();
    if (savedSection == "clock" || savedSection == "timer" || savedSection == "music" || savedSection == "volume")
        section_ = savedSection;
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
    connect(&eventExpiry_, &QTimer::timeout, this, &AppState::dismissEvent);
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
    settings_.setValue("schemaVersion", 1);
    settings_.setValue(key, value);
    settings_.sync();
    if (settings_.status() != QSettings::NoError)
        qWarning() << "Could not save settings to" << settings_.fileName();
}

void AppState::setSection(const QString &section) {
    if (section != "clock" && section != "timer" && section != "music" && section != "volume")
        return;
    setSettingsOpen(false);
    if (section_ == section)
        return;
    section_ = section;
    persist("ui/section", section_);
    emit sectionChanged();
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
        dismissEvent();
        emit countdownChanged();
        updateTicker();
    }
}

void AppState::toggleTimer() {
    const auto now = monotonic_.elapsed();
    if (countdown_.advance(now)) {
        showTemporaryEvent(QStringLiteral("Час вийшов"));
        emit timerFinished();
    } else if (countdown_.state() == Countdown::State::Running) {
        countdown_.pause(now);
    } else {
        dismissEvent();
        countdown_.start(now);
    }
    emit countdownChanged();
    updateTicker();
}

void AppState::resetTimer() {
    countdown_.reset();
    dismissEvent();
    emit countdownChanged();
    updateTicker();
}

void AppState::showTemporaryEvent(const QString &title, int durationMs) {
    eventKind_ = "notice";
    eventTitle_ = title;
    eventExpiry_.start(qMax(1, durationMs));
    emit eventChanged();
}

void AppState::showVolumeEvent(int volume, bool muted, int durationMs) {
    // A level change must not replace a more important active event (e.g. timer).
    if (!eventTitle_.isEmpty() && eventKind_ != "volume") return;
    eventKind_ = "volume";
    eventVolume_ = qBound(0, volume, 100);
    eventMuted_ = muted;
    eventTitle_ = muted ? QStringLiteral("Звук вимкнено")
                        : QStringLiteral("Гучність %1%").arg(eventVolume_);
    eventExpiry_.start(qMax(1, durationMs));
    emit eventChanged();
}

void AppState::dismissEvent() {
    eventExpiry_.stop();
    if (eventTitle_.isEmpty())
        return;
    eventTitle_.clear();
    eventKind_.clear();
    emit eventChanged();
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
            showTemporaryEvent(QStringLiteral("Час вийшов"));
            emit timerFinished();
        }
        emit countdownChanged();
    }
    updateTicker();
}

