#include "hotkeycontroller.h"
#include "hotkeymapping.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QKeySequence>
#include <QMetaObject>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
const char *const settingsKeys[2] = {"hotkeys/toggle", "hotkeys/playPause"};
const char *const defaults[2] = {"Ctrl+Alt+Space", "Ctrl+Alt+P"};

// Returns the normalised portable text, or empty when not acceptable.
QString normalised(const QString &text) {
    const auto sequence = QKeySequence::fromString(text.trimmed(), QKeySequence::PortableText);
    if (!nativeHotkey(sequence).valid) return {};
    return sequence.toString(QKeySequence::PortableText);
}
}  // namespace

#ifdef Q_OS_WIN

class HotkeyEventFilter final : public QAbstractNativeEventFilter {
public:
    explicit HotkeyEventFilter(HotkeyController *owner) : owner_(owner) {}
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *) override {
        if (eventType != "windows_generic_MSG" || !message) return false;
        const auto *msg = static_cast<const MSG *>(message);
        if (msg->message != WM_HOTKEY || msg->hwnd != nullptr) return false;
        const int id = static_cast<int>(msg->wParam);
        if (id != idBase && id != idBase + 1) return false;
        const int index = id - idBase;
        QMetaObject::invokeMethod(owner_, [owner = owner_, index] { owner->handleHotkey(index); }, Qt::QueuedConnection);
        return true;
    }
    static constexpr int idBase = 0x4944;  // "ID"
private:
    HotkeyController *owner_;
};

HotkeyController::HotkeyController(const QString &settingsFile, QObject *parent)
    : QObject(parent), settings_(settingsFile, QSettings::IniFormat) {
    for (int i = 0; i < 2; ++i) {
        // An explicitly saved empty string means disabled; an absent or unusable value falls back to the default.
        if (settings_.contains(QLatin1String(settingsKeys[i]))) {
            const auto stored = settings_.value(QLatin1String(settingsKeys[i])).toString();
            sequences_[i] = stored.trimmed().isEmpty() ? QString() : normalised(stored);
            if (!stored.trimmed().isEmpty() && sequences_[i].isEmpty()) sequences_[i] = normalised(QLatin1String(defaults[i]));
        } else {
            sequences_[i] = normalised(QLatin1String(defaults[i]));
        }
    }
    if (sequences_[0] == sequences_[1]) sequences_[1].clear();
    filter_ = std::make_unique<HotkeyEventFilter>(this);
    if (auto *app = QCoreApplication::instance()) app->installNativeEventFilter(filter_.get());
    registerAll();
}

HotkeyController::~HotkeyController() {
    unregisterAll();
    if (auto *app = QCoreApplication::instance()) app->removeNativeEventFilter(filter_.get());
}

void HotkeyController::unregisterAll() {
    for (int i = 0; i < 2; ++i) {
        if (active_[i]) UnregisterHotKey(nullptr, HotkeyEventFilter::idBase + i);
        active_[i] = false;
    }
}

void HotkeyController::registerAll() {
    unregisterAll();
    if (!suspended_ && !capturing_) {
        for (int i = 0; i < 2; ++i) {
            if (sequences_[i].isEmpty()) continue;
            const auto native = nativeHotkey(QKeySequence::fromString(sequences_[i], QKeySequence::PortableText));
            if (!native.valid) continue;
            active_[i] = RegisterHotKey(nullptr, HotkeyEventFilter::idBase + i, native.modifiers | MOD_NOREPEAT, native.virtualKey) != FALSE;
        }
    }
}

#else

class HotkeyEventFilter final : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override { return false; }
};

HotkeyController::HotkeyController(const QString &settingsFile, QObject *parent)
    : QObject(parent), settings_(settingsFile, QSettings::IniFormat) {
    for (int i = 0; i < 2; ++i) {
        if (settings_.contains(QLatin1String(settingsKeys[i])))
            sequences_[i] = normalised(settings_.value(QLatin1String(settingsKeys[i])).toString());
        else
            sequences_[i] = normalised(QLatin1String(defaults[i]));
    }
}
HotkeyController::~HotkeyController() = default;
void HotkeyController::unregisterAll() {}
void HotkeyController::registerAll() {}

#endif

void HotkeyController::handleHotkey(int index) {
    if (capturing_ || suspended_) return;
    if (index == 0) emit toggleRequested();
    else if (index == 1) emit playPauseRequested();
}

void HotkeyController::setSequence(int index, const QString &text) {
    const auto trimmed = text.trimmed();
    QString value;
    if (!trimmed.isEmpty()) {
        value = normalised(trimmed);
        if (value.isEmpty()) return;
        if (value == sequences_[1 - index]) return;
    }
    if (value == sequences_[index]) return;
    sequences_[index] = value;
    settings_.setValue(QLatin1String(settingsKeys[index]), value);
    settings_.sync();
    registerAll();
    emit changed();
}

void HotkeyController::setCapturing(bool capturing) {
    if (capturing_ == capturing) return;
    capturing_ = capturing;
    registerAll();
    emit changed();
}

void HotkeyController::setSuspended(bool suspended) {
    if (suspended_ == suspended) return;
    suspended_ = suspended;
    registerAll();
    emit changed();
}

QString HotkeyController::sequenceFromKey(int key, int modifiers) const {
    return hotkeyText(key, modifiers);
}
