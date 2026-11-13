import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool islandInteractive: true
    required property QtObject colors
    property string iconName: ""
    property bool primary: false
    property bool selected: false
    property string hint: ""
    implicitWidth: text.length > 0 ? contentItem.implicitWidth + 28 : 34
    implicitHeight: 34
    padding: 0
    leftPadding: text.length > 0 ? 11 : 0
    rightPadding: leftPadding
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: hint.length > 0 ? hint : text
    ToolTip.visible: hovered && hint.length > 0
    ToolTip.text: hint
    ToolTip.delay: 650
    TextMetrics {
        id: labelMetrics
        text: control.text
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }
    background: Rectangle {
        radius: height / 2
        color: control.primary ? control.colors.text
             : control.down ? control.colors.line
             : control.hovered ? control.colors.hover
             : control.selected ? control.colors.raised : "transparent"
        border.width: control.visualFocus ? 1.5 : control.selected && !control.primary ? 1 : 0
        border.color: control.visualFocus ? control.colors.secondary : control.colors.line
        opacity: !control.enabled ? 0.4 : control.primary && control.down ? 0.84 : 1
    }
    contentItem: Item {
        implicitWidth: (glyph.visible ? 17 : 0) + (glyph.visible && label.visible ? 7 : 0)
                       + (label.visible ? Math.ceil(labelMetrics.advanceWidth) : 0)
        implicitHeight: 20
        opacity: control.enabled ? 1 : 0.4
        Row {
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.implicitWidth)
            height: 20
            spacing: glyph.visible && label.visible ? 7 : 0
            LineIcon {
                id: glyph
                visible: control.iconName.length > 0
                name: control.iconName
                width: 17; height: 17
                anchors.verticalCenter: parent.verticalCenter
                color: control.primary ? control.colors.inverse : control.colors.text
            }
            Text {
                id: label
                visible: control.text.length > 0
                width: visible ? Math.max(0, parent.width - (glyph.visible ? glyph.width + parent.spacing : 0)) : 0
                height: 20
                text: control.text
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                font.pixelSize: 12
                font.weight: control.selected || control.primary ? Font.DemiBold : Font.Medium
                color: control.primary ? control.colors.inverse : control.colors.text
            }
        }
    }
}
