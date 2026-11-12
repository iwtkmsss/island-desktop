import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property IslandColors colors
    required property QtObject hotkeys
    property string label: ""
    property string sequence: ""
    property bool active: false
    property bool capturing: false
    readonly property string note: capturing ? "Esc — скасувати · Backspace — вимкнути"
                                 : sequence.length > 0 && !active ? "Зайнято іншою програмою" : ""
    signal sequenceChosen(string sequence)
    signal captureStarted()

    implicitHeight: 34 + (note.length > 0 ? 18 : 0)
    Accessible.name: label

    function setCapturing(on) {
        if (capturing === on) return
        capturing = on
        hotkeys.capturing = on
        if (on) { field.forceActiveFocus(); captureStarted() }
    }
    onVisibleChanged: if (!visible) setCapturing(false)
    Component.onDestruction: if (capturing) hotkeys.capturing = false

    RowLayout {
        width: parent.width; height: 34; spacing: 16
        Text { text: root.label; textFormat: Text.PlainText; color: root.colors.text; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight }
        Button {
            id: field
            Layout.preferredWidth: 276; Layout.preferredHeight: 34
            padding: 0
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.name: root.label
            Accessible.description: text
            text: root.capturing ? "Натисніть комбінацію…" : root.sequence.length > 0 ? root.sequence : "Вимкнено"
            onClicked: root.setCapturing(true)
            onActiveFocusChanged: if (!activeFocus) root.setCapturing(false)
            Keys.onPressed: event => {
                if (!root.capturing) return
                event.accepted = true
                if (event.key === Qt.Key_Escape) root.setCapturing(false)
                else if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                    root.setCapturing(false)
                    root.sequenceChosen("")
                } else {
                    const s = root.hotkeys.sequenceFromKey(event.key, event.modifiers)
                    if (s.length > 0) { root.setCapturing(false); root.sequenceChosen(s) }
                }
            }
            background: Rectangle {
                radius: 8
                color: field.hovered || root.capturing ? root.colors.hover : root.colors.raised
                border.color: field.visualFocus || root.capturing ? root.colors.secondary : root.colors.line
                border.width: field.visualFocus || root.capturing ? 1.5 : 1
            }
            contentItem: Text {
                text: field.text; textFormat: Text.PlainText
                color: root.capturing || root.sequence.length === 0 ? root.colors.secondary : root.colors.text
                font.pixelSize: 12; verticalAlignment: Text.AlignVCenter
                leftPadding: 12; rightPadding: 12; elide: Text.ElideRight
            }
        }
    }
    Text {
        visible: root.note.length > 0
        y: 38; width: parent.width; height: 14
        text: root.note; textFormat: Text.PlainText
        color: root.colors.secondary; font.pixelSize: 11
        horizontalAlignment: Text.AlignRight; elide: Text.ElideRight
    }
}
