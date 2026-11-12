import QtQuick

Item {
    id: meter
    required property QtObject audio
    required property QtObject colors
    property bool reducedMotion: false
    implicitWidth: 132; implicitHeight: 32
    Accessible.ignored: true
    Repeater {
        model: 8
        Rectangle {
            required property int index
            readonly property real magnitude: meter.audio.active && !meter.reducedMotion && meter.audio.bands.length > index
                ? Math.max(0, Math.min(1, meter.audio.bands[index])) : 0
            x: index * (meter.width / 8)
            width: Math.max(2, meter.width / 8 - 5)
            height: 2 + magnitude * (meter.height - 2)
            anchors.bottom: parent.bottom
            radius: Math.min(3, width / 2)
            color: meter.colors.text
            opacity: 0.35 + magnitude * 0.65
            Behavior on height { NumberAnimation { duration: meter.reducedMotion ? 0 : 45 } }
        }
    }
}
