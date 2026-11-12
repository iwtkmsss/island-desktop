import QtQuick

Item {
    id: page
    required property QtObject colors
    required property QtObject model
    required property QtObject interaction
    required property QtObject audio
    property bool active: false
    implicitHeight: 132

    IslandEyes {
        objectName: "homePageEyes"
        anchors.centerIn: parent
        width: 48; height: 28; scale: 2
        visible: page.model.eyesEnabled
        eyeColor: page.colors.text; pupilColor: page.colors.surface
        running: visible && page.active
        reducedMotion: page.model.reducedMotion
        gazeX: page.interaction.gazeX; gazeY: page.interaction.gazeY
        activitySerial: page.interaction.pointerActivity
        audioActive: page.audio.enabled && page.audio.active
        audioLevel: page.audio.level; bands: page.audio.bands
    }
    Text {
        anchors.centerIn: parent
        visible: !page.model.eyesEnabled
        text: "Очі вимкнено в налаштуваннях"
        color: page.colors.secondary; font.pixelSize: 12
    }
}
