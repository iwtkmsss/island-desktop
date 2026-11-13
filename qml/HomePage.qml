import QtQuick
import QtQuick.Controls
Item {
    id: page
    required property QtObject colors
    required property QtObject model
    required property QtObject interaction
    required property QtObject audio
    required property QtObject desktop
    required property QtObject volume
    required property QtObject power
    required property QtObject bluetooth
    property bool active: false
    readonly property bool popupOpen: desktopLoader.item ? desktopLoader.item.popupOpen : false
    // Tasks (64) + 22 + clock block (76); the tray row adds 58 below when opened.
    // Uses the target, not the animated value: Main animates the page height.
    // Stable during expansion, including when the animated width wraps the date.
    property bool trayExpanded: false
    property real trayProgress: trayExpanded ? 1 : 0
    Behavior on trayProgress { NumberAnimation { duration: page.model.reducedMotion ? 0 : 380; easing.type: Easing.OutCubic } }
    implicitHeight: desktop.available ? 162 + (trayExpanded ? 58 : 0) : 132
    readonly property var metricParts: desktop.metrics.split("·")
    Binding { target: page.desktop; property: "metricsActive"; value: page.active }
    IslandEyes {
        objectName: "homePageEyes"
        anchors.horizontalCenter: parent.horizontalCenter
        y: page.desktop.available ? 3 : 48
        width: 48; height: 28
        scale: page.desktop.available ? 1 : 2
        visible: !page.desktop.available && page.model.eyesEnabled
        eyeColor: page.colors.text; pupilColor: page.colors.surface
        running: visible && page.active
        reducedMotion: page.model.reducedMotion
        gazeX: page.interaction.gazeX; gazeY: page.interaction.gazeY
        activitySerial: page.interaction.pointerActivity
        audioActive: page.audio.enabled && page.audio.active
        audioLevel: page.audio.level; bands: page.audio.bands
    }
    Text {
        anchors.centerIn: parent
        visible: !page.desktop.available && !page.model.eyesEnabled
        text: "Очі вимкнено в налаштуваннях"
        color: page.colors.secondary; font.pixelSize: 12
    }
    Loader {
        id: desktopLoader
        y: 0; width: parent.width; height: 220
        active: page.desktop.available
        Component.onCompleted: {
            if (active) setSource("DesktopPanel.qml", {colors: page.colors, desktop: page.desktop,
                volume: page.volume, power: page.power, bluetooth: page.bluetooth, trayY: 180})
        }
    }
    Binding { target: desktopLoader.item; property: "trayOpacity"; value: page.trayProgress; when: desktopLoader.item !== null }
    Item {
        id: bottomArea
        y: 86; width: parent.width
        height: 76
        visible: page.desktop.available
        Column {
            id: stats
            anchors.left: parent.left
            y: clock.y + clockText.y + clockText.height / 2 - height / 2
            width: Math.min(150, Math.max(96, parent.width * 0.30))
            spacing: 7
            Text {
                width: parent.width
                text: String(page.metricParts[0] || "CPU  —").trim()
                color: page.colors.muted; font.pixelSize: 11
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: String(page.metricParts[1] || "RAM  —").trim()
                color: page.colors.muted; font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
        Column {
            id: clock
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            width: parent.width - stats.width - 16
            spacing: 3
            Text {
                id: clockText
                objectName: "homeClock"
                width: parent.width
                text: page.model.clockTime + ":" + page.model.clockSeconds
                horizontalAlignment: Text.AlignRight
                color: page.colors.text; font.pixelSize: 44; font.weight: Font.Light
                font.features: { "tnum": 1 }
                fontSizeMode: Text.HorizontalFit; minimumPixelSize: 24
            }
            Text {
                id: dateText
                width: parent.width
                text: page.model.clockWeekday + " · " + page.model.clockDate
                horizontalAlignment: Text.AlignRight
                color: page.colors.secondary; font.pixelSize: width < 260 ? 12 : 14
                wrapMode: Text.WordWrap
            }
        }
        // Round toggle centred between the RAM line and the lower edge (island edge
        // when collapsed, the tray divider when opened): equal gaps above and below.
        Button {
            id: trayToggle
            objectName: "homeTrayToggle"
            x: 0; width: 28; height: 28
            readonly property real lowerEdge: bottomArea.height + 26 - 16 * page.trayProgress
            // The RAM Text box ends ~6 px below its glyphs (descent + line gap).
            y: (stats.y + stats.height - 6 + lowerEdge) / 2 - height / 2
            hoverEnabled: true; padding: 0
            Accessible.name: page.trayExpanded ? "Сховати трей" : "Показати трей"
            ToolTip.text: Accessible.name; ToolTip.visible: hovered; ToolTip.delay: 650
            onClicked: page.trayExpanded = !page.trayExpanded
            background: Rectangle {
                radius: width / 2
                color: trayToggle.down ? page.colors.line : trayToggle.hovered || trayToggle.visualFocus ? page.colors.hover : page.colors.raised
                border.width: trayToggle.visualFocus ? 1 : 0; border.color: page.colors.secondary
            }
            contentItem: Item {
                Canvas {
                    id: chevron
                    anchors.centerIn: parent; width: 12; height: 8
                    rotation: 180 * page.trayProgress
                    property color ink: page.colors.text
                    onInkChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d"); ctx.reset()
                        ctx.strokeStyle = ink; ctx.lineWidth = 1.8; ctx.lineCap = "round"; ctx.lineJoin = "round"
                        ctx.beginPath(); ctx.moveTo(1.5, 2); ctx.lineTo(6, 6.2); ctx.lineTo(10.5, 2); ctx.stroke()
                    }
                }
            }
        }
    }
}
