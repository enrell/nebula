import Nebula
import QtQuick

Item {
    id: root
    clip: true
    property var tab
    property int paneId: -1
    property string kind: "terminal"          // "terminal" | "view"
    readonly property bool isView: kind === "view"
    // the pane object: a TerminalSession, or a ViewPane for views (both have label and agentState)
    readonly property var session: tab ? (isView ? tab.view(paneId) : tab.session(paneId)) : null
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
        StateMark {
            mark: root.session && root.session.agentState ? root.session.agentState : ""
            x: 1; anchors.verticalCenter: parent.verticalCenter
            size: 7
        }
        Text {
            id: title
            anchors { verticalCenter: parent.verticalCenter; right: parent.right; rightMargin: 5 }
            text: (root.session ? (root.session.label || "shell") : "")
                  + (root.isView && root.session && root.session.errorCount > 0 ? "  ·  " + root.session.errorCount + (root.session.errorCount === 1 ? " error" : " errors") : "")
            color: root.isView && root.session && root.session.errorCount > 0 ? theme.red : root.focused ? theme.accent : theme.muted
            font.family: theme.fontFamily; font.pixelSize: root.fs - 1; font.bold: root.focused
        }
    }

    Connections {
        target: app
        function onFocusRequested() { if (root.focused && content.item) content.item.forceActiveFocus() }
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
            IconButton { size: 11; icon: "popout"; visible: root.isView; onClicked: app.popOutView(root.paneId) }
            IconButton { size: 11; icon: "split-right"; onClicked: { root.tab.focusPane(root.paneId); app.runAction("split-right") } }
            IconButton { size: 11; icon: "split-down"; onClicked: { root.tab.focusPane(root.paneId); app.runAction("split-down") } }
            IconButton { size: 11; icon: "zoom"; onClicked: { root.tab.focusPane(root.paneId); app.runAction("zoom-pane") } }
            IconButton { size: 11; icon: "close"; onClicked: root.tab.closePane(root.paneId) }
        }
    }

    Loader {
        id: content
        anchors { fill: parent; leftMargin: 6; rightMargin: 6; topMargin: 8 + 5; bottomMargin: 5 }
        sourceComponent: root.isView ? viewContent : terminalContent
    }

    Component {
        id: terminalContent
        TerminalView {
            id: view
            session: root.session
            paneFocused: root.focused
            onActivated: if (root.tab) root.tab.focusPane(root.paneId)
            onContextMenuRequested: (x, y, sel, url) => win.openMenu(view, x, y, sel, url)
        }
    }

    Component {
        id: viewContent
        ViewContent {
            pane: root.session
            paneFocused: root.focused
            onActivated: if (root.tab) root.tab.focusPane(root.paneId)
        }
    }
}
