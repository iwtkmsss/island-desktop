import QtQuick
// Windows: tray/app images come from the image provider; theme names map to LineIcon glyphs.
Item {
    id: icon
    property var source: ""
    property color color: "white"
    property bool isMask: true
    implicitWidth: 22
    implicitHeight: 22
    readonly property string sourceText: typeof source === "string" ? source : (source === undefined || source === null ? "" : source.toString())
    readonly property bool isImage: sourceText.indexOf("image:") === 0
    readonly property var glyphs: ({
        "application-menu": "apps", "network-wireless": "wifi",
        "audio-volume-muted": "speaker-off", "audio-volume-high": "speaker",
        "preferences-system-bluetooth-symbolic": "bluetooth", "battery": "battery"
    })
    readonly property string glyph: isImage ? "" : (glyphs[sourceText] || "")
    Image {
        anchors.centerIn: parent
        width: parent.width * 0.8
        height: parent.height * 0.8
        visible: icon.isImage
        source: icon.isImage ? icon.sourceText : ""
        fillMode: Image.PreserveAspectFit
        sourceSize: Qt.size(64, 64)
        smooth: true; mipmap: true; asynchronous: false
    }
    LineIcon { anchors.fill: parent; visible: icon.glyph !== ""; name: icon.glyph; color: icon.color }
}
