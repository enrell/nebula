import QtQuick

// Agent views shown over the workspace (the default placement). The top of the modal stack is shown; hiding
// keeps every view (Esc, Ctrl+Shift+O, a click outside, the status bar chip brings them back), the header docks
// the view next to the agent that made it, and × closes it for good.
Item {
    id: root
    visible: app.modalVisible
    readonly property var pane: app.modalView
    readonly property int fs: Math.round(theme.fontSize * 1.33)

    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.72) }
    MouseArea { anchors.fill: parent; onClicked: app.setModalHidden(true) }

    Rectangle {
        id: box
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, Math.max(760, parent.width * 0.86))
        height: parent.height - 32
        color: theme.bg
        radius: 6
        border.width: 1
        border.color: theme.accent

        MouseArea { anchors.fill: parent }   // clicks inside never close

        Item {
            id: header
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: root.fs + 22

            Row {
                anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                spacing: 12
                Text {
                    text: root.pane ? root.pane.title : ""
                    color: theme.accent; font.bold: true
                    font.family: theme.fontFamily; font.pixelSize: root.fs + 1
                }
                Text {
                    visible: root.pane && root.pane.errorCount > 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.pane ? root.pane.errorCount + (root.pane.errorCount === 1 ? " error" : " errors") : ""
                    color: theme.red; font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                }
                Text {
                    visible: app.modalCount > 1
                    anchors.verticalCenter: parent.verticalCenter
                    text: app.modalCount - 1 + " more below"
                    color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                }
            }
            Row {
                anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                spacing: 4
                component HeaderButton: Rectangle {
                    property string icon
                    property string label
                    signal clicked
                    width: row.implicitWidth + 14; height: root.fs + 8; radius: 4
                    color: hover.hovered ? theme.panel : "transparent"
                    Row {
                        id: row
                        anchors.centerIn: parent
                        spacing: 6
                        Icon { name: parent.parent.icon; size: 12; color: hover.hovered ? theme.fg : theme.muted; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: parent.parent.label; color: hover.hovered ? theme.fg : theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2 }
                    }
                    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: parent.clicked() }
                }
                HeaderButton { icon: "split-right"; label: "dock right"; onClicked: app.dockModal("right") }
                HeaderButton { icon: "split-down"; label: "dock below"; onClicked: app.dockModal("down") }
                HeaderButton { icon: "hide"; label: "hide  esc"; onClicked: app.setModalHidden(true) }
                HeaderButton { icon: "close"; label: "close"; onClicked: app.closeModal() }
            }
            Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: theme.border }
        }

        // one page per view object: a different view is a different page (and QWebChannel registration)
        Repeater {
            model: root.pane ? [root.pane] : []
            delegate: ViewContent {
                required property var modelData
                x: 1; y: header.height
                width: box.width - 2; height: box.height - header.height - 6
                pane: modelData
                paneFocused: root.visible
            }
        }
    }
}
