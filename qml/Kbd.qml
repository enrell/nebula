import QtQuick

// Renders "Ctrl+Shift+D" as a row of key caps.
Row {
    id: root
    property string keys
    property int size: Math.round(theme.fontSize * 1.33) - 2
    spacing: 4
    Repeater {
        model: root.keys === "" ? [] : root.keys.split(/\+(?=.)/)
        delegate: Rectangle {
            required property string modelData
            height: root.size + 8
            width: Math.max(height, k.implicitWidth + 12)
            radius: 4
            color: theme.panel
            border.width: 1
            border.color: theme.border
            Text {
                id: k
                anchors.centerIn: parent
                text: modelData
                color: theme.fg
                font.family: theme.fontFamily; font.pixelSize: root.size
            }
        }
    }
}
