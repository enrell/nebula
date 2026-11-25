import QtQuick

// Ctrl+Shift+P: type part of a path, Enter opens it in an editor. A path that does not exist is created on save.
Dialog {
    id: root
    title: "open file"
    contentWidth: 720
    property var results: []
    property int sel: 0
    property string error: ""
    readonly property string dir: app.currentDir()

    function refresh() { results = app.quickOpen(input.text, 40); sel = 0; error = "" }
    function open(path) {
        app.hideOverlay()
        const r = app.openFile(path)
        if (r.error) { app.showOverlay("quickopen"); error = r.error }
    }
    function accept() {
        const t = input.text.trim()
        if (results.length > 0) open(results[sel].abs)
        else if (t !== "") open(t.startsWith("/") || t.startsWith("~") ? t.replace(/^~/, app.currentDir().split("/").slice(0, 3).join("/")) : root.dir + "/" + t)
    }
    Component.onCompleted: { refresh(); input.forceActiveFocus() }

    Column {
        width: parent.width
        spacing: 8
        Rectangle {
            width: parent.width; height: root.fs + 16; radius: 5
            color: "transparent"; border.width: 1; border.color: theme.accent
            TextInput {
                id: input
                anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                verticalAlignment: TextInput.AlignVCenter
                color: theme.fg; selectionColor: theme.accent; selectedTextColor: theme.bg
                font.family: theme.fontFamily; font.pixelSize: root.fs
                onTextChanged: root.refresh()
                Keys.onPressed: (e) => {
                    if (e.key === Qt.Key_Down) { root.sel = Math.min(root.sel + 1, root.results.length - 1); e.accepted = true }
                    else if (e.key === Qt.Key_Up) { root.sel = Math.max(root.sel - 1, 0); e.accepted = true }
                    else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) { root.accept(); e.accepted = true }
                    else if (e.key === Qt.Key_Escape) { app.hideOverlay(); e.accepted = true }
                }
            }
        }
        Text {
            text: root.error !== "" ? root.error : root.dir + "   ·   ↑↓ choose, Enter opens; a new path creates the file"
            color: root.error !== "" ? theme.red : theme.muted
            font.family: theme.fontFamily; font.pixelSize: root.fs - 3
            width: parent.width; elide: Text.ElideLeft
        }
        Column {
            width: parent.width
            Repeater {
                model: root.results.slice(0, 14)
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: parent.width; height: root.fs + 10; radius: 3
                    color: index === root.sel ? theme.panel : "transparent"
                    Text {
                        anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                        width: parent.width - 16; elide: Text.ElideLeft
                        text: modelData.path
                        color: index === root.sel ? theme.accent : theme.fg
                        font.family: theme.fontFamily; font.pixelSize: root.fs - 1
                    }
                    MouseArea { anchors.fill: parent; hoverEnabled: true; onEntered: root.sel = index; onClicked: root.open(modelData.abs) }
                }
            }
        }
    }
}
