import QtQuick

Item {
    id: page
    required property IslandColors colors
    required property QtObject model
    readonly property bool timerView: model.timeView === "timer"
    readonly property Item currentPage: timerView ? timerPage : clockPage
    implicitHeight: switcher.height + 10 + currentPage.implicitHeight

    Row {
        id: switcher
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 3; height: 34 // fixed: a hidden Row reports 0 and would retarget the shell
        ActionButton { colors: page.colors; text: "Годинник"; implicitWidth: contentItem.implicitWidth + 22; selected: !page.timerView; onClicked: page.model.timeView = "clock" }
        ActionButton { colors: page.colors; text: "Таймер"; implicitWidth: contentItem.implicitWidth + 22; selected: page.timerView; onClicked: page.model.timeView = "timer" }
    }
    ClockPage { id: clockPage; x: 0; y: switcher.height + 10; width: parent.width; height: implicitHeight; colors: page.colors; model: page.model; visible: !page.timerView }
    TimerPage { id: timerPage; x: 0; y: switcher.height + 10; width: parent.width; height: implicitHeight; colors: page.colors; model: page.model; visible: page.timerView }
}
