import QtQuick

Item {
    id: root
    property string icon
    property int size: 14
    property color color: theme.muted
    property color hoverColor: theme.fg
    property alias hovered: ma.containsMouse
    signal clicked

    width: size + 10
    height: size + 10

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: theme.panel
        opacity: ma.containsMouse ? 1 : 0
    }
    Icon {
        anchors.centerIn: parent
        name: root.icon
        size: root.size
        color: ma.containsMouse ? root.hoverColor : root.color
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
