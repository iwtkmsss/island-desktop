import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    required property QtObject colors
    required property QtObject model
    required property QtObject media
    required property QtObject monitors
    required property QtObject startup
    required property QtObject hotkeys
    // Visible height stays as before the "Система" group; the rest scrolls.
    implicitHeight: monitorRow.y + monitorRow.height

    function reveal(item) {
        const top = item.mapToItem(content, 0, 0).y, bottom = top + item.height + 6
        if (top < scroller.contentY) scroller.contentY = Math.max(0, top - 6)
        else if (bottom > scroller.contentY + scroller.height) scroller.contentY = Math.min(scroller.contentHeight - scroller.height, bottom - scroller.height)
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        clip: true
        contentWidth: width; contentHeight: content.height
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            id: bar
            policy: scroller.contentHeight > scroller.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            implicitWidth: 4
            contentItem: Rectangle { implicitWidth: 4; radius: 2; color: page.colors.secondary; opacity: bar.pressed ? 0.9 : 0.55 }
            background: null
        }
        Item {
        id: content
        width: scroller.width - 10
        height: systemGroup.y + systemGroup.height + 2
        Text { y: 1; text: "Налаштування"; color: page.colors.text; font.pixelSize: 19; font.weight: Font.DemiBold }
        RowLayout {
            y: 41; width: parent.width; spacing: 12
            Repeater {
                model: [{ key: "light", label: "Біла", icon: "sun", bg: "#ffffff", fg: "#171717" },
                        { key: "dark", label: "Чорна", icon: "moon", bg: "#090909", fg: "#f5f5f5" }]
                Button {
                    id: themeButton
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 61
                    hoverEnabled: true
                    Accessible.name: "Тема: " + modelData.label
                    onClicked: page.model.theme = modelData.key
                    background: Rectangle {
                        color: themeButton.modelData.bg; radius: 13
                        border.color: page.model.theme === themeButton.modelData.key || themeButton.visualFocus ? page.colors.text : page.colors.line
                        border.width: page.model.theme === themeButton.modelData.key || themeButton.visualFocus ? 2 : 1
                    }
                    contentItem: RowLayout {
                        anchors.fill: parent; anchors.margins: 16; spacing: 10
                        LineIcon { name: themeButton.modelData.icon; color: themeButton.modelData.fg; Layout.preferredWidth: 19; Layout.preferredHeight: 19 }
                        Text { text: themeButton.modelData.label; color: themeButton.modelData.fg; font.pixelSize: 13; Layout.fillWidth: true }
                        LineIcon { name: "check"; color: themeButton.modelData.fg; visible: page.model.theme === themeButton.modelData.key; Layout.preferredWidth: 17; Layout.preferredHeight: 17 }
                    }
                }
            }
        }
        Rectangle { y: 120; width: parent.width; height: 1; color: page.colors.line }
        RowLayout {
            id: motionRow
            y: 130; width: parent.width
            Column {
                Layout.fillWidth: true; spacing: 5
                Text { text: "Менше руху"; color: page.colors.text; font.pixelSize: 13 }
                Text { text: "Без переходів і постійних анімацій"; color: page.colors.muted; font.pixelSize: 11 }
            }
            Switch {
                id: motionSwitch
                checked: page.model.reducedMotion
                Accessible.name: "Менше руху"
                onToggled: page.model.reducedMotion = checked
                indicator: Rectangle {
                    x: motionSwitch.leftPadding; y: (motionSwitch.height - height) / 2
                    width: 38; height: 22; radius: 11
                    color: motionSwitch.checked ? page.colors.text : page.colors.raised
                    border.color: motionSwitch.visualFocus ? page.colors.text : page.colors.line
                    border.width: motionSwitch.visualFocus ? 2 : 1
                    Rectangle {
                        x: motionSwitch.checked ? 19 : 3; y: 3
                        width: 16; height: 16; radius: 8
                        color: motionSwitch.checked ? page.colors.inverse : page.colors.secondary
                    }
                }
            }
        }
        RowLayout {
            id: playerRow
            y: motionRow.y + motionRow.implicitHeight + 18
            width: parent.width; height: 34; spacing: 16
            Text { text: "Музичний плеєр"; color: page.colors.text; font.pixelSize: 13; Layout.fillWidth: true }
            ComboBox {
                id: sourcePicker
                Layout.preferredWidth: 276; Layout.preferredHeight: 34
                model: page.media.sources; textRole: "label"; valueRole: "id"
                currentIndex: {
                    for (let i = 0; i < page.media.sources.length; ++i)
                        if (page.media.sources[i].id === page.media.selectedSource) return i
                    return -1
                }
                displayText: currentIndex >= 0 ? currentText : "Плеєр недоступний"
                onActivated: page.media.selectSource(currentValue)
                Accessible.name: "Джерело музики"
                background: Rectangle {
                    radius: 8; color: sourcePicker.hovered ? page.colors.hover : page.colors.raised
                    border.color: sourcePicker.visualFocus ? page.colors.secondary : page.colors.line
                }
                contentItem: Text {
                    text: sourcePicker.displayText; textFormat: Text.PlainText; color: page.colors.text
                    font.pixelSize: 12; verticalAlignment: Text.AlignVCenter
                    leftPadding: 12; rightPadding: 32; elide: Text.ElideRight
                }
                indicator: LineIcon { x: sourcePicker.width - 24; y: 10; width: 14; height: 14; name: "down"; color: page.colors.secondary }
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: sourcePicker.width - 10; height: 32
                    contentItem: Text { text: modelData.label; textFormat: Text.PlainText; color: page.colors.text; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                    background: Rectangle { radius: 6; color: highlighted ? page.colors.hover : "transparent" }
                    highlighted: sourcePicker.highlightedIndex === index
                }
                popup: Popup {
                    y: -implicitHeight - 6; width: sourcePicker.width; padding: 5
                    implicitHeight: Math.min(150, contentItem.contentHeight) + 10
                    contentItem: ListView { clip: true; model: sourcePicker.popup.visible ? sourcePicker.delegateModel : null; currentIndex: sourcePicker.highlightedIndex }
                    background: Rectangle { radius: 11; color: page.colors.raised; border.color: page.colors.line }
                }
            }
        }
        RowLayout {
            id: monitorRow
            y: playerRow.y + playerRow.height + 18
            width: parent.width; height: 34; spacing: 16
            Text { text: "Монітор"; color: page.colors.text; font.pixelSize: 13; Layout.fillWidth: true }
            ComboBox {
                id: monitorPicker
                Layout.preferredWidth: 276; Layout.preferredHeight: 34
                model: page.monitors.options; textRole: "label"; valueRole: "id"
                currentIndex: {
                    for (let i = 0; i < page.monitors.options.length; ++i)
                        if (page.monitors.options[i].id === page.monitors.selectedId) return i
                    return 0
                }
                onActivated: page.monitors.selectMonitor(currentValue)
                Accessible.name: "Монітор острівця"
                background: Rectangle {
                    radius: 8; color: monitorPicker.hovered ? page.colors.hover : page.colors.raised
                    border.color: monitorPicker.visualFocus ? page.colors.secondary : page.colors.line
                }
                contentItem: Text {
                    text: monitorPicker.displayText; textFormat: Text.PlainText; color: page.colors.text
                    font.pixelSize: 12; verticalAlignment: Text.AlignVCenter
                    leftPadding: 12; rightPadding: 32; elide: Text.ElideRight
                }
                indicator: LineIcon { x: monitorPicker.width - 24; y: 10; width: 14; height: 14; name: "down"; color: page.colors.secondary }
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: monitorPicker.width - 10; height: 32
                    contentItem: Text { text: modelData.label; textFormat: Text.PlainText; color: page.colors.text; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                    background: Rectangle { radius: 6; color: highlighted ? page.colors.hover : "transparent" }
                    highlighted: monitorPicker.highlightedIndex === index
                }
                popup: Popup {
                    y: -implicitHeight - 6; width: monitorPicker.width; padding: 5
                    implicitHeight: Math.min(180, contentItem.contentHeight) + 10
                    contentItem: ListView { clip: true; model: monitorPicker.popup.visible ? monitorPicker.delegateModel : null; currentIndex: monitorPicker.highlightedIndex }
                    background: Rectangle { radius: 11; color: page.colors.raised; border.color: page.colors.line }
                }
            }
        }
        Column {
            id: systemGroup
            y: monitorRow.y + monitorRow.height + 18
            width: parent.width; spacing: 14
            Rectangle { width: parent.width; height: 1; color: page.colors.line }
            Text { text: "Система"; color: page.colors.secondary; font.pixelSize: 11; font.weight: Font.DemiBold }
            Column {
                width: parent.width; spacing: 5
                RowLayout {
                    width: parent.width
                    Text { text: "Запускати з Windows"; color: page.colors.text; font.pixelSize: 13; Layout.fillWidth: true }
                    Switch {
                        id: startupSwitch
                        enabled: page.startup.available
                        checked: page.startup.enabled
                        Accessible.name: "Запускати з Windows"
                        onToggled: page.startup.enabled = checked
                        opacity: enabled ? 1 : 0.4
                        indicator: Rectangle {
                            x: startupSwitch.leftPadding; y: (startupSwitch.height - height) / 2
                            width: 38; height: 22; radius: 11
                            color: startupSwitch.checked ? page.colors.text : page.colors.raised
                            border.color: startupSwitch.visualFocus ? page.colors.text : page.colors.line
                            border.width: startupSwitch.visualFocus ? 2 : 1
                            Rectangle {
                                x: startupSwitch.checked ? 19 : 3; y: 3
                                width: 16; height: 16; radius: 8
                                color: startupSwitch.checked ? page.colors.inverse : page.colors.secondary
                            }
                        }
                    }
                }
                Text {
                    visible: page.startup.error.length > 0
                    width: parent.width; text: page.startup.error; textFormat: Text.PlainText
                    color: page.colors.secondary; font.pixelSize: 11; wrapMode: Text.WordWrap
                }
            }
            HotkeyField {
                id: toggleKey
                width: parent.width; colors: page.colors; hotkeys: page.hotkeys
                label: "Показати / сховати острівець"
                sequence: page.hotkeys.toggleSequence; active: page.hotkeys.toggleActive
                onSequenceChosen: s => page.hotkeys.toggleSequence = s
                onCaptureStarted: page.reveal(toggleKey)
            }
            HotkeyField {
                id: playKey
                width: parent.width; colors: page.colors; hotkeys: page.hotkeys
                label: "Пауза / відтворення"
                sequence: page.hotkeys.playPauseSequence; active: page.hotkeys.playPauseActive
                onSequenceChosen: s => page.hotkeys.playPauseSequence = s
                onCaptureStarted: page.reveal(playKey)
            }
        }
        }
    }
}
