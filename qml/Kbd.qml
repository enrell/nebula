import QtQuick

// Renders "Ctrl+Shift+D" as a row of key caps. `keys` is one combination or a list of alternatives, drawn apart
// ("Ctrl++" is Ctrl and +).
Row {
    id: root
    property var keys
    property int size: Math.round(theme.fontSize * 1.33) - 2
    readonly property var combos: !keys ? [] : typeof keys === "string" ? [keys] : keys
    spacing: 12
    Repeater {
        model: root.combos
        delegate: Row {
            required property string modelData
            spacing: 4
            Repeater {
                model: modelData === "" ? [] : modelData.split(/\+(?=.)/)
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
    }
}
