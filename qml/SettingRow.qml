import QtQuick

Item {
    id: root
    property string label
    property string description
    default property alias content: slot.data
    readonly property int fs: Math.round(theme.fontSize * 1.33)

    width: parent ? parent.width : 0
    height: Math.max(text.implicitHeight, slot.childrenRect.height) + 18

    Column {
        id: text
        anchors { left: parent.left; right: slot.left; rightMargin: 24; verticalCenter: parent.verticalCenter }
        spacing: 3
        Text { text: root.label; color: theme.fg; font.family: theme.fontFamily; font.pixelSize: root.fs }
        Text {
            visible: root.description !== ""
            width: parent.width
            text: root.description; color: theme.muted; wrapMode: Text.WordWrap
            font.family: theme.fontFamily; font.pixelSize: root.fs - 2
        }
    }
    Item {
        id: slot
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        width: childrenRect.width
        height: childrenRect.height
    }
    Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: theme.border; opacity: 0.5 }
}
