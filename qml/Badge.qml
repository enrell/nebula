import QtQuick

Rectangle {
    id: root
    property string text
    property color tone: theme.muted
    readonly property int fs: Math.round(theme.fontSize * 1.33)
    implicitWidth: t.implicitWidth + 14
    implicitHeight: root.fs + 6
    radius: height / 2
    color: Qt.rgba(tone.r, tone.g, tone.b, 0.16)
    border.width: 1
    border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.45)
    Text {
        id: t
        anchors.centerIn: parent
        text: root.text
        color: root.tone
        font.family: theme.fontFamily; font.pixelSize: root.fs - 2; font.bold: true
    }
}
