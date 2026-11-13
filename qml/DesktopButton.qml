import QtQuick
import QtQuick.Controls
Button {
    id: button
    required property QtObject colors
    property bool islandInteractive: true
    property string symbol: ""
    property string hint: ""
    implicitWidth: 40; implicitHeight: 40
    padding: 9
    hoverEnabled: true
    Accessible.name: hint
    ToolTip.text: hint; ToolTip.visible: hovered && hint.length > 0; ToolTip.delay: 650
    background: Rectangle {
        radius: 10
        color: button.down ? button.colors.line : button.hovered || button.visualFocus ? button.colors.hover : "transparent"
        border.width: button.visualFocus ? 1 : 0; border.color: button.colors.secondary
    }
    contentItem: Text { text: button.symbol; color: button.colors.text; font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
}
