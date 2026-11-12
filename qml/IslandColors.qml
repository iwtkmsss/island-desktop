import QtQuick

QtObject {
    id: islandColors
    property bool light: false
    readonly property color surface: light ? "#ffffff" : "#070707"
    readonly property color raised: light ? "#f1f1f1" : "#1b1b1b"
    readonly property color hover: light ? "#e6e6e6" : "#292929"
    readonly property color text: light ? "#141414" : "#f5f5f5"
    readonly property color secondary: light ? "#626262" : "#a1a1a1"
    readonly property color muted: light ? "#777777" : "#858585"
    readonly property color line: light ? "#dedede" : "#2a2a2a"
    readonly property color inverse: light ? "#ffffff" : "#111111"
}
