import QtQuick

Rectangle {
    id: root
    property bool checked: false
    signal toggled(bool value)
    width: 36
    height: 20
    radius: 10
    opacity: enabled ? 1 : 0.4
    color: checked ? theme.accent : theme.panel
    border.width: 1
    border.color: checked ? theme.accent : theme.border
    Behavior on color { ColorAnimation { duration: 90 } }

    Rectangle {
        y: 3
        x: root.checked ? root.width - width - 3 : 3
        width: 14; height: 14; radius: 7
        color: root.checked ? theme.bg : theme.muted
        Behavior on x { NumberAnimation { duration: 90 } }
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.checked)
    }
}
