import QtQuick

Rectangle {
    id: root
    property string label
    property bool primary: false
    property bool danger: false
    signal clicked
    readonly property color base: danger ? theme.red : theme.accent

    implicitWidth: t.implicitWidth + 24
    height: 28
    radius: 5
    color: primary ? (ma.containsMouse ? Qt.lighter(base, 1.15) : base) : (ma.containsMouse ? theme.panel : "transparent")
    border.width: 1
    border.color: primary ? base : (danger ? theme.red : theme.border)
    opacity: enabled ? 1 : 0.4

    Text {
        id: t
        anchors.centerIn: parent
        text: root.label
        color: root.primary ? theme.bg : (root.danger ? theme.red : theme.fg)
        font.family: theme.fontFamily
        font.pixelSize: Math.round(theme.fontSize * 1.33) - 1
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        enabled: root.enabled
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
