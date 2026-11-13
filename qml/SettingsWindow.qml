import QtQuick
import QtQuick.Controls
import QtQuick.Window

Window {
    id: settings
    required property QtObject model
    required property QtObject media
    required property QtObject monitors
    required property QtObject startup
    required property QtObject hotkeys
    readonly property IslandColors colors: IslandColors { light: settings.model.theme === "light" }
    title: "Island Desktop — Налаштування"
    flags: Qt.Window
    color: colors.surface
    minimumWidth: 540; minimumHeight: 400
    width: Math.min(620, Screen.desktopAvailableWidth-64)
    height: Math.min(740, Screen.desktopAvailableHeight-64)
    visible: model.settingsOpen
    onClosing: model.settingsOpen = false
    onVisibleChanged: {
        if (visible) { raise(); requestActivate() }
        else { hotkeys.capturing = false; if (model.settingsOpen) model.settingsOpen = false }
    }
    Shortcut { sequence: "Escape"; enabled: !settings.hotkeys.capturing; onActivated: settings.model.settingsOpen = false }
    SettingsPage {
        anchors.fill: parent; anchors.margins: 24
        visible: settings.visible
        colors: settings.colors; model: settings.model; media: settings.media
        monitors: settings.monitors; startup: settings.startup; hotkeys: settings.hotkeys
    }
}
