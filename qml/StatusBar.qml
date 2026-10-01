import QtQuick

Rectangle {
    id: root
    color: Qt.darker(theme.bg, 1.15)
    readonly property int fs: Math.round(theme.fontSize * 1.33)
    readonly property int working: win.countState("working")
    readonly property int blocked: win.countState("blocked")
    readonly property int done: win.countState("done")

    Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: theme.border }

    component Chip: Item {
        property string text
        property color tone
        property bool active: true
        property string mark            // an agent state: its StateMark, moving only while some agent is in it
        signal clicked
        height: parent.height
        width: r.implicitWidth + 4
        opacity: active ? 1 : 0.45
        Row {
            id: r
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Rectangle { visible: parent.parent.mark === "" || !parent.parent.active; anchors.verticalCenter: parent.verticalCenter; width: 7; height: 7; radius: 4; color: parent.parent.tone }
            StateMark { visible: parent.parent.mark !== "" && parent.parent.active; mark: parent.parent.mark; size: 7; anchors.verticalCenter: parent.verticalCenter }
            Text { text: parent.parent.text; color: theme.fg; font.family: theme.fontFamily; font.pixelSize: root.fs - 2 }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
    }

    Row {
        x: 14
        height: parent.height
        spacing: 16
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: win.space ? win.space.name + (win.space.branch ? "  ⎇ " + win.space.branch : "") : ""
            color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2
        }
        Chip { text: root.working + " working"; tone: theme.yellow; mark: "working"; active: root.working > 0 }
        Chip { text: root.blocked + " blocked"; tone: theme.red; mark: "blocked"; active: root.blocked > 0; onClicked: app.runAction("next-attention") }
        Chip { text: root.done + " done"; tone: theme.green; mark: "done"; active: root.done > 0; onClicked: app.runAction("next-attention") }
        Chip {
            visible: app.modalCount > 0
            text: app.modalCount + (app.modalCount === 1 ? " view" : " views") + (app.modalVisible ? "" : "  (hidden)")
            tone: theme.accent
            onClicked: app.runAction("toggle-views")
        }
    }

    Row {
        anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
        spacing: 14
        Row {
            spacing: 6
            Kbd { keys: "Ctrl+Shift+L"; size: root.fs - 4; anchors.verticalCenter: parent.verticalCenter }
            Text { anchors.verticalCenter: parent.verticalCenter; text: "agent"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2 }
        }
        Row {
            spacing: 6
            Kbd { keys: "Ctrl+Shift+?"; size: root.fs - 4; anchors.verticalCenter: parent.verticalCenter }
            Text { anchors.verticalCenter: parent.verticalCenter; text: "help"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2 }
        }
    }
}
