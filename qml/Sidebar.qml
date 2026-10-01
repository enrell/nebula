import QtQuick

Rectangle {
    id: root
    color: theme.bg
    readonly property int fs: Math.round(theme.fontSize * 1.33)

    function stateColor(s) {
        switch (s) {
        case "working": return theme.yellow
        case "blocked": return theme.red
        case "done": return theme.green
        case "idle": return theme.muted
        default: return "transparent"
        }
    }

    component Header: Item {
        property alias text: t.text
        property alias trailing: r.text
        property string icon
        signal trailingClicked
        height: root.fs + 16
        width: parent ? parent.width : 0
        Text {
            id: t
            x: 16; anchors.verticalCenter: parent.verticalCenter
            color: theme.muted; font.bold: true
            font.family: theme.fontFamily; font.pixelSize: root.fs - 1
        }
        Text {
            id: r
            anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
            color: theme.muted
            font.family: theme.fontFamily; font.pixelSize: root.fs - 1
        }
        IconButton {
            visible: parent.icon !== ""
            icon: parent.icon
            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            onClicked: parent.trailingClicked()
        }
    }

    component Row2: Rectangle {
        id: row
        property string title
        property string subtitle
        property color dot: "transparent"
        property string mark            // an agent state: drawn as a StateMark instead of the plain dot
        property bool selected: false
        signal clicked
        signal middleClicked
        signal rightClicked(real x, real y)
        signal renamed(string name)
        property bool closable: false
        signal closeClicked
        property bool editable: false
        property bool editing: false
        function startEdit() { if (editable) { editing = true; editor.text = title; editor.forceActiveFocus(); editor.selectAll() } }
        width: parent ? parent.width : 0
        height: root.fs * 2 + 14
        color: selected ? theme.panel : (ma.containsMouse ? Qt.darker(theme.panel, 1.4) : "transparent")
        Rectangle { visible: row.selected; width: 3; height: parent.height; color: theme.accent }
        Rectangle {
            visible: row.mark === ""
            x: 16; y: 15; width: 8; height: 8; radius: 4
            color: row.dot
        }
        StateMark { mark: row.mark; x: 14; y: 13; size: 8 }
        Column {
            x: 34; anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Text {
                visible: !row.editing
                text: row.title; color: theme.fg; font.bold: true; elide: Text.ElideRight; width: row.width - 50
                font.family: theme.fontFamily; font.pixelSize: root.fs
            }
            TextInput {
                id: editor
                visible: row.editing
                width: row.width - 50
                color: theme.fg; selectionColor: theme.accent; selectedTextColor: theme.bg
                font.bold: true; font.family: theme.fontFamily; font.pixelSize: root.fs
                clip: true
                property bool done: false
                onVisibleChanged: if (visible) done = false
                function finish(save) {
                    if (done) return
                    done = true
                    row.editing = false
                    if (save) row.renamed(text)
                    else app.focusRequested()
                }
                Keys.onReturnPressed: finish(true)
                Keys.onEnterPressed: finish(true)
                Keys.onEscapePressed: finish(false)
                onActiveFocusChanged: if (!activeFocus && row.editing) finish(true)
                Rectangle { anchors { left: parent.left; right: parent.right; top: parent.bottom } height: 1; color: theme.accent }
            }
            Text {
                text: row.subtitle; color: row.dot.a > 0 ? row.dot : theme.muted; elide: Text.ElideRight; width: row.width - 50
                font.family: theme.fontFamily; font.pixelSize: root.fs - 2
            }
        }
        IconButton {
            visible: row.closable && (ma.containsMouse || hovered)
            icon: "close"; size: 10
            anchors { right: parent.right; rightMargin: 6; top: parent.top; topMargin: 6 }
            z: 2
            onClicked: row.closeClicked()
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
            onClicked: (m) => m.button === Qt.MiddleButton ? row.middleClicked() : m.button === Qt.RightButton ? row.rightClicked(m.x + row.x, m.y + row.y) : row.clicked()
            onDoubleClicked: row.startEdit()
        }
    }

    Column {
        id: top
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: parent.height * 0.55
        Header { text: "spaces"; icon: "plus"; onTrailingClicked: app.newSpace() }
        ListView {
            id: spaceList
            width: parent.width
            height: parent.height - root.fs - 16
            clip: true
            model: app.spaces
            Connections {
                target: app
                function onRenameRequested(i) { const it = spaceList.itemAtIndex(i); if (it) it.startEdit() }
            }
            delegate: Row2 {
                required property var modelData
                required property int index
                title: modelData.name
                subtitle: modelData.branch
                dot: root.stateColor(modelData.state) === "transparent" ? theme.accent : root.stateColor(modelData.state)
                mark: modelData.state || ""
                selected: index === app.currentIndex
                editable: true
                closable: true
                onCloseClicked: app.closeSpace(index)
                onRightClicked: (x, y) => win.openSpaceMenu(index, x, y + spaceList.y + top.y)
                onRenamed: (n) => app.renameSpace(index, n)
                onClicked: app.setCurrent(index)
                onMiddleClicked: app.closeSpace(index)
            }
        }
    }

    Rectangle { anchors { left: parent.left; right: parent.right; top: top.bottom } height: 1; color: theme.border }

    Column {
        anchors { left: parent.left; right: parent.right; top: top.bottom; topMargin: 1; bottom: foot.top }
        Header { text: "agents" + (app.agents.length > 0 ? "  " + app.agents.length : ""); icon: "plus"; onTrailingClicked: app.runAction("launch-agent") }
        ListView {
            width: parent.width
            height: parent.height - root.fs - 16
            clip: true
            model: app.agents
            Column {
                visible: app.agents.length === 0
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16; topMargin: 8 }
                spacing: 10
                Text {
                    width: parent.width; wrapMode: Text.WordWrap
                    text: "No agents running yet. Launch one and its status (working, blocked, done) shows up here."
                    color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                }
                Button { label: "Launch an agent"; primary: true; onClicked: app.runAction("launch-agent") }
            }
            delegate: Row2 {
                required property var modelData
                title: modelData.space
                // working and blocked show for how long: a clock that keeps going is the difference between busy and stuck
                subtitle: (modelData.state === "done" && modelData.summary) ? modelData.summary
                        : modelData.state + ((modelData.state === "working" || modelData.state === "blocked") && modelData.since ? " " + win.since(modelData.since) : "")
                          + " · " + modelData.agent
                dot: root.stateColor(modelData.state)
                mark: modelData.state
                onClicked: app.focusAgent(modelData.spaceIndex, modelData.tabIndex, modelData.pane)
            }
        }
    }

    Item {
        id: foot
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 48
        Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: theme.border }
        Button {
            id: launchBtn
            x: 10; anchors.verticalCenter: parent.verticalCenter
            label: "+ Agent"; primary: true
            onClicked: app.runAction("launch-agent")
        }
        Row {
            anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
            IconButton { icon: "operator"; size: 16; onClicked: app.runAction("operator") }
            IconButton { icon: "keys"; size: 16; onClicked: app.runAction("toggle-help") }
            IconButton {
                icon: "gear"; size: 16
                color: app.settingsVisible ? theme.accent : theme.muted
                onClicked: app.setSettingsVisible(!app.settingsVisible)
            }
        }
    }
}
