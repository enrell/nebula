import QtQuick

Item {
    id: root
    property var items: []
    property real px: 0
    property real py: 0
    visible: false
    readonly property int fs: Math.round(theme.fontSize * 1.33)

    function open(x, y, list) {
        items = list
        px = x
        py = y
        visible = true
    }
    function close() { visible = false }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: root.close()
    }

    Rectangle {
        x: Math.min(root.px, root.width - width - 4)
        y: Math.min(root.py, root.height - height - 4)
        width: 220
        height: col.height + 8
        color: theme.bg
        border.width: 1
        border.color: theme.accent

        Column {
            id: col
            y: 4
            width: parent.width
            Repeater {
                model: root.items
                delegate: Item {
                    required property var modelData
                    width: col.width
                    height: modelData.separator ? 9 : root.fs + 12
                    Rectangle {
                        visible: modelData.separator === true
                        anchors { left: parent.left; right: parent.right; margins: 8; verticalCenter: parent.verticalCenter }
                        height: 1; color: theme.border
                    }
                    Rectangle {
                        visible: !modelData.separator
                        anchors.fill: parent
                        anchors.margins: 2
                        color: ma.containsMouse && modelData.enabled !== false ? theme.panel : "transparent"
                    }
                    Text {
                        visible: !modelData.separator
                        x: 12; anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label || ""
                        color: modelData.enabled === false ? theme.muted : theme.fg
                        font.family: theme.fontFamily; font.pixelSize: root.fs - 1
                    }
                    Text {
                        visible: !modelData.separator
                        anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                        text: modelData.hint || ""
                        color: theme.muted
                        font.family: theme.fontFamily; font.pixelSize: root.fs - 3
                    }
                    MouseArea {
                        id: ma
                        anchors.fill: parent
                        enabled: !modelData.separator && modelData.enabled !== false
                        hoverEnabled: true
                        onClicked: { root.close(); modelData.run() }
                    }
                }
            }
        }
    }
}
