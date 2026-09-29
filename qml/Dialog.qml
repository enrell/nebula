import QtQuick

FocusScope {
    id: root
    property string title
    property int contentWidth: 560
    default property alias content: body.data
    readonly property int fs: Math.round(theme.fontSize * 1.33)

    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.86) }
    MouseArea { anchors.fill: parent; onClicked: app.hideOverlay() }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(root.width - 40, root.contentWidth)
        height: Math.min(root.height - 40, body.childrenRect.height + 76)
        color: theme.bg
        border.width: 1
        border.color: theme.accent

        MouseArea { anchors.fill: parent }   // swallow clicks

        Text {
            x: 20; y: 16
            text: root.title
            color: theme.accent; font.bold: true
            font.family: theme.fontFamily; font.pixelSize: root.fs + 1
        }
        Text {
            anchors { right: parent.right; rightMargin: 20; top: parent.top; topMargin: 18 }
            text: "esc  close"; color: theme.muted
            font.family: theme.fontFamily; font.pixelSize: root.fs - 2
        }
        Item {
            id: body
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20; topMargin: 52 }
        }
    }
}
