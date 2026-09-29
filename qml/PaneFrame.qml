import Nebula
import QtQuick

Item {
    id: root
    property var tab
    property int paneId: -1
    readonly property var session: tab ? tab.session(paneId) : null
    readonly property bool focused: tab && tab.focusedId === paneId && win.tab === tab && win.active
    readonly property int fs: Math.round(theme.fontSize * 1.33)

    HoverHandler {
        id: hover
        onHoveredChanged: if (hovered && settings.focusFollowsMouse && root.tab && win.tab === root.tab) root.tab.focusPane(root.paneId)
    }

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 8
        color: "transparent"
        radius: 4
        border.width: root.focused ? 2 : 1
        border.color: root.focused ? theme.accent : theme.border
    }

    Rectangle {
        x: 10; y: 0
        height: root.fs + 2
        width: title.implicitWidth + 22
        color: theme.bg
        Rectangle {
            visible: root.session && root.session.agentState !== undefined && root.session.agentState !== "" && root.session.agentState !== "none"
            x: 3; anchors.verticalCenter: parent.verticalCenter
            width: 7; height: 7; radius: 4
            color: root.session ? win.stateColor(root.session.agentState) : "transparent"
        }
        Text {
            id: title
            anchors { verticalCenter: parent.verticalCenter; right: parent.right; rightMargin: 5 }
            text: root.session ? (root.session.label || "shell") : ""
            color: root.focused ? theme.accent : theme.muted
            font.family: theme.fontFamily; font.pixelSize: root.fs - 1; font.bold: root.focused
        }
    }

    Connections {
        target: app
        function onFocusRequested() { if (root.focused) view.forceActiveFocus() }
    }

    Rectangle {
        visible: hover.hovered || root.focused
        opacity: hover.hovered ? 1 : 0.0
        anchors { right: parent.right; rightMargin: 10; top: parent.top }
        height: root.fs + 6
        width: btns.width + 8
        color: theme.bg
        Row {
            id: btns
            anchors.centerIn: parent
            IconButton { size: 11; icon: "split-right"; onClicked: { root.tab.focusPane(root.paneId); app.runAction("split-right") } }
            IconButton { size: 11; icon: "split-down"; onClicked: { root.tab.focusPane(root.paneId); app.runAction("split-down") } }
            IconButton { size: 11; icon: "zoom"; onClicked: { root.tab.focusPane(root.paneId); app.runAction("zoom-pane") } }
            IconButton { size: 11; icon: "close"; onClicked: root.tab.closePane(root.paneId) }
        }
    }

    TerminalView {
        id: view
        anchors { fill: parent; leftMargin: 6; rightMargin: 6; topMargin: 8 + 5; bottomMargin: 5 }
        session: root.session
        paneFocused: root.focused
        onActivated: if (root.tab) root.tab.focusPane(root.paneId)
        onContextMenuRequested: (x, y, sel, url) => win.openMenu(view, x, y, sel, url)
    }
}
