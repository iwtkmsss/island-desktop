#pragma once

#include <QKeyCombination>
#include <QKeySequence>
#include <QString>
#include <Qt>

// Mapping between Qt key chords and Win32 RegisterHotKey values. The numeric
// constants are defined here so the header does not need <windows.h>.

struct NativeHotkey {
    unsigned modifiers = 0;
    unsigned virtualKey = 0;
    bool valid = false;
};

namespace hotkeydetail {
constexpr unsigned modAlt = 0x1, modControl = 0x2, modShift = 0x4, modWin = 0x8;

inline unsigned virtualKeyFor(int key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return static_cast<unsigned>(key);
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return static_cast<unsigned>(key);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return 0x70u + static_cast<unsigned>(key - Qt::Key_F1);
    switch (key) {
    case Qt::Key_Space: return 0x20;
    case Qt::Key_PageUp: return 0x21;
    case Qt::Key_PageDown: return 0x22;
    case Qt::Key_End: return 0x23;
    case Qt::Key_Home: return 0x24;
    case Qt::Key_Left: return 0x25;
    case Qt::Key_Up: return 0x26;
    case Qt::Key_Right: return 0x27;
    case Qt::Key_Down: return 0x28;
    case Qt::Key_Insert: return 0x2D;
    case Qt::Key_Delete: return 0x2E;
    case Qt::Key_Tab: return 0x09;
    case Qt::Key_Return:
    case Qt::Key_Enter: return 0x0D;
    default: return 0;
    }
}

inline NativeHotkey fromChord(int key, Qt::KeyboardModifiers modifiers) {
    NativeHotkey result;
    const unsigned vk = virtualKeyFor(key);
    if (vk == 0) return result;
    unsigned mods = 0;
    if (modifiers.testFlag(Qt::ControlModifier)) mods |= modControl;
    if (modifiers.testFlag(Qt::AltModifier)) mods |= modAlt;
    if (modifiers.testFlag(Qt::MetaModifier)) mods |= modWin;
    if (mods == 0) return result;  // Shift alone or no modifier is too easy to type by accident.
    if (modifiers.testFlag(Qt::ShiftModifier)) mods |= modShift;
    result.modifiers = mods;
    result.virtualKey = vk;
    result.valid = true;
    return result;
}
}  // namespace hotkeydetail

inline NativeHotkey nativeHotkey(const QKeySequence &sequence) {
    if (sequence.count() != 1) return {};
    const auto combination = sequence[0];
    return hotkeydetail::fromChord(static_cast<int>(combination.key()), combination.keyboardModifiers());
}

inline QString hotkeyText(int qtKey, int qtModifiers) {
    const auto modifiers = Qt::KeyboardModifiers(qtModifiers);
    if (!hotkeydetail::fromChord(qtKey, modifiers).valid) return {};
    const Qt::KeyboardModifiers relevant = modifiers
        & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
    return QKeySequence(QKeyCombination(relevant, static_cast<Qt::Key>(qtKey))).toString(QKeySequence::PortableText);
}
