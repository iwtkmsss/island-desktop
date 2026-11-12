import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window

Window {
    id: root
    required property QtObject model
    required property QtObject media
    required property QtObject interaction
    required property QtObject volume
    required property QtObject audio
    required property QtObject monitors
    required property QtObject startup
    required property QtObject hotkeys
    required property QtObject power
    required property QtObject bluetooth
    title: "Island Desktop"
    visible: true
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    readonly property IslandColors colors: IslandColors { light: root.model.theme === "light" }
    readonly property int motionDuration: model.reducedMotion ? 0 : 380
    readonly property bool mediaSection: model.section === "media"
    readonly property bool devicesSection: model.section === "devices"
    readonly property var connectedDevice: {
        const list = bluetooth.available ? bluetooth.devices : []
        for (let i = 0; i < list.length; ++i) if (list[i].connected) return list[i]
        return null
    }
    readonly property bool timerView: model.section === "time" && model.timeView === "timer"
    readonly property bool volumeEvent: model.eventKind === "volume" && model.eventTitle.length > 0
    readonly property real compactWidth: model.eventTitle.length > 0 ? 268 : mediaSection ? 312 : 238
    property real shellWidth: model.expanded ? Math.min(520, Screen.desktopAvailableWidth - 40)
                                           : compactWidth - interaction.proximity * 5
    readonly property Item activePage: model.settingsOpen ? settingsPage
        : model.section === "time" ? timePage : devicesSection ? devicesPage : musicPage
    readonly property real expandedHeight: 24 + 58 + activePage.implicitHeight + 24 + 2
    property real shellHeight: model.expanded ? expandedHeight : 50 + interaction.proximity * 1.5
    property real wavePhase: 0
    readonly property bool flowing: mediaSection && audio.enabled && audio.active && !model.reducedMotion && visible && !interaction.suppressed
    // Artwork glow: only the outline is tinted; themes stay black/white.
    readonly property color glowAdapted: {
        const c = media.accentColor
        const sat = Math.min(0.90, Math.max(0.45, c.hslSaturation))
        return Qt.hsla(c.hslHue, sat, colors.light ? 0.46 : 0.62, 1)
    }
    readonly property bool glowActive: mediaSection && media.available && media.hasAccent && model.artworkGlow
        && model.eventTitle.length === 0 && !interaction.suppressed && !model.settingsOpen && media.accentColor.hslHue >= 0
    onGlowAdaptedChanged: if (media.hasAccent && media.accentColor.hslHue >= 0) glowTint = glowAdapted
    Component.onCompleted: if (media.hasAccent && media.accentColor.hslHue >= 0) glowTint = glowAdapted
    property color glowTint: "#000000"
    Behavior on glowTint { ColorAnimation { duration: root.model.reducedMotion ? 0 : 600 } }
    property real glowLevel: glowActive ? 1 : 0
    Behavior on glowLevel { NumberAnimation { duration: root.model.reducedMotion ? 0 : 450; easing.type: Easing.OutCubic } }
    Binding {
        target: root.audio; property: "enabled"
        value: root.visible && !root.interaction.suppressed && !root.model.reducedMotion
            && root.mediaSection && !root.model.settingsOpen
            && root.volume.available && !root.volume.muted && root.volume.volume > 0
    }
    // Fixed native frame, as in the supplied mod: the window is never resized
    // during open/close (that reallocates the swap chain and stutters). Clicks
    // outside the shell pass through via WS_EX_TRANSPARENT in ShellController.
    readonly property real frameWidth: Math.max(312, Math.min(520, Screen.desktopAvailableWidth - 40))
    // Reserve the largest page even when it is hidden or has an empty state.
    // Native geometry must not follow a tab change or media availability.
    readonly property real frameHeight: 378 + 18 // tallest settings page + OutBack headroom
    readonly property real shellX: (width - shellWidth) / 2
    onFrameWidthChanged: interaction.requestFrame()
    // Geometry is applied from C++ in one native call (size + centred position).
    width: 280; height: 100
    Behavior on shellWidth { NumberAnimation { duration: root.motionDuration; easing.type: Easing.OutCubic } }
    Behavior on shellHeight {
        id: heightTransition
        property bool previousExpanded: false
        // True from an open/close until its animation has had time to finish.
        // Page heights may still settle during that window (layouts of a page
        // that just became visible); those retargets must keep the open/close
        // motion instead of being mistaken for a tab change.
        property bool inTransition: false
        readonly property bool resizingPage: !inTransition
        onTargetValueChanged: {
            if (previousExpanded !== root.model.expanded) { inTransition = true; transitionEnd.restart() }
            previousExpanded = root.model.expanded
        }
        NumberAnimation {
            duration: root.model.reducedMotion ? 0 : heightTransition.resizingPage ? 200 : root.motionDuration
            easing.type: heightTransition.resizingPage ? Easing.OutCubic : Easing.OutBack
            easing.overshoot: 0.55
        }
    }
    Timer { id: transitionEnd; interval: root.motionDuration + 40; onTriggered: heightTransition.inTransition = false }
    NumberAnimation on wavePhase {
        from: 0; to: Math.PI * 2; duration: 2400; loops: Animation.Infinite
        running: root.flowing
    }
    Shortcut { sequence: "Escape"; onActivated: root.interaction.close() }
    Shortcut { sequence: "Ctrl+1"; onActivated: root.model.section = "time" }
    Shortcut { sequence: "Ctrl+2"; onActivated: root.model.section = "media" }
    Shortcut { sequence: "Ctrl+3"; onActivated: root.model.section = "devices" }

    Item {
        id: shell
        visible: !root.interaction.suppressed
        x: root.shellX; y: 16; width: root.shellWidth; height: root.shellHeight
        IslandSurface {
            anchors.fill: parent; anchors.margins: -8
            surfaceColor: root.colors.surface; inkColor: root.colors.text
            radius: root.model.expanded ? 30 : 25
            pointerX: root.interaction.pointerX; pointerY: root.interaction.pointerY
            proximity: root.interaction.proximity
            phase: root.wavePhase
            wave: root.flowing ? Math.min(7, 1 + root.audio.level * 6) : 0
            bands: root.audio.bands
            glowColor: root.glowTint
            glowStrength: root.glowLevel * (root.flowing ? 0.75 + 0.25 * Math.min(1, root.audio.level * 1.6) : 0.85)
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: Qt.tint("#000000", Qt.rgba(root.glowTint.r, root.glowTint.g, root.glowTint.b, 0.55 * root.glowLevel))
                shadowOpacity: (root.colors.light ? 0.17 : 0.42) + 0.18 * root.glowLevel
                shadowBlur: 0.7; shadowVerticalOffset: 7
            }
        }
        Item {
            anchors.fill: parent; anchors.margins: 1
            clip: true
            Button {
                id: compact
                width: parent.width; height: 49
                visible: opacity > 0; enabled: !root.model.expanded
                opacity: root.model.expanded ? 0 : 1
                padding: 0; hoverEnabled: true; focusPolicy: Qt.StrongFocus
                Accessible.name: "Розгорнути острівець"
                onClicked: { root.interaction.open(); root.model.dismissEvent() }
                background: Rectangle {
                    color: "transparent"; radius: 25
                    border.width: compact.visualFocus ? 2 : 0; border.color: root.colors.secondary
                }
                Behavior on opacity { NumberAnimation { duration: root.motionDuration / 3 } }
                contentItem: RowLayout {
                    spacing: 10
                    anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18
                    Image {
                        visible: root.mediaSection && root.media.artworkUrl.length > 0 && root.model.eventTitle.length === 0
                        source: root.media.artworkUrl; sourceSize: Qt.size(64, 64)
                        fillMode: Image.PreserveAspectCrop
                        Layout.preferredWidth: 28; Layout.preferredHeight: 28
                    }
                    LineIcon {
                        visible: !(root.mediaSection && root.media.artworkUrl.length > 0) || root.model.eventTitle.length > 0
                        name: root.model.eventTitle.length > 0 ? (root.model.eventIcon || "check")
                            : root.mediaSection ? "music"
                            : root.devicesSection ? (root.power.present ? "battery" : "bluetooth")
                            : root.model.timeView === "timer" ? "timer" : "clock"
                        color: root.colors.text; Layout.preferredWidth: 19; Layout.preferredHeight: 19
                    }
                    Column {
                        Layout.fillWidth: true; spacing: 1
                        Text {
                            width: parent.width
                            text: root.model.eventTitle.length > 0 ? root.model.eventTitle
                                : root.mediaSection ? (root.media.available ? (root.media.title || "Без назви") : root.media.busy ? "Підключення плеєра…" : "Музика")
                                : root.devicesSection ? (root.power.present ? (root.power.percent >= 0 ? root.power.percent + "%" : "Батарея")
                                    : root.connectedDevice ? root.connectedDevice.name : "Пристрої")
                                : root.model.timeView === "timer" ? root.model.timerText : root.model.clockTime
                            color: root.colors.text; font.pixelSize: 13; font.weight: Font.DemiBold
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: root.mediaSection && root.media.available && root.model.eventTitle.length === 0
                            width: parent.width; text: root.media.artist || root.media.sourceName
                            textFormat: Text.PlainText
                            color: root.colors.secondary; font.pixelSize: 11; elide: Text.ElideRight
                        }
                    }
                    ActionButton {
                        visible: root.mediaSection && root.media.available && root.model.eventTitle.length === 0
                        colors: root.colors
                        iconName: root.media.playing ? "pause" : "play"
                        hint: root.media.playing ? "Призупинити" : "Відтворити"
                        enabled: root.media.canToggle && !root.media.busy
                        onClicked: root.media.togglePlayback()
                        Layout.preferredWidth: 26; Layout.preferredHeight: 26
                    }
                    Text {
                        visible: !root.mediaSection && root.model.eventTitle.length === 0
                        textFormat: Text.PlainText
                        text: root.devicesSection ? (root.power.present ? (root.power.charging ? "Заряджання" : "Батарея")
                                : root.connectedDevice ? (root.connectedDevice.battery >= 0 ? root.connectedDevice.battery + "%" : "Підключено") : "Bluetooth")
                            : root.model.timeView !== "timer" ? root.model.clockWeekdayShort
                            : root.model.timerState === "running" ? "Відлік" : root.model.timerState === "paused" ? "Пауза" : root.model.timerState === "finished" ? "Готово" : "Таймер"
                        color: root.colors.secondary; font.pixelSize: 11
                    }
                    LineIcon { name: "down"; color: root.colors.secondary; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                }
            }

            Item {
                id: expandedContent
                x: 23; y: 24; width: parent.width - 46; height: pages.y + pages.height
                // While opening, reveal the content once the shell is tall enough;
                // short pages never reach 190 px, so the limit follows the page.
                opacity: root.model.expanded && (!heightTransition.inTransition
                    || root.shellHeight > Math.min(190, root.expandedHeight - 24)) ? 1 : 0
                // Stays in the scene while transparent: hiding it would collapse
                // layout-based page heights and retarget the opening animation.
                enabled: root.model.expanded
                Behavior on opacity { NumberAnimation { duration: root.motionDuration * 0.5 } }
                RowLayout {
                    id: header
                    width: parent.width; spacing: 3
                    Repeater {
                        model: [{ key: "time", label: "Час", icon: "clock" }, { key: "media", label: "Медіа", icon: "music" },
                                { key: "devices", label: "Пристрої", icon: "bluetooth" }]
                        ActionButton {
                            required property var modelData
                            colors: root.colors; text: modelData.label; iconName: modelData.icon
                            implicitWidth: contentItem.implicitWidth + 22
                            selected: root.model.section === modelData.key && !root.model.settingsOpen
                            onClicked: root.model.section = modelData.key
                        }
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        colors: root.colors; iconName: "pin"
                        hint: root.interaction.pinned ? "Відкріпити панель" : "Закріпити відкритою"
                        selected: root.interaction.pinned
                        onClicked: root.interaction.pinned = !root.interaction.pinned
                    }
                    ActionButton {
                        colors: root.colors; iconName: "settings"; hint: "Налаштування"
                        selected: root.model.settingsOpen
                        onClicked: root.model.settingsOpen = !root.model.settingsOpen
                    }
                    ActionButton { colors: root.colors; iconName: "up"; hint: "Згорнути · Esc"; onClicked: root.interaction.close() }
                }
                Item {
                    id: pages
                    x: 0; y: 58; width: parent.width; height: root.activePage.implicitHeight
                    TimePage { id: timePage; anchors.fill: parent; colors: root.colors; model: root.model; visible: root.activePage === timePage }
                    MusicPage { id: musicPage; anchors.fill: parent; colors: root.colors; media: root.media; model: root.model; audio: root.audio; volume: root.volume; visible: root.activePage === musicPage }
                    DevicesPage { id: devicesPage; anchors.fill: parent; colors: root.colors; power: root.power; bluetooth: root.bluetooth; visible: root.activePage === devicesPage }
                    SettingsPage { id: settingsPage; anchors.fill: parent; colors: root.colors; model: root.model; media: root.media; monitors: root.monitors; startup: root.startup; hotkeys: root.hotkeys; visible: root.activePage === settingsPage }
                }
            }
            Rectangle {
                x: 20; y: parent.height - 7
                width: (parent.width - 40) * root.model.timerProgress
                height: 2; radius: 1; color: root.colors.secondary
                visible: !root.model.expanded && root.model.eventTitle.length === 0 && root.timerView && root.model.timerState !== "idle"
            }
            Rectangle {
                x: 22; y: parent.height - 7
                width: (parent.width - 44) * (root.model.eventMuted ? 0 : root.model.eventVolume / 100)
                height: 2; radius: 1; color: root.colors.secondary
                visible: !root.model.expanded && root.volumeEvent
            }
            Rectangle {
                x: 22; y: parent.height - 54; width: parent.width - 44; height: 35; radius: 18
                visible: root.model.expanded && root.model.eventTitle.length > 0
                color: root.colors.text
                Text {
                    anchors.centerIn: parent; width: Math.min(implicitWidth, parent.width - 24)
                    text: root.model.eventTitle; textFormat: Text.PlainText; elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    color: root.colors.inverse; font.pixelSize: 12
                }
                TapHandler { onTapped: root.model.dismissEvent() }
            }
        }
    }
}
