import QtQuick
import QtQuick.Controls

Slider {
    id: control
    required property QtObject volume
    required property QtObject colors
    property string accessibleLabel: "Системна гучність"
    from: 0; to: 100; stepSize: 1
    implicitWidth: 180; implicitHeight: 26
    padding: 0
    enabled: volume.available
    Accessible.name: accessibleLabel
    Component.onCompleted: value = volume.volume
    onMoved: volume.setVolume(Math.round(value))
    Connections {
        target: control.volume
        function onChanged() {
            if (!control.pressed) control.value = control.volume.volume
        }
    }
    background: Rectangle {
        y: (control.height - height) / 2
        width: control.width; height: control.visualFocus ? 5 : 4; radius: 3
        color: control.colors.line
        Rectangle {
            width: parent.width * control.visualPosition; height: parent.height; radius: 3
            color: control.colors.text
            opacity: control.volume.muted ? 0.4 : 1
        }
    }
    handle: Rectangle {
        x: control.visualPosition * (control.width - width)
        y: (control.height - height) / 2
        width: control.pressed ? 13 : 10; height: width; radius: width / 2
        color: control.colors.text
        border.color: control.colors.surface; border.width: 1
    }
}
