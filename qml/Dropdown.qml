import QtQuick

// A button that opens a list of choices. items: [{label, hint, value, enabled}]. The list is drawn inside `scope`
// (usually the modal box) so it can overflow its parent and closes when the user clicks anywhere else.
Item {
    id: root
    property var items: []
    property string text                 // what the button shows
    property string placeholder: "Select"
    property string prefix               // small muted label before the text
    property bool flat: false            // text-like: no frame until hovered (for headers)
    property Item scope
    property string currentValue
    property bool open: false
    signal picked(var value)
    readonly property int fs: Math.round(theme.fontSize * 1.33)
    property real px: 0
    property real py: 0

    implicitWidth: label.implicitWidth + (flat ? 30 : 44) + (prefix !== "" ? prefixText.implicitWidth + 6 : 0)
    implicitHeight: flat ? fs + 8 : 30
    enabled: items.length > 0
    opacity: enabled ? 1 : 0.5

    onOpenChanged: if (open && scope) {
        const p = root.mapToItem(scope, 0, root.height + 4)
        px = Math.max(8, Math.min(p.x, scope.width - pop.width - 8))
        py = p.y
    }

    Rectangle {
        anchors.fill: parent
        radius: root.flat ? 4 : 6
        color: ma.containsMouse || root.open ? theme.panel : "transparent"
        border.width: root.flat && !root.open ? 0 : 1
        border.color: root.open ? theme.accent : theme.border
        Row {
            anchors.verticalCenter: parent.verticalCenter
            x: root.flat ? 7 : 10
            spacing: 6
            Text {
                id: prefixText
                visible: root.prefix !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: root.prefix; color: theme.muted
                font.family: theme.fontFamily; font.pixelSize: root.fs - 2
            }
            Text {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, root.width - (root.flat ? 30 : 44) - (root.prefix !== "" ? prefixText.implicitWidth + 6 : 0))
                elide: Text.ElideRight
                text: root.text !== "" ? root.text : root.placeholder
                color: root.text !== "" && !root.flat ? theme.fg : root.flat && (ma.containsMouse || root.open) ? theme.fg : theme.muted
                font.family: theme.fontFamily; font.pixelSize: root.flat ? root.fs - 2 : root.fs - 1
            }
        }
        Text {
            anchors { right: parent.right; rightMargin: root.flat ? 7 : 10; verticalCenter: parent.verticalCenter }
            text: root.open ? "▴" : "▾"; color: theme.muted; font.pixelSize: root.fs - 2
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.open = !root.open
        }
    }

    Item {
        parent: root.scope
        anchors.fill: parent
        visible: root.open
        z: 100
        MouseArea { anchors.fill: parent; onClicked: root.open = false }
        Rectangle {
            id: pop
            x: root.px; y: root.py
            width: Math.max(root.width, 280)
            height: Math.min(list.contentHeight + 8, 300)
            radius: 6
            color: theme.bg
            border.width: 1
            border.color: theme.border
            MouseArea { anchors.fill: parent }
            ListView {
                id: list
                anchors { fill: parent; margins: 4 }
                clip: true
                model: root.items
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool cur: modelData.value === root.currentValue
                    readonly property bool ok: modelData.enabled !== false
                    width: list.width
                    height: (modelData.hint ? root.fs * 2 + 10 : root.fs + 14)
                    radius: 5
                    color: ima.containsMouse && ok ? theme.panel : "transparent"
                    opacity: ok ? 1 : 0.5
                    Column {
                        x: 10; anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 34
                        spacing: 1
                        Text {
                            width: parent.width; elide: Text.ElideRight
                            text: modelData.label; color: cur ? theme.accent : theme.fg; font.bold: cur
                            font.family: theme.fontFamily; font.pixelSize: root.fs - 1
                        }
                        Text {
                            visible: !!modelData.hint
                            width: parent.width; elide: Text.ElideRight
                            text: modelData.hint || ""; color: theme.muted
                            font.family: theme.fontFamily; font.pixelSize: root.fs - 3
                        }
                    }
                    Text { visible: cur; anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter } text: "✓"; color: theme.accent; font.pixelSize: root.fs }
                    MouseArea {
                        id: ima
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: ok ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: if (ok) { root.open = false; root.picked(modelData.value) }
                    }
                }
            }
        }
    }
}
