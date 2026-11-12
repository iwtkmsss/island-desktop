import QtQuick

Item {
    id: art
    property bool playing: false
    property bool reducedMotion: false
    implicitWidth: 128
    implicitHeight: 128
    Rectangle {
        anchors.fill: parent
        radius: 18
        color: "#202020"
        border.color: "#3b3b3b"
        Item {
            id: record
            width: parent.width * 0.82; height: width
            anchors.centerIn: parent
            RotationAnimator on rotation {
                from: 0; to: 360; duration: 12000; loops: Animation.Infinite
                running: art.playing && art.visible && !art.reducedMotion
            }
            Repeater {
                model: 8
                Rectangle {
                    required property int index
                    anchors.centerIn: parent
                    width: record.width - index * 8; height: width; radius: width / 2
                    color: index === 0 ? "#111111" : "transparent"
                    border.color: index % 2 === 0 ? "#353535" : "#282828"
                    border.width: 1
                }
            }
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.3; height: width; radius: width / 2
                color: "#dedede"
                Rectangle { anchors.centerIn: parent; width: 5; height: 5; radius: 3; color: "#202020" }
                Rectangle { x: parent.width * 0.6; y: 5; width: 2; height: 7; radius: 1; color: "#888888" }
            }
        }
        Rectangle { x: 11; y: 11; width: 3; height: 3; radius: 2; color: "#b0b0b0" }
    }
}
