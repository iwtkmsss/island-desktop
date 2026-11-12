import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    required property QtObject colors
    required property QtObject model
    readonly property bool running: model.timerState === "running"
    implicitHeight: 182

    Row {
        x: 0; y: 0; spacing: 8
        Repeater {
            model: [5, 15, 25]
            ActionButton {
                required property int modelData
                colors: page.colors
                text: modelData + " хв"
                selected: page.model.timerMinutes === modelData
                enabled: !page.running
                implicitHeight: 29
                onClicked: page.model.setTimerMinutes(modelData)
            }
        }
    }
    Row {
        anchors.right: parent.right; y: 0; spacing: 4
        ActionButton {
            colors: page.colors; iconName: "minus"; hint: "На хвилину менше"
            implicitWidth: 29; implicitHeight: 29
            enabled: !page.running && page.model.timerMinutes > 1
            onClicked: page.model.setTimerMinutes(page.model.timerMinutes - 1)
        }
        Text { width: 52; height: 29; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter; text: page.model.timerMinutes + " хв"; color: page.colors.secondary; font.pixelSize: 12 }
        ActionButton {
            colors: page.colors; iconName: "plus"; hint: "На хвилину більше"
            implicitWidth: 29; implicitHeight: 29
            enabled: !page.running && page.model.timerMinutes < 180
            onClicked: page.model.setTimerMinutes(page.model.timerMinutes + 1)
        }
    }
    Text {
        x: 0; y: 39
        text: page.model.timerText
        color: page.colors.text
        font.pixelSize: 66; font.weight: Font.Light; font.letterSpacing: -2
    }
    Text {
        x: 4; y: 123
        text: page.running ? "Відлік триває" : page.model.timerState === "paused" ? "На паузі"
            : page.model.timerState === "finished" ? "Час вийшов" : ""
        color: page.colors.secondary; font.pixelSize: 12
    }
    Canvas {
        id: ring
        anchors.right: parent.right; anchors.rightMargin: 12
        y: 49; width: 86; height: 86
        property real progress: page.model.timerProgress
        property color stroke: page.colors.text
        onProgressChanged: requestPaint()
        onStrokeChanged: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.reset(); c.lineWidth = 3; c.lineCap = "round"
            c.strokeStyle = page.colors.line
            c.beginPath(); c.arc(43, 43, 38, 0, Math.PI * 2); c.stroke()
            c.strokeStyle = page.colors.text
            c.beginPath(); c.arc(43, 43, 38, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * (1 - progress)); c.stroke()
        }
        LineIcon { anchors.centerIn: parent; name: "timer"; color: page.colors.secondary; width: 24; height: 24 }
    }
    RowLayout {
        y: parent.height - 34; width: parent.width; height: 34; spacing: 8
        Item { Layout.fillWidth: true }
        ActionButton { colors: page.colors; iconName: "reset"; hint: "Скинути таймер"; onClicked: page.model.resetTimer() }
        ActionButton {
            colors: page.colors; primary: true
            iconName: page.running ? "pause" : "play"
            text: page.running ? "Пауза" : page.model.timerState === "paused" ? "Далі" : "Почати"
            onClicked: page.model.toggleTimer()
        }
    }
}
