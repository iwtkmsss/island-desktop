import QtQuick
import QtQuick.Layouts

Item {
    id: page
    required property QtObject colors
    required property QtObject volume
    required property QtObject audio
    implicitHeight: volume.available ? 160 : emptyState.implicitHeight
    required property QtObject model
    Item {
        anchors.fill: parent
        visible: page.volume.available
        RowLayout {
            y: 0; width: parent.width; spacing: 8
            LineIcon { name: "speaker"; color: page.colors.secondary; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
            Text { text: page.volume.deviceName || "Аудіовихід Windows"; textFormat: Text.PlainText; color: page.colors.secondary; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
        }
        Text {
            x: 0; y: 29; text: page.volume.volume + "%"
            color: page.colors.text; opacity: page.volume.muted ? 0.45 : 1
            font.pixelSize: 62; font.weight: Font.Light; font.letterSpacing: -2
        }
        AudioMeter {
            anchors.right: parent.right; anchors.rightMargin: 12; y: 49
            width: 112; height: 42
            audio: page.audio; colors: page.colors; reducedMotion: page.model.reducedMotion
        }
        Text {
            x: 3; y: 110
            text: page.volume.error || (page.volume.muted ? "Звук вимкнено" : page.model.reducedMotion ? "Візуалізацію вимкнено"
                : page.audio.error || (page.audio.active ? "Звук комп’ютера" : "Зараз тихо"))
            width: parent.width - 6; elide: Text.ElideRight; textFormat: Text.PlainText
            color: page.colors.secondary; font.pixelSize: 12
        }
        SoundSlider { x: 0; y: 129; width: parent.width - 48; height: 28; volume: page.volume; colors: page.colors }
        ActionButton {
            anchors.right: parent.right; y: 126
            colors: page.colors
            iconName: page.volume.muted ? "speaker-off" : "speaker"
            hint: page.volume.muted ? "Увімкнути звук Windows" : "Вимкнути звук Windows"
            selected: page.volume.muted
            onClicked: page.volume.toggleMute()
        }
    }
    Column {
        id: emptyState
        anchors.centerIn: parent; width: parent.width - 30; spacing: 13
        visible: !page.volume.available
        LineIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "speaker-off"; width: 34; height: 34; color: page.colors.secondary }
        Text { width: parent.width; text: "Немає аудіовиходу"; horizontalAlignment: Text.AlignHCenter; color: page.colors.text; font.pixelSize: 22; font.weight: Font.DemiBold }
        Text { width: parent.width; text: page.volume.error || "Підключіть навушники або динаміки."; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; color: page.colors.secondary; font.pixelSize: 12 }
    }
}
