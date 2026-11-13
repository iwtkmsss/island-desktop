import QtQuick

Canvas {
    id: icon
    property string name: "clock"
    property color color: "white"
    implicitWidth: 20
    implicitHeight: 20
    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const c = getContext("2d")
        c.reset()
        const side = Math.min(width, height)
        c.translate((width - side) / 2, (height - side) / 2)
        c.scale(side / 24, side / 24)
        c.strokeStyle = color
        c.fillStyle = color
        c.lineWidth = 1.8
        c.lineCap = "round"
        c.lineJoin = "round"
        function line(x1, y1, x2, y2) {
            c.beginPath(); c.moveTo(x1, y1); c.lineTo(x2, y2); c.stroke()
        }
        function circle(x, y, r) {
            c.beginPath(); c.arc(x, y, r, 0, Math.PI * 2); c.stroke()
        }
        if (name === "home") {
            c.beginPath(); c.moveTo(3, 10.5); c.lineTo(12, 3.5); c.lineTo(21, 10.5); c.stroke()
            c.beginPath(); c.moveTo(5.5, 9); c.lineTo(5.5, 20); c.lineTo(18.5, 20); c.lineTo(18.5, 9); c.stroke()
            c.beginPath(); c.moveTo(10, 20); c.lineTo(10, 14); c.lineTo(14, 14); c.lineTo(14, 20); c.stroke()
        } else if (name === "clock") {
            circle(12, 12, 8.5); line(12, 7, 12, 12); line(12, 12, 15.5, 14)
        } else if (name === "timer") {
            circle(12, 13.5, 7.5); line(9, 3, 15, 3); line(12, 3, 12, 6)
            line(17.5, 8, 19.5, 6); line(12, 9.5, 12, 13.5)
        } else if (name === "music") {
            c.beginPath(); c.moveTo(9, 17.5); c.lineTo(9, 6); c.lineTo(19, 4); c.lineTo(19, 15.5); c.stroke()
            line(9, 9, 19, 7)
            c.beginPath(); c.ellipse(6.3, 18, 2.8, 2.2, -0.2, 0, Math.PI * 2); c.fill()
            c.beginPath(); c.ellipse(16.3, 16, 2.8, 2.2, -0.2, 0, Math.PI * 2); c.fill()
        } else if (name === "speaker" || name === "volume" || name === "speaker-off") {
            c.beginPath(); c.moveTo(3.5, 9); c.lineTo(7, 9); c.lineTo(12, 5); c.lineTo(12, 19); c.lineTo(7, 15); c.lineTo(3.5, 15); c.closePath(); c.stroke()
            if (name === "speaker-off") {
                line(3, 3, 21, 21)
            } else {
                c.beginPath(); c.arc(11.5, 12, 5.5, -0.65, 0.65); c.stroke()
                c.beginPath(); c.arc(11.5, 12, 9, -0.65, 0.65); c.stroke()
            }
        } else if (name === "settings") {
            c.beginPath()
            for (let i = 0; i < 8; ++i) {
                const angles = [-22.5, -14, -10, 10, 14, 22.5]
                for (let j = 0; j < angles.length; ++j) {
                    const a = (i * 45 + angles[j]) * Math.PI / 180
                    const r = j === 2 || j === 3 ? 8.5 : 7
                    const x = 12 + r * Math.cos(a), y = 12 + r * Math.sin(a)
                    if (i === 0 && j === 0) c.moveTo(x, y); else c.lineTo(x, y)
                }
            }
            c.closePath(); c.stroke(); circle(12, 12, 2.8)
        } else if (name === "up" || name === "down") {
            c.beginPath(); c.moveTo(6, name === "up" ? 15 : 9)
            c.lineTo(12, name === "up" ? 9 : 15); c.lineTo(18, name === "up" ? 15 : 9); c.stroke()
        } else if (name === "play") {
            c.beginPath(); c.moveTo(8, 5.5); c.lineTo(19.5, 12); c.lineTo(8, 18.5); c.closePath(); c.fill()
        } else if (name === "previous" || name === "next") {
            c.save()
            if (name === "previous") { c.translate(24, 0); c.scale(-1, 1) }
            c.beginPath(); c.moveTo(6, 6); c.lineTo(15, 12); c.lineTo(6, 18); c.closePath(); c.fill()
            c.fillRect(17, 6, 2, 12); c.restore()
        } else if (name === "pin") {
            c.save(); c.translate(11.3, 12.7); c.rotate(Math.PI / 4); c.translate(-12, -12)
            c.beginPath(); c.moveTo(8, 4); c.lineTo(16, 4); c.lineTo(15, 10); c.lineTo(18, 14); c.lineTo(6, 14); c.lineTo(9, 10); c.closePath(); c.stroke()
            line(12, 14, 12, 21); c.restore()
        } else if (name === "pause") {
            c.fillRect(7, 5.5, 3, 13); c.fillRect(14, 5.5, 3, 13)
        } else if (name === "reset") {
            c.beginPath(); c.arc(12, 12, 8, -Math.PI * 0.7, Math.PI * 1.2); c.stroke()
            c.beginPath(); c.moveTo(5.5, 3.5); c.lineTo(5.5, 7.5); c.lineTo(9.5, 7.5); c.stroke()
        } else if (name === "sun") {
            circle(12, 12, 4)
            for (let j = 0; j < 8; ++j) {
                const a = j * Math.PI / 4
                line(12 + 7 * Math.cos(a), 12 + 7 * Math.sin(a), 12 + 9 * Math.cos(a), 12 + 9 * Math.sin(a))
            }
        } else if (name === "moon") {
            c.beginPath(); c.moveTo(20.2, 13.4)
            c.bezierCurveTo(17.4, 14.9, 13.9, 14.4, 11.7, 12.1)
            c.bezierCurveTo(9.5, 9.9, 9, 6.5, 10.4, 3.8)
            c.bezierCurveTo(6.5, 4.5, 3.7, 8, 3.7, 12)
            c.bezierCurveTo(3.7, 16.6, 7.4, 20.3, 12, 20.3)
            c.bezierCurveTo(16, 20.3, 19.5, 17.4, 20.2, 13.4)
            c.closePath(); c.stroke()
        } else if (name === "check") {
            c.beginPath(); c.moveTo(5, 12); c.lineTo(10, 17); c.lineTo(19, 7); c.stroke()
        } else if (name === "minus") {
            line(6, 12, 18, 12)
        } else if (name === "plus") {
            line(6, 12, 18, 12); line(12, 6, 12, 18)
        } else if (name === "bluetooth") {
            c.beginPath(); c.moveTo(6.5, 7.5); c.lineTo(17, 16.5); c.lineTo(12, 21); c.lineTo(12, 3); c.lineTo(17, 7.5); c.lineTo(6.5, 16.5); c.stroke()
        } else if (name === "battery") {
            c.beginPath(); c.roundedRect(2.5, 7, 17, 10, 2.5, 2.5); c.stroke()
            line(22, 10.5, 22, 13.5)
        } else if (name === "plug") {
            line(9, 3, 9, 8); line(15, 3, 15, 8)
            c.beginPath(); c.moveTo(6.5, 8); c.lineTo(17.5, 8); c.lineTo(17.5, 11.5); c.bezierCurveTo(17.5, 14.5, 15, 16, 12, 16); c.bezierCurveTo(9, 16, 6.5, 14.5, 6.5, 11.5); c.closePath(); c.stroke()
            line(12, 16, 12, 21)
        } else if (name === "headphones") {
            c.beginPath(); c.arc(12, 13, 8, Math.PI, 0); c.stroke()
            c.beginPath(); c.roundedRect(3.5, 13, 4, 7, 1.8, 1.8); c.stroke()
            c.beginPath(); c.roundedRect(16.5, 13, 4, 7, 1.8, 1.8); c.stroke()
        } else if (name === "keyboard") {
            c.beginPath(); c.roundedRect(2.5, 6, 19, 12, 2.5, 2.5); c.stroke()
            line(6.5, 10, 6.6, 10); line(10, 10, 10.1, 10); line(13.5, 10, 13.6, 10); line(17, 10, 17.1, 10)
            line(8, 14, 16, 14)
        } else if (name === "mouse") {
            c.beginPath(); c.roundedRect(6.5, 3, 11, 18, 5.5, 5.5); c.stroke()
            line(12, 3.5, 12, 9)
        } else if (name === "phone") {
            c.beginPath(); c.roundedRect(6.5, 2.5, 11, 19, 2.5, 2.5); c.stroke()
            line(11, 18.5, 13, 18.5)
        } else if (name === "computer") {
            c.beginPath(); c.roundedRect(3, 4.5, 18, 12, 2, 2); c.stroke()
            line(12, 16.5, 12, 20); line(8, 20, 16, 20)
        } else if (name === "gamepad") {
            c.beginPath(); c.roundedRect(2.5, 7, 19, 11, 5, 5); c.stroke()
            line(7.5, 10.5, 7.5, 14.5); line(5.5, 12.5, 9.5, 12.5)
            line(15.5, 11.5, 15.6, 11.5); line(18, 13.5, 18.1, 13.5)
        } else if (name === "apps") {
            const ps = [5.5, 12, 18.5]
            for (let i = 0; i < 3; ++i) for (let j = 0; j < 3; ++j) {
                c.beginPath(); c.arc(ps[i], ps[j], 1.5, 0, Math.PI * 2); c.fill()
            }
        } else if (name === "wifi") {
            for (const r of [4.5, 8.5, 12.5]) {
                c.beginPath(); c.arc(12, 18.5, r, -Math.PI * 0.75, -Math.PI * 0.25); c.stroke()
            }
            c.beginPath(); c.arc(12, 18.5, 1.4, 0, Math.PI * 2); c.fill()
        } else if (name === "device") {
            c.beginPath(); c.roundedRect(4, 4, 16, 16, 4, 4); c.stroke()
            c.beginPath(); c.arc(12, 12, 1.6, 0, Math.PI * 2); c.fill()
        }
    }
}
