import QtQuick

Item {
    id: page
    required property QtObject colors
    required property QtObject model
    implicitHeight: dateLabel.y + dateLabel.implicitHeight

    Text {
        x: 4; y: 7
        text: page.model.clockWeekday
        color: page.colors.secondary
        font.pixelSize: 13
    }
    Row {
        x: 0; y: 35; spacing: 8
        Text {
            text: page.model.clockTime
            color: page.colors.text
            font.pixelSize: 70; font.weight: Font.Light
            font.letterSpacing: -3
        }
        Text {
            text: page.model.clockSeconds
            anchors.baseline: parent.children[0].baseline
            color: page.colors.muted
            font.pixelSize: 22; font.weight: Font.Light
        }
    }
    Text {
        id: dateLabel
        x: 4; y: 127
        text: page.model.clockDate
        color: page.colors.secondary
        font.pixelSize: 14
    }
    Canvas {
        id: dial
        anchors.right: parent.right; anchors.rightMargin: 12
        y: 19; width: 116; height: 116
        property string time: page.model.clockTime + page.model.clockSeconds
        property color stroke: page.colors.text
        onTimeChanged: requestPaint()
        onStrokeChanged: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.reset(); c.translate(58, 58)
            for (let i = 0; i < 60; ++i) {
                const angle = i * Math.PI / 30
                const outer = 52
                const inner = i % 5 === 0 ? 44 : 49
                c.strokeStyle = i % 5 === 0 ? page.colors.secondary : page.colors.line
                c.lineWidth = i % 5 === 0 ? 1.5 : 1
                c.beginPath(); c.moveTo(Math.sin(angle) * inner, -Math.cos(angle) * inner)
                c.lineTo(Math.sin(angle) * outer, -Math.cos(angle) * outer); c.stroke()
            }
            const parts = page.model.clockTime.split(":")
            const minute = Number(parts[1]); const second = Number(page.model.clockSeconds)
            function hand(angle, length, thickness, color) {
                c.strokeStyle = color; c.lineWidth = thickness; c.lineCap = "round"
                c.beginPath(); c.moveTo(0, 0); c.lineTo(Math.sin(angle) * length, -Math.cos(angle) * length); c.stroke()
            }
            hand((Number(parts[0]) % 12 + minute / 60) * Math.PI / 6, 25, 3, page.colors.text)
            hand((minute + second / 60) * Math.PI / 30, 38, 2, page.colors.text)
            hand(second * Math.PI / 30, 41, 1, page.colors.muted)
            c.fillStyle = page.colors.text; c.beginPath(); c.arc(0, 0, 3, 0, Math.PI * 2); c.fill()
        }
    }
}
