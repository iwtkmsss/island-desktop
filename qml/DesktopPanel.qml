import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import QtQuick.Effects
Item {
    id: panel
    required property QtObject colors
    required property QtObject desktop
    required property QtObject volume
    required property QtObject power
    required property QtObject bluetooth
    property bool islandInteractive: true
    property bool popupOpen: volumePopup.opened
    // The status/tray row lives in the extra space HomePage reveals below the clock.
    property real trayY: 86
    property real trayOpacity: 1
    implicitHeight: trayY + 40
    Rectangle {
        width: parent.width; height: 64; radius: 16; color: panel.colors.raised
        DesktopButton {
            x: 4; y: 8; width: 48; height: 48; colors: panel.colors; hint: "Меню застосунків"
            onClicked: panel.desktop.applicationMenu()
            contentItem: Kirigami.Icon { source: "application-menu"; implicitWidth: 22; implicitHeight: 22; color: panel.colors.text; isMask: true }
        }
        Rectangle { x: 59; y: 20; width: 1; height: 24; color: panel.colors.line }
        ListView {
            id: tasks
            objectName: "desktopTasks"
            x: 68; y: 6; width: parent.width - x - 6; height: 52
            orientation: ListView.Horizontal; spacing: 6; clip: true
            model: panel.desktop.tasks
            readonly property real targetSize: Math.max(36, Math.min(48, (width - Math.max(0,count-1)*spacing) / Math.max(1,count)))
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar { policy: tasks.contentWidth > tasks.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
            WheelHandler { onWheel: event => { tasks.contentX=Math.max(0,Math.min(tasks.contentWidth-tasks.width,tasks.contentX-(event.angleDelta.y || event.angleDelta.x)/3));event.accepted=true } }
            delegate: DesktopButton {
                id: task
                required property int index
                required property var model
                colors: panel.colors
                width: tasks.targetSize; height: 48
                y: 0
                hint: String(model.display || model.AppName || "Застосунок") + (windows.count > 1 ? " · вікон: " + windows.count : "")
                // Windows in this task and which one is active; refreshes on any task change.
                readonly property var windows: { panel.desktop.taskRevision; return panel.desktop.taskState(index) }
                readonly property bool current: windows.active >= 0
                onClicked: panel.desktop.activateTask(index)
                background: Rectangle { radius: 11; color: task.current ? panel.colors.line : task.hovered || task.visualFocus ? panel.colors.hover : "transparent" }
                contentItem: Item {
                    Kirigami.Icon {
                        id: appIcon
                        anchors.centerIn: parent; width: task.width-16; height: width
                        source: task.model.decoration
                        layer.enabled: task.model.IsLauncher === true
                        layer.effect: MultiEffect { saturation: -1 }
                    }
                    // One dash per window (up to 4); the active window's dash is long.
                    // A single window keeps one dash: long when active, short otherwise.
                    Row {
                        anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 3
                        Repeater {
                            model: Math.min(4, task.windows.count || 0)
                            Rectangle {
                                required property int index
                                readonly property bool on: Math.min(task.windows.active, 3) === index
                                height: 3; radius: 1.5
                                width: on ? (task.windows.count > 1 ? 10 : 14) : 5
                                color: panel.colors.text
                                opacity: on || task.windows.count === 1 || !task.current ? 1 : 0.45
                                Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                            }
                        }
                    }
                }
                TapHandler { acceptedButtons: Qt.RightButton; onTapped: panel.desktop.taskMenu(task.index) }
            }
        }
    }
    Rectangle { y: panel.trayY - 8; width: parent.width; height: 1; color: panel.colors.line; opacity: panel.trayOpacity; visible: opacity > 0 }
    Row {
        y: panel.trayY; spacing: 4
        opacity: panel.trayOpacity; visible: opacity > 0; enabled: panel.trayOpacity === 1
        DesktopButton {
            colors: panel.colors; hint: "Мережа"; onClicked: panel.desktop.openSettings("kcm_networkmanagement")
            contentItem: Kirigami.Icon { source: "network-wireless"; color: panel.colors.text; isMask: true }
        }
        DesktopButton {
            id: volumeButton
            colors: panel.colors; hint: panel.volume.available ? "Гучність · " + panel.volume.volume + "%" : "Звук недоступний"
            enabled: panel.volume.available
            onClicked: volumePopup.opened ? volumePopup.close() : volumePopup.open()
            contentItem: Kirigami.Icon { source: panel.volume.muted ? "audio-volume-muted" : "audio-volume-high"; color: panel.colors.text; isMask: true }
        }
        DesktopButton {
            colors: panel.colors; hint: "Bluetooth"; visible: panel.bluetooth.available
            onClicked: panel.desktop.openSettings("kcm_bluetooth")
            contentItem: Kirigami.Icon { source: "preferences-system-bluetooth-symbolic"; color: panel.colors.text; isMask: true }
        }
        DesktopButton {
            colors: panel.colors; hint: panel.power.percent + "% · Живлення"; visible: panel.power.present
            onClicked: panel.desktop.openSettings("kcm_powerdevilprofilesconfig")
            contentItem: Kirigami.Icon { source: "battery"; color: panel.colors.text; isMask: true }
        }
    }
    ListView {
        id: tray
        objectName: "desktopTray"
        y: panel.trayY; anchors.right: parent.right;
        opacity: panel.trayOpacity; visible: opacity > 0; enabled: panel.trayOpacity === 1
        width: Math.max(40, parent.width - (panel.bluetooth.available ? 132 : 88) - (panel.power.present ? 44 : 0) - 16); height: 40
        orientation: ListView.Horizontal; spacing: 4; clip: true; boundsBehavior: Flickable.StopAtBounds
        model: panel.desktop.trayItems
        ScrollBar.horizontal: ScrollBar { policy: tray.contentWidth > tray.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
        delegate: DesktopButton {
            required property var modelData
            colors: panel.colors; hint: modelData.title || "Значок трея"; width: 40; height: 40
            onClicked: panel.desktop.trayAction(modelData.id,"Activate")
            contentItem: Item {
                Image { anchors.centerIn: parent; width: 22; height: 22; source: "image://desktop/" + encodeURIComponent(modelData.id) + "?" + modelData.revision + (panel.colors.light ? "&light" : "&dark"); sourceSize: Qt.size(22,22); fillMode: Image.PreserveAspectFit }
            }
            opacity: modelData.status === "Passive" ? 0.6 : 1
            TapHandler { acceptedButtons: Qt.RightButton; onTapped: panel.desktop.trayAction(parent.modelData.id,"ContextMenu") }
            TapHandler { acceptedButtons: Qt.MiddleButton; onTapped: panel.desktop.trayAction(parent.modelData.id,"SecondaryActivate") }
            WheelHandler { onWheel: event => { panel.desktop.trayAction(parent.modelData.id,"Scroll",event.angleDelta.y);event.accepted=true } }
        }
    }
    Popup {
        id: volumePopup
        parent: panel
        x: 0; y: 4; width: Math.min(300,panel.width); height: 76
        padding: 14
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { radius: 14; color: panel.colors.surface; border.color: panel.colors.line }
        Row {
            width: parent.width; spacing: 10
            DesktopButton { colors: panel.colors; symbol: panel.volume.muted ? "×" : "♪"; hint: "Вимкнути / увімкнути звук"; onClicked: panel.volume.toggleMute() }
            Slider { width: parent.width-100; from: 0; to: 100; value: panel.volume.volume; onMoved: panel.volume.setVolume(Math.round(value)) }
            Text { text: panel.volume.volume + "%"; color: panel.colors.text; anchors.verticalCenter: parent.verticalCenter }
        }
    }
}
