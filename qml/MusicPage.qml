import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Item {
    id: page
    required property QtObject colors
    required property QtObject media
    required property QtObject model
    required property QtObject audio
    required property QtObject volume
    readonly property bool soundOff: !volume.available || volume.muted || volume.volume === 0
    implicitHeight: media.available ? (playbackError.visible ? playbackError.y + playbackError.implicitHeight
                                                           : timeLabels.y + timeLabels.implicitHeight) : emptyState.implicitHeight
    function timeLabel(milliseconds) {
        const seconds = Math.floor(milliseconds / 1000)
        return Math.floor(seconds / 60) + ":" + (seconds % 60).toString().padStart(2, "0")
    }
    Item {
        anchors.fill: parent; visible: page.media.available
        Rectangle {
            id: coverCard
            x: 0; y: 4; width: 128; height: 128; radius: 18
            color: page.colors.raised; border.color: page.colors.line
            Image {
                id: artwork
                anchors.fill: parent; anchors.margins: 6
                source: page.media.artworkUrl; sourceSize: Qt.size(256, 256)
                fillMode: Image.PreserveAspectCrop
                smooth: true; mipmap: true
                visible: source.toString().length > 0
                layer.enabled: visible
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: artworkMask
                    // Preserve the rounded mask's fractional edge coverage.
                    maskThresholdMin: 0.5
                    maskSpreadAtMin: 1.0
                }
            }
            Rectangle {
                id: artworkMask
                anchors.fill: artwork
                radius: 12; color: "white"; antialiasing: true
                layer.smooth: true
                layer.enabled: true; visible: false
            }
            LineIcon { anchors.centerIn: parent; width: 40; height: 40; name: "music"; color: page.colors.muted; visible: page.media.artworkUrl.length === 0 }
        }
        Item {
            id: details
            x: 148; y: 0; width: parent.width - 148; height: 128
            Text {
                y: 0; width: parent.width; text: page.media.title || "Без назви"
                font.pixelSize: 23; font.weight: Font.DemiBold; font.letterSpacing: -0.5
                textFormat: Text.PlainText
                color: page.colors.text; elide: Text.ElideRight
            }
            Text { y: 33; width: parent.width; text: page.media.artist || page.media.sourceName; textFormat: Text.PlainText; color: page.colors.secondary; font.pixelSize: 13; elide: Text.ElideRight }
            Row {
                id: transport
                y: coverCard.y + coverCard.height - details.y - height
                height: 40; spacing: 12
                ActionButton { anchors.verticalCenter: parent.verticalCenter; colors: page.colors; iconName: "previous"; hint: "Попередній трек"; enabled: page.media.canPrevious && !page.media.busy; onClicked: page.media.previous() }
                ActionButton {
                    colors: page.colors; primary: true; implicitWidth: 40; implicitHeight: 40
                    iconName: page.media.playing ? "pause" : "play"
                    hint: page.media.playing ? "Призупинити" : "Відтворити"
                    enabled: page.media.canToggle && !page.media.busy
                    onClicked: page.media.togglePlayback()
                }
                ActionButton { anchors.verticalCenter: parent.verticalCenter; colors: page.colors; iconName: "next"; hint: "Наступний трек"; enabled: page.media.canNext && !page.media.busy; onClicked: page.media.next() }
            }
        }
        Slider {
            id: scrubber
            x: 0; y: 144; width: parent.width; height: 18
            from: 0; to: 1; padding: 0
            enabled: page.media.canSeek && page.media.durationMs > 0 && !page.media.busy
            Accessible.name: "Позиція відтворення"
            Binding { target: scrubber; property: "value"; value: page.media.progress; when: !scrubber.pressed; restoreMode: Binding.RestoreNone }
            onPressedChanged: if (!pressed) page.media.seek(value)
            onMoved: if (!pressed) page.media.seek(value)
            background: Rectangle {
                y: (scrubber.height - height) / 2; width: scrubber.width; height: scrubber.visualFocus ? 4 : 3; radius: 2; color: page.colors.line
                Rectangle { width: parent.width * scrubber.visualPosition; height: parent.height; radius: 2; color: page.colors.text }
            }
            handle: Rectangle { x: scrubber.visualPosition * (scrubber.width - width); y: 5; width: 8; height: 8; radius: 4; color: page.colors.text; visible: scrubber.enabled && (scrubber.hovered || scrubber.pressed || scrubber.visualFocus) }
        }
        RowLayout {
            id: timeLabels
            y: 164; width: parent.width
            Text { text: page.timeLabel(page.media.positionMs); color: page.colors.muted; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Text { text: page.media.durationMs > 0 ? page.timeLabel(page.media.durationMs) : "—"; color: page.colors.muted; font.pixelSize: 11 }
        }
        RowLayout {
            anchors.right: parent.right
            y: details.y + transport.y + (transport.height - height) / 2
            width: 126; height: 24; spacing: 8
            ActionButton {
                colors: page.colors; implicitWidth: 24; implicitHeight: 24
                iconName: page.soundOff ? "speaker-off" : "speaker"
                hint: !page.volume.available ? "Немає аудіовиходу Windows"
                    : page.volume.muted ? "Увімкнути звук Windows"
                    : page.volume.volume === 0 ? "Гучність Windows: 0%" : "Вимкнути звук Windows"
                enabled: page.volume.available
                onClicked: page.volume.toggleMute()
            }
            SoundSlider { colors: page.colors; volume: page.volume; Layout.preferredWidth: 94; Layout.preferredHeight: 22 }
        }
        Text {
            id: playbackError
            y: timeLabels.y + timeLabels.implicitHeight + 8; width: parent.width
            visible: page.media.error.length > 0
            text: page.media.error; textFormat: Text.PlainText; color: page.colors.muted
            font.pixelSize: 11; elide: Text.ElideRight
        }
    }
    Column {
        id: emptyState
        anchors.centerIn: parent; width: parent.width - 30; spacing: 12
        visible: !page.media.available
        LineIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "music"; color: page.colors.secondary; width: 34; height: 34 }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: page.media.busy ? "Підключення плеєра…" : "Увімкніть музику"; font.pixelSize: 22; font.weight: Font.DemiBold; color: page.colors.text }
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: page.media.busy ? "Оновлюємо дані вибраного джерела." : page.media.error || "Запустіть відтворення у Spotify, браузері або іншому сумісному плеєрі."
            color: page.colors.secondary; font.pixelSize: 12; wrapMode: Text.WordWrap
        }
        ActionButton {
            anchors.horizontalCenter: parent.horizontalCenter
            colors: page.colors; text: "Обрати активний плеєр"
            visible: page.media.selectedSource.length > 0
            selected: true
            onClicked: page.media.selectSource("")
        }
    }
}
