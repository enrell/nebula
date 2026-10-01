import QtQuick

Window {
    id: win
    width: 1280
    height: 760
    visible: true
    title: "nebula"
    color: theme.bg

    readonly property var space: app.currentSpace
    readonly property var tab: space ? space.currentTab : null
    readonly property int uiSize: Math.round(theme.fontSize * 1.33)

    function stateColor(s) {
        switch (s) {
        case "working": return theme.yellow
        case "blocked": return theme.red
        case "done": return theme.green
        case "idle": return theme.muted
        default: return "transparent"
        }
    }
    function countState(s) { return app.agents.filter(a => a.state === s).length }

    // one clock for every state mark, so all spinners turn together, and one for "working 2m 13s"
    readonly property var spinFrames: ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]
    property int spin: 0
    property double now: Date.now()
    readonly property bool anyWorking: app.agents.some(a => a.state === "working")
    Timer { interval: 80; repeat: true; running: win.anyWorking; onTriggered: win.spin = (win.spin + 1) % win.spinFrames.length }
    Timer { interval: 1000; repeat: true; triggeredOnStart: true; running: app.agents.length > 0; onTriggered: win.now = Date.now() }
    // how long an agent has been in its state: "12s", "4m", "1h 20m"
    function since(ms) {
        if (!ms) return ""
        const s = Math.max(0, Math.floor((win.now - ms) / 1000))
        if (s < 60) return s + "s"
        if (s < 3600) return Math.floor(s / 60) + "m " + (s % 60 < 10 ? "0" : "") + (s % 60) + "s"
        return Math.floor(s / 3600) + "h " + Math.floor(s % 3600 / 60) + "m"
    }

    Component.onCompleted: if (!settings.onboarded) app.showOverlay("onboarding")

    Sidebar {
        id: sidebar
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: app.sidebarVisible ? settings.sidebarWidth : 0
        visible: app.sidebarVisible
    }

    Rectangle {
        anchors { left: sidebar.right; top: parent.top; bottom: parent.bottom }
        width: 1
        visible: app.sidebarVisible
        color: theme.border
    }

    MouseArea {
        visible: app.sidebarVisible
        x: sidebar.width - 3
        width: 7
        height: parent.height
        z: 5
        cursorShape: Qt.SplitHCursor
        preventStealing: true
        onPositionChanged: (m) => settings.sidebarWidth = Math.round(mapToItem(win.contentItem, m.x, 0).x)
    }

    function openMenu(view, x, y, hasSel, url) {
        const p = view.mapToItem(win.contentItem, x, y)
        const zoomed = tab && tab.zoomed
        const items = [
            { label: "Copy", hint: "Ctrl+Shift+C", enabled: hasSel, run: () => view.copy() },
            { label: "Paste", hint: "Ctrl+Shift+V", run: () => view.pasteClipboard() }
        ]
        if (url) items.push({ label: "Open link", run: () => view.openUrl(url) })
        items.push({ separator: true },
            { label: "Ask operator about selection…", enabled: hasSel, run: () => app.showOverlay("operator", { context: view.selectedText() }) },
            { label: "Operator…", hint: "Ctrl+Shift+I", run: () => app.runAction("operator") },
            { separator: true },
            { label: "Split right", hint: "Ctrl+Shift+D", run: () => app.runAction("split-right") },
            { label: "Split down", hint: "Ctrl+Shift+E", run: () => app.runAction("split-down") },
            { label: zoomed ? "Unzoom pane" : "Zoom pane", hint: "Ctrl+Shift+Z", run: () => app.runAction("zoom-pane") },
            { label: "Close pane", hint: "Ctrl+Shift+W", run: () => app.runAction("close-pane") },
            { separator: true },
            { label: "Settings", hint: "Ctrl+,", run: () => app.setSettingsVisible(true) })
        menu.open(p.x, p.y, items)
    }

    function openSpaceMenu(i, x, y) {
        const p = sidebar.mapToItem(win.contentItem, x, y)
        const cur = app.spaces[i].profile
        const items = [
            { label: "Rename", hint: "Ctrl+Shift+R", run: () => { app.setCurrent(i); app.runAction("rename-space") } },
            { separator: true },
            { label: (cur === "" ? "● " : "  ") + "profile: default", run: () => app.setSpaceProfile(i, "") }
        ]
        profiles.list.forEach(pr => items.push({ label: (cur === pr.name ? "● " : "  ") + "profile: " + pr.name, run: () => app.setSpaceProfile(i, pr.name) }))
        items.push({ separator: true }, { label: "Close space", run: () => app.closeSpace(i) })
        menu.open(p.x, p.y, items)
    }

    TabBar {
        id: tabs
        anchors { left: sidebar.right; leftMargin: 1; right: parent.right; top: parent.top }
        height: win.uiSize + 14
    }

    StatusBar {
        id: status
        anchors { left: sidebar.right; leftMargin: 1; right: parent.right; bottom: parent.bottom }
        height: win.uiSize + 12
    }

    Item {
        anchors { left: sidebar.right; leftMargin: 1; right: parent.right; top: tabs.bottom; bottom: status.top }
        Repeater {
            model: win.space ? win.space.tabs : []
            delegate: PaneTree {
                required property var modelData
                required property int index
                anchors.fill: parent
                anchors.margins: 4
                visible: index === win.space.currentIndex
                tab: modelData
                node: modelData.layout
                Connections {
                    target: modelData
                    function onLayoutChanged() { node = modelData.layout }
                }
            }
        }
    }

    SettingsPage {
        anchors { left: sidebar.right; leftMargin: 1; right: parent.right; top: parent.top; bottom: parent.bottom }
        visible: app.settingsVisible
        z: 10
    }

    ViewModal {
        anchors { left: sidebar.right; leftMargin: 1; right: parent.right; top: parent.top; bottom: status.top }
        z: 12
    }

    Loader { anchors.fill: parent; z: 15; active: app.overlay === "launcher"; source: "LauncherDialog.qml" }
    Loader { anchors.fill: parent; z: 15; active: app.overlay === "operator"; source: "OperatorPanel.qml" }
    Loader { anchors.fill: parent; z: 15; active: app.overlay === "broadcast"; source: "BroadcastDialog.qml" }
    Loader { anchors.fill: parent; z: 16; active: app.overlay === "onboarding"; source: "OnboardingWizard.qml" }

    ContextMenu {
        id: menu
        anchors.fill: parent
        z: 20
    }

    Rectangle {
        anchors.fill: parent
        visible: app.helpVisible
        color: Qt.rgba(0, 0, 0, 0.86)
        MouseArea { anchors.fill: parent; onClicked: app.hideHelp() }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - 40, 640)
            height: Math.min(parent.height - 40, helpList.contentHeight + 64)
            color: theme.bg
            radius: 10
            border.color: theme.accent
            border.width: 1

            Text {
                x: 20; y: 14
                text: "keyboard shortcuts"
                color: theme.accent; font.bold: true
                font.family: theme.fontFamily; font.pixelSize: win.uiSize
            }
            ListView {
                id: helpList
                anchors { fill: parent; topMargin: 44; leftMargin: 20; rightMargin: 20; bottomMargin: 12 }
                clip: true
                model: app.bindings
                delegate: Item {
                    required property var modelData
                    width: helpList.width
                    height: win.uiSize + 8
                    Text {
                        text: modelData.desc; color: theme.fg
                        font.family: theme.fontFamily; font.pixelSize: win.uiSize - 1
                    }
                    Kbd {
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        keys: modelData.keys
                    }
                }
            }
        }
    }
}
