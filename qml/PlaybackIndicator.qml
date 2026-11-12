import QtQuick

Item {
    id: indicator
    property bool playing: false
    property bool reducedMotion: false
    property color color: "white"
    property QtObject audio: null
    readonly property bool live: visible && audio !== null && audio.enabled && audio.active && !reducedMotion
    implicitWidth: 23; implicitHeight: 18
    Accessible.ignored: true
    Repeater {
        model: 4
        Rectangle {
            id: bar
            required property int index
            visible: indicator.live
            x: index * 6; anchors.verticalCenter: parent.verticalCenter
            width: 2; height: indicator.live && indicator.audio.bands.length >= 8
                ? 3 + 14 * Math.max(indicator.audio.bands[index * 2], indicator.audio.bands[index * 2 + 1]) : 3
            radius: 1
            color: indicator.color
            Behavior on height { NumberAnimation { duration: indicator.reducedMotion ? 0 : 45 } }
        }
    }
    LineIcon { anchors.centerIn: parent; width: 14; height: 14; name: indicator.playing ? "play" : "pause"; color: indicator.color; visible: !indicator.live }
}
