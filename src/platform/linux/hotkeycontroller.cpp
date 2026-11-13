#include "hotkeycontroller.h"
#include <QAction>
#include <QGuiApplication>
#include <QKeySequence>
#include <KGlobalAccel>
class HotkeyEventFilter final {
public:
    QAction actions[2];
};
namespace {
const char *keys[] = {"hotkeys/toggle", "hotkeys/playPause"};
const char *defaults[] = {"Ctrl+Alt+Space", "Ctrl+Alt+P"};
QString normalised(const QString &text) {
    const auto sequence = QKeySequence::fromString(text.trimmed(), QKeySequence::PortableText);
    if (sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown || sequence[0].key() == 0
        || sequence[0].keyboardModifiers() == Qt::NoModifier) return {};
    return sequence.toString(QKeySequence::PortableText);
}
}
HotkeyController::HotkeyController(const QString &file, QObject *parent)
    : QObject(parent), settings_(file, QSettings::IniFormat), filter_(std::make_unique<HotkeyEventFilter>()) {
    for (int i = 0; i < 2; ++i) {
        sequences_[i] = normalised(settings_.value(keys[i], defaults[i]).toString());
        auto *action = &filter_->actions[i];
        action->setObjectName(i == 0 ? "toggle-island" : "play-pause");
        action->setText(i == 0 ? QStringLiteral("Показати острівець") : QStringLiteral("Пауза / відтворення"));
        action->setProperty("componentName", "io.github.iwtkmsss.island-desktop");
        action->setProperty("componentDisplayName", "Island Desktop");
        connect(action, &QAction::triggered, this, [this, i] { handleHotkey(i); });
    }
    // Registration is deferred so smoke/diagnostic runs can suspend it first.
    QMetaObject::invokeMethod(this, [this] { registerAll(); emit changed(); }, Qt::QueuedConnection);
}
HotkeyController::~HotkeyController() { unregisterAll(); }
void HotkeyController::unregisterAll() {
    for (int i = 0; i < 2; ++i) {
        if (active_[i]) KGlobalAccel::self()->removeAllShortcuts(&filter_->actions[i]);
        active_[i] = false;
    }
}
void HotkeyController::registerAll() {
    unregisterAll();
    if (capturing_ || suspended_ || !qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains("KDE", Qt::CaseInsensitive)) return;
    for (int i = 0; i < 2; ++i) {
        if (sequences_[i].isEmpty()) continue;
        const auto sequence = QKeySequence::fromString(sequences_[i], QKeySequence::PortableText);
        if (!KGlobalAccel::isGlobalShortcutAvailable(sequence, "io.github.iwtkmsss.island-desktop")) continue;
        active_[i] = KGlobalAccel::self()->setShortcut(&filter_->actions[i], {sequence}, KGlobalAccel::NoAutoloading);
    }
}
void HotkeyController::handleHotkey(int index) {
    if (capturing_ || suspended_) return;
    if (index == 0) emit toggleRequested(); else emit playPauseRequested();
}
void HotkeyController::setSequence(int index, const QString &text) {
    const auto value = normalised(text);
    if ((!text.trimmed().isEmpty() && value.isEmpty()) || (!value.isEmpty() && value == sequences_[1-index]) || value == sequences_[index]) return;
    sequences_[index] = value; settings_.setValue(keys[index], value); settings_.sync(); registerAll(); emit changed();
}
void HotkeyController::setCapturing(bool value) { if (value != capturing_) { capturing_ = value; registerAll(); emit changed(); } }
void HotkeyController::setSuspended(bool value) { if (value != suspended_) { suspended_ = value; registerAll(); emit changed(); } }
QString HotkeyController::sequenceFromKey(int key, int modifiers) const {
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta) return {};
    return normalised(QKeySequence(QKeyCombination(Qt::KeyboardModifiers(modifiers), Qt::Key(key))).toString(QKeySequence::PortableText));
}
