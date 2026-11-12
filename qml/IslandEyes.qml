pragma ComponentBehavior: Bound

import QtQuick

// Decorative only: the parent retains compact context, actions and hit testing.
Item {
    id: eyes

    implicitWidth: 48
    implicitHeight: 28
    Accessible.ignored: true

    property color eyeColor: "white"
    property color pupilColor: "black"
    property bool running: false
    property bool reducedMotion: false
    property bool attentive: false
    property real gazeX: 0
    property real gazeY: 0
    property bool audioActive: false
    property real audioLevel: 0
    property var bands: []
    property int idleTimeout: 8000
    property bool sleeping: false

    readonly property bool motionAllowed: running && visible && !reducedMotion
    readonly property real localLevel: audioActive ? unit(audioLevel) : 0
    readonly property real bass: {
        if (!audioActive || !bands || bands.length === 0)
            return 0
        const count = Math.min(3, bands.length)
        let sum = 0
        for (let i = 0; i < count; ++i)
            sum += unit(bands[i])
        return sum / count
    }
    readonly property bool audible: audioActive && (localLevel > 0.008 || bass > 0.008)

    // One bounded update loop, no autonomous music oscillation or animation queue.
    property real _lookX: 0
    property real _lookY: 0
    property real _opening: 1
    property real _energy: 0
    property double _quietSince: 0
    property double _nextBlink: 0
    property double _blinkStarted: 0
    property double _lastTick: 0

    function unit(value: real): real {
        return isFinite(value) ? Math.max(0, Math.min(1, value)) : 0
    }

    function gaze(value: real): real {
        return isFinite(value) ? Math.max(-1, Math.min(1, value)) : 0
    }

    function resetMotion() {
        const now = Date.now()
        _lookX = 0
        _lookY = 0
        _opening = 1
        _energy = 0
        _quietSince = now
        _nextBlink = now + 3800 + Math.random() * 3200
        _blinkStarted = 0
        _lastTick = now
        sleeping = false
    }

    function step() {
        const now = Date.now()
        const dt = Math.max(1, Math.min(100, now - _lastTick))
        _lastTick = now
        if (attentive || audible)
            _quietSince = now
        sleeping = now - _quietSince >= idleTimeout

        let openingTarget = sleeping ? 0.12 : 1
        if (sleeping) {
            _blinkStarted = 0
            _nextBlink = now + 3800
        } else if (_blinkStarted > 0) {
            const elapsed = now - _blinkStarted
            if (elapsed < 85)
                openingTarget = 0.10
            else if (elapsed >= 210)
                _blinkStarted = 0
        } else if (now >= _nextBlink) {
            _blinkStarted = now
            _nextBlink = now + 3800 + Math.random() * 3200
            openingTarget = 0.10
        }

        const follow = 1 - Math.exp(-dt / 95)
        const eyelid = 1 - Math.exp(-dt / (sleeping ? 240 : 38))
        const energyTarget = audible ? unit(localLevel * 0.8 + bass * 0.2) : 0
        const music = 1 - Math.exp(-dt / (energyTarget > _energy ? 65 : 170))
        _lookX += ((sleeping ? 0 : gaze(gazeX)) - _lookX) * follow
        _lookY += ((sleeping ? 0 : gaze(gazeY)) - _lookY) * follow
        _opening += (openingTarget - _opening) * eyelid
        _energy += (energyTarget - _energy) * music
    }

    onMotionAllowedChanged: resetMotion()
    onAttentiveChanged: if (attentive && motionAllowed) { _quietSince = Date.now(); sleeping = false; step() }
    onAudibleChanged: if (audible && motionAllowed) { _quietSince = Date.now(); sleeping = false; step() }
    Component.onCompleted: resetMotion()

    Timer {
        interval: eyes.sleeping ? 250 : 33
        repeat: true
        running: eyes.motionAllowed
        onTriggered: eyes.step()
    }

    Repeater {
        model: 2
        delegate: Rectangle {
            id: eye
            required property int index

            // Small vertical pills like the reference; audio changes at most 1.5 px.
            readonly property real opening: eyes.motionAllowed ? eyes._opening : 1
            readonly property real energy: eyes.motionAllowed ? eyes._energy : 0
            width: 8
            height: Math.max(1.6, (13 + 1.5 * energy) * opening)
            x: eyes.width / 2 + (index === 0 ? -13 : 5)
            y: (eyes.height - height) / 2 - energy * 0.5
            radius: Math.min(width, height) / 2
            color: eyes.eyeColor
            antialiasing: true
            clip: true
            Accessible.ignored: true

            Rectangle {
                id: pupil
                // A circle kept inside the rounded capsule analytically, including
                // during blinks. No offscreen mask, shader or rectangular edge leak.
                readonly property real gx: eyes.motionAllowed ? eyes._lookX : 0
                readonly property real gy: eyes.motionAllowed ? eyes._lookY : 0
                readonly property real length: Math.max(1, Math.sqrt(gx * gx + gy * gy))
                readonly property real travel: Math.max(0, eye.radius - width / 2 - 0.6)
                width: Math.min(2.5, Math.max(0, eye.height - 1.4))
                height: width
                radius: width / 2
                x: (eye.width - width) / 2
                   + gx / length * (travel + Math.max(0, (eye.width - eye.height) / 2))
                y: (eye.height - height) / 2
                   + gy / length * (travel + Math.max(0, (eye.height - eye.width) / 2))
                color: eyes.pupilColor
                opacity: 0.8 * Math.max(0, Math.min(1, (eye.opening - 0.25) / 0.5))
                antialiasing: true
                Accessible.ignored: true
            }
        }
    }
}
