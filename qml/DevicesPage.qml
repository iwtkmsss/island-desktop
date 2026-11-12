import QtQuick
import QtQuick.Controls

Item {
    id: page
    required property IslandColors colors
    required property QtObject power
    required property QtObject bluetooth
    readonly property int maxHeight: 226
    readonly property bool hasList: bluetooth.available && bluetooth.devices.length > 0
    readonly property real listTop: (power.present ? batteryRow.y + batteryRow.height + 14 : 0) + sectionRow.height + 8
    implicitHeight: Math.min(maxHeight, hasList ? listTop + list.contentHeight : listTop + emptyText.implicitHeight + 24)
    onVisibleChanged: if (visible) bluetooth.refresh()
    Component.onCompleted: if (visible) bluetooth.refresh()

    function iconFor(kind) {
        return ["headphones", "keyboard", "mouse", "phone", "computer", "gamepad"].indexOf(kind) >= 0 ? kind : "device"
    }

    Item {
        id: batteryRow
        visible: page.power.present
        width: parent.width; height: page.power.present ? 44 : 0
        LineIcon { id: batteryGlyph; x: 0; y: 4; width: 24; height: 24; name: "battery"; color: page.colors.text }
        Text {
            id: percentText
            x: 34; y: -2
            text: page.power.percent >= 0 ? page.power.percent + "%" : "—"
            color: page.colors.text; font.pixelSize: 30; font.weight: Font.DemiBold; font.letterSpacing: -0.5
        }
        Text {
            anchors.left: percentText.right; anchors.leftMargin: 12
            anchors.baseline: percentText.baseline
            text: page.power.charging ? "Заряджається" : "Від батареї"
            color: page.colors.secondary; font.pixelSize: 12
        }
        Rectangle {
            y: 38; width: parent.width; height: 3; radius: 1.5; color: page.colors.secondary
            Rectangle { width: parent.width * Math.max(0, page.power.percent) / 100; height: parent.height; radius: 1.5; color: page.colors.text }
        }
    }
    Item {
        id: sectionRow
        y: page.power.present ? batteryRow.y + batteryRow.height + 14 : 0
        width: parent.width; height: 16
        Text { text: "Bluetooth"; color: page.colors.secondary; font.pixelSize: 11; font.weight: Font.DemiBold }
        Text {
            anchors.right: parent.right
            visible: page.bluetooth.available && page.bluetooth.connectedCount > 0
            text: page.bluetooth.connectedCount + " підключено"
            color: page.colors.secondary; font.pixelSize: 11
        }
    }
    Flickable {
        id: list
        visible: page.hasList
        x: 0; y: page.listTop; width: parent.width; height: Math.max(0, page.implicitHeight - page.listTop)
        clip: true
        contentWidth: width; contentHeight: devicesColumn.height
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            id: bar
            policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            implicitWidth: 4
            contentItem: Rectangle { implicitWidth: 4; radius: 2; color: page.colors.secondary; opacity: bar.pressed ? 0.9 : 0.55 }
            background: null
        }
        Column {
            id: devicesColumn
            width: list.width - (list.contentHeight > list.height ? 10 : 0)
            Repeater {
                model: page.bluetooth.devices
                Item {
                    id: row
                    required property var modelData
                    readonly property bool connected: modelData.connected === true
                    width: devicesColumn.width; height: 40
                    opacity: connected ? 1 : 0.55
                    LineIcon { x: 0; anchors.verticalCenter: parent.verticalCenter; width: 19; height: 19; name: page.iconFor(row.modelData.kind); color: page.colors.text }
                    Text {
                        x: 31; anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, parent.width - 31 - status.implicitWidth - 12)
                        text: row.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight
                        color: page.colors.text; font.pixelSize: 13; font.weight: row.connected ? Font.DemiBold : Font.Normal
                    }
                    Text {
                        id: status
                        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                        text: !row.connected ? "Не підключено" : row.modelData.battery >= 0 ? row.modelData.battery + "%" : "Підключено"
                        color: page.colors.secondary; font.pixelSize: 12
                    }
                }
            }
        }
    }
    Text {
        id: emptyText
        visible: !page.hasList
        x: 0; y: page.listTop + 12; width: parent.width
        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; textFormat: Text.PlainText
        text: !page.bluetooth.available ? (page.bluetooth.error.length > 0 ? page.bluetooth.error : "Bluetooth вимкнено або недоступний")
            : "Немає сполучених пристроїв Bluetooth"
        color: page.colors.secondary; font.pixelSize: 12
    }
}
