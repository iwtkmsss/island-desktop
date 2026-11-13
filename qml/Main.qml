import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window

Window {
    id: root
    required property QtObject desktop
    required property QtObject model
    required property QtObject media
    required property QtObject interaction
    required property QtObject volume
    required property QtObject audio
    required property QtObject monitors
    required property QtObject power
    required property QtObject bluetooth
    title: "Island Desktop"
    visible: true
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    readonly property IslandColors colors: IslandColors { light: root.model.theme === "light" }
    readonly property int motionDuration: model.reducedMotion ? 0 : 380
    readonly property bool homeSection: model.section === "home"
    property bool interactionHold: desktop.menuOpen || homePage.popupOpen
    readonly property bool mediaSection: model.section === "media"
    readonly property bool devicesSection: model.section === "devices"
    readonly property var connectedDevice: {
        const list = bluetooth.available ? bluetooth.devices : []
        for (let i = 0; i < list.length; ++i) if (list[i].connected) return list[i]
        return null
    }
    readonly property bool timerView: model.section === "time" && model.timeView === "timer"
    readonly property bool volumeEvent: model.eventKind === "volume" && model.eventTitle.length > 0
    readonly property bool showHomeEyes: homeSection && model.eyesEnabled && model.eventTitle.length === 0
    readonly property string dockEdge: monitors.dockEdge
    readonly property real compactTargetWidth: model.eventTitle.length > 0 ? 268 : showHomeEyes ? 148 : mediaSection ? 312 : 238
    property real compactWidth: compactTargetWidth
    Behavior on compactWidth { NumberAnimation { duration: root.motionDuration; easing.type: Easing.OutCubic } }
    // Home has its own header geometry (crown, eyes, tighter offsets). One shared
    // progress morphs it when switching tabs instead of jumping between layouts.
    readonly property bool homeLayout: desktop.available && homeSection
    property real homeProgress: homeLayout ? 1 : 0
    Behavior on homeProgress { NumberAnimation { duration: root.motionDuration; easing.type: Easing.OutCubic } }
    // One progress value keeps both dimensions in step, including reversals
    // when a double click interrupts the first click's opening animation.
    property real expansionProgress: model.expanded ? 1 : 0
    Behavior on expansionProgress {
        NumberAnimation { id: expansionAnimation; duration: root.motionDuration; easing.type: Easing.OutCubic }
    }
    readonly property real collapsedWidth: compactWidth - interaction.proximity * 5
    readonly property real shellWidth: collapsedWidth
        + (Math.min(520, Screen.desktopAvailableWidth - 40) - collapsedWidth) * expansionProgress
    readonly property Item activePage: homeSection ? homePage : model.section === "time" ? timePage : devicesSection ? devicesPage : musicPage
    readonly property real contentTop: 24 - 6 * homeProgress
    readonly property real crownHeight: desktop.available && model.eyesEnabled ? 10 * expansionProgress * homeProgress : 0
    readonly property real eyeCenterY: homeLayout ? homeHeaderEyes.y + 14 : shellHeight/2
    readonly property real pageTop: 58 - 12 * homeProgress
    // Target values, not the animated ones, so the height animation is not retargeted every frame.
    readonly property real expandedHeight: (homeLayout ? 18 + 46 : 24 + 58) + activePage.implicitHeight + 24 + 2
    property real pageHeight: expandedHeight
    Behavior on pageHeight { NumberAnimation { id: pageHeightAnimation; duration: root.motionDuration; easing.type: Easing.OutCubic } }
    readonly property real collapsedHeight: 50 + interaction.proximity * 1.5
    readonly property real shellHeight: collapsedHeight + (pageHeight - collapsedHeight) * expansionProgress
    property real wavePhase: 0
    readonly property bool flowing: mediaSection && audio.enabled && audio.active && !model.reducedMotion && visible && !interaction.suppressed && !model.passThroughActive
    // Artwork glow: only the outline is tinted; themes stay black/white. The glow
    // is always light: a dark or grey outline would merge with the windows behind
    // the island. Grey or black-and-white artwork falls back to a neutral light tone.
    readonly property bool accentUsable: media.hasAccent && media.accentColor.hslHue >= 0
    readonly property color glowAdapted: {
        if (!accentUsable) return colors.light ? Qt.hsla(0, 0, 0.66, 1) : Qt.hsla(0, 0, 0.90, 1)
        const c = media.accentColor
        const sat = Math.min(0.90, Math.max(0.50, c.hslSaturation))
        return Qt.hsla(c.hslHue, sat, colors.light ? 0.55 : 0.74, 1)
    }
    readonly property bool glowActive: mediaSection && media.available && model.artworkGlow
        && model.eventTitle.length === 0 && !interaction.suppressed && !model.passThroughActive
    onGlowAdaptedChanged: glowTint = glowAdapted
    Component.onCompleted: glowTint = glowAdapted
    property color glowTint: "#e6e6e6"
    Behavior on glowTint { ColorAnimation { duration: root.model.reducedMotion ? 0 : 600 } }
    property real glowLevel: glowActive ? 1 : 0
    Behavior on glowLevel { NumberAnimation { duration: root.model.reducedMotion ? 0 : 450; easing.type: Easing.OutCubic } }
    property real glowPulse: flowing ? 0.78 + 0.22 * Math.min(1, audio.level * 1.6) : 0.85
    Binding {
        target: root.audio; property: "enabled"
        value: root.visible && !root.interaction.suppressed && !root.model.passThroughActive && !root.model.reducedMotion
            && (root.mediaSection || root.showHomeEyes)
            && root.volume.available && !root.volume.muted && root.volume.volume > 0
    }
    // Fixed native frame, as in the supplied mod: the window is never resized
    // during open/close (that reallocates the swap chain and stutters). Clicks
    // outside the shell pass through via WS_EX_TRANSPARENT in ShellController.
    readonly property real frameWidth: Math.max(312, Math.min(520, Screen.desktopAvailableWidth - 40))
    // Reserve the expanded content even when a page is hidden or has an empty state.
    // Native geometry must not follow a tab change or media availability.
    readonly property real frameHeight: desktop.available ? 406 + 18 : 378 + 18 // fixed frame with animation headroom
    readonly property real edgeMargin: 16
    readonly property real shellX: dockEdge === "left" ? edgeMargin : dockEdge === "right" ? width-shellWidth-edgeMargin : (width-shellWidth)/2
    readonly property real shellY: dockEdge === "top" ? edgeMargin : dockEdge === "bottom" ? height-shellHeight-edgeMargin : (height-shellHeight)/2
    onFrameWidthChanged: interaction.requestFrame()
    // Geometry is applied from C++ in one native call (size + centred position).
    width: 280; height: 100
    NumberAnimation on wavePhase {
        from: 0; to: Math.PI * 2; duration: 2400; loops: Animation.Infinite
        running: root.flowing
    }
    Shortcut { sequence: "Escape"; onActivated: root.interaction.close() }
    Shortcut { sequence: "Ctrl+1"; onActivated: root.model.section = "time" }
    Shortcut { sequence: "Ctrl+2"; onActivated: root.model.section = "media" }
    Shortcut { sequence: "Ctrl+3"; onActivated: root.model.section = "devices" }
    Shortcut { sequence: "Ctrl+4"; onActivated: root.model.section = "home" }

    Item {
        id: opacityLayer
        anchors.fill: parent
        // Use the fixed native frame so elastic edges, waves and shadows fit
        // in the opacity texture without cropping or rescaling the capsule.
        layer.enabled: true
        opacity: root.model.passThroughActive ? 0.5 : 1
        Behavior on opacity {
            NumberAnimation { duration: root.model.reducedMotion ? 0 : 450; easing.type: Easing.InOutQuad }
        }
        Item {
            id: shell
            visible: !root.interaction.suppressed
            enabled: !root.model.passThroughActive
            x: root.shellX; y: root.shellY; width: root.shellWidth; height: root.shellHeight
            IslandSurface {
                anchors.fill: parent; anchors.margins: -22 // paint margin, same value as in IslandSurface
                crownHeight: root.crownHeight
                surfaceColor: root.colors.surface; inkColor: root.colors.text
                radius: 25 + 5 * root.expansionProgress
                pointerX: root.interaction.pointerX; pointerY: root.interaction.pointerY
                proximity: root.interaction.proximity
                phase: root.wavePhase
                wave: root.flowing ? 13 : 0
                bands: root.audio.bands
                glowColor: root.glowTint
                glowStrength: root.glowLevel * root.glowPulse
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: Qt.tint("#000000", Qt.rgba(root.glowTint.r, root.glowTint.g, root.glowTint.b, 0.55 * root.glowLevel))
                    shadowOpacity: (root.colors.light ? 0.17 : 0.42) + 0.18 * root.glowLevel
                    shadowBlur: 0.7; shadowVerticalOffset: 7
                }
            }
            IslandEyes {
                id: homeHeaderEyes
                objectName: "homeHeaderEyes"
                anchors.horizontalCenter: parent.horizontalCenter
                // One instance follows the shared expansion progress; no eye swap/reset.
                y: (root.collapsedHeight - 28) / 2 * (1-root.expansionProgress) - 11 * root.expansionProgress
                width: 48; height: 28
                opacity: root.homeProgress
                visible: opacity > 0 && root.desktop.available && root.model.eyesEnabled && root.model.eventTitle.length === 0
                eyeColor: root.colors.text; pupilColor: root.colors.surface
                running: visible && root.visible && !root.interaction.suppressed && !root.model.passThroughActive
                reducedMotion: root.model.reducedMotion
                gazeX: root.interaction.gazeX; gazeY: root.interaction.gazeY
                activitySerial: root.interaction.pointerActivity
                audioActive: root.audio.enabled && root.audio.active
                audioLevel: root.audio.level; bands: root.audio.bands
            }
            Item {
                anchors.fill: parent; anchors.margins: 1
                clip: true
                Button {
                    id: compact
                    width: parent.width; height: parent.height
                    visible: opacity > 0; enabled: !root.model.expanded
                    opacity: root.model.expanded ? 0 : 1
                    padding: 0; hoverEnabled: true; focusPolicy: Qt.StrongFocus
                    Accessible.name: root.homeSection ? "Відкрити головну" : "Розгорнути острівець"
                    onClicked: { root.interaction.open(); root.model.dismissEvent() }
                    background: Rectangle {
                        color: "transparent"; radius: 25
                        border.width: compact.visualFocus ? 2 : 0; border.color: root.colors.secondary
                    }
                    Behavior on opacity { NumberAnimation { duration: root.motionDuration / 3 } }
                    contentItem: Item {
                        IslandEyes {
                            objectName: "homeCompactEyes"
                            anchors.centerIn: parent
                            width: 48; height: 28
                            visible: root.showHomeEyes && !root.desktop.available
                            eyeColor: root.colors.text; pupilColor: root.colors.surface
                            running: visible && root.visible && !root.model.expanded && !root.interaction.suppressed && !root.model.passThroughActive
                            reducedMotion: root.model.reducedMotion
                            gazeX: root.interaction.gazeX; gazeY: root.interaction.gazeY
                            activitySerial: root.interaction.pointerActivity
                            audioActive: root.audio.enabled && root.audio.active
                            audioLevel: root.audio.level; bands: root.audio.bands
                        }
                        RowLayout {
                            visible: !root.showHomeEyes
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
                                    : root.homeSection ? "home"
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
                                        : root.homeSection ? "Головна"
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
                            Text {
                                visible: root.mediaSection && root.media.available && root.model.eventTitle.length === 0
                                text: musicPage.timeLabel(root.media.positionMs) + " / "
                                    + (root.media.durationMs > 0 ? musicPage.timeLabel(root.media.durationMs) : "—")
                                color: root.colors.secondary; font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: !root.homeSection && !root.mediaSection && root.model.eventTitle.length === 0
                                textFormat: Text.PlainText
                                text: root.devicesSection ? (root.power.present ? (root.power.charging ? "Заряджання" : "Батарея")
                                        : root.connectedDevice ? (root.connectedDevice.battery >= 0 ? root.connectedDevice.battery + "%" : "Підключено") : "Bluetooth")
                                    : root.model.timeView !== "timer" ? root.model.clockWeekdayShort
                                    : root.model.timerState === "running" ? "Відлік" : root.model.timerState === "paused" ? "Пауза" : root.model.timerState === "finished" ? "Готово" : "Таймер"
                                color: root.colors.secondary; font.pixelSize: 11
                            }
                        }
                    }
                }

                Item {
                    id: expandedContent
                    x: 23; y: root.contentTop; width: parent.width - 46; height: pages.y + pages.height
                    // While opening, reveal the content once the shell is tall enough;
                    // short pages never reach 190 px, so the limit follows the page.
                    opacity: root.model.expanded && (!expansionAnimation.running
                        || root.shellHeight > Math.min(190, root.expandedHeight - 24)) ? 1 : 0
                    // Stays in the scene while transparent: hiding it would collapse
                    // layout-based page heights and retarget the opening animation.
                    enabled: root.model.expanded
                    Behavior on opacity { NumberAnimation { duration: root.motionDuration * 0.5 } }
                    RowLayout {
                        id: header
                        y: 0
                        width: parent.width; spacing: 3
                        Repeater {
                            model: [{ key: "home", label: "Головна", icon: "home" }, { key: "time", label: "Час", icon: "clock" }, { key: "media", label: "Медіа", icon: "music" },
                                    { key: "devices", label: "Пристрої", icon: "bluetooth" }]
                            ActionButton {
                                required property var modelData
                                colors: root.colors; text: modelData.label; iconName: modelData.icon
                                implicitWidth: contentItem.implicitWidth + 22
                                selected: root.model.section === modelData.key
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
                        ActionButton { colors: root.colors; iconName: "up"; hint: "Згорнути · Esc"; onClicked: root.interaction.close() }
                    }
                    Item {
                        id: pages
                        x: 0; y: root.pageTop; width: parent.width
                        // Follows the animated shell so neither the outgoing nor the
                        // incoming page spills past the outline during a tab switch.
                        height: Math.max(0, root.pageHeight - root.contentTop - root.pageTop - 26)
                        clip: pageHeightAnimation.running
                        HomePage {
                            id: homePage
                            width: parent.width; height: implicitHeight; colors: root.colors; model: root.model
                            interaction: root.interaction; audio: root.audio
                            desktop: root.desktop; volume: root.volume; power: root.power; bluetooth: root.bluetooth
                            opacity: root.activePage === homePage ? 1 : 0
                            visible: opacity > 0; enabled: root.activePage === homePage
                            Behavior on opacity { NumberAnimation { duration: root.motionDuration * 0.6; easing.type: Easing.OutCubic } }
                            active: root.activePage === homePage && root.visible && root.model.expanded && !root.interaction.suppressed && !root.model.passThroughActive && root.model.eventTitle.length === 0
                        }
                        TimePage { id: timePage; width: parent.width; height: implicitHeight; colors: root.colors; model: root.model; opacity: root.activePage === timePage ? 1 : 0; visible: opacity > 0; enabled: root.activePage === timePage
                            Behavior on opacity { NumberAnimation { duration: root.motionDuration * 0.6; easing.type: Easing.OutCubic } } }
                        MusicPage { id: musicPage; width: parent.width; height: implicitHeight; colors: root.colors; media: root.media; model: root.model; audio: root.audio; volume: root.volume; opacity: root.activePage === musicPage ? 1 : 0; visible: opacity > 0; enabled: root.activePage === musicPage
                            Behavior on opacity { NumberAnimation { duration: root.motionDuration * 0.6; easing.type: Easing.OutCubic } } }
                        DevicesPage { id: devicesPage; width: parent.width; height: implicitHeight; colors: root.colors; power: root.power; bluetooth: root.bluetooth; opacity: root.activePage === devicesPage ? 1 : 0; visible: opacity > 0; enabled: root.activePage === devicesPage
                            Behavior on opacity { NumberAnimation { duration: root.motionDuration * 0.6; easing.type: Easing.OutCubic } } }
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
}
