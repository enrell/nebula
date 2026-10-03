import QtQuick

Rectangle {
    id: root
    property string label
    property string icon                 // optional Icon name shown before the label
    property bool primary: false
    property bool danger: false
    signal clicked
    readonly property color base: danger ? theme.red : theme.accent

    implicitWidth: content.implicitWidth + 24
    height: 28
    radius: 5
    color: primary ? (ma.containsMouse ? Qt.lighter(base, 1.15) : base) : (ma.containsMouse ? theme.panel : "transparent")
    border.width: 1
    border.color: primary ? base : (danger ? theme.red : theme.border)
    opacity: enabled ? 1 : 0.4

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 7
        Icon {
            visible: root.icon !== ""
            anchors.verticalCenter: parent.verticalCenter
            name: root.icon
            size: t.font.pixelSize
            color: t.color
        }
        Text {
            id: t
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: root.primary ? theme.bg : (root.danger ? theme.red : theme.fg)
            font.family: theme.fontFamily
            font.pixelSize: Math.round(theme.fontSize * 1.33) - 1
        }
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
