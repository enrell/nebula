import QtQuick

// The file explorer: the current space's project (its git work tree, else its folder) as a tree. A click opens a
// file (in the editor, or as a view for .neb and Markdown); folders open and close; the tree follows the disk.
FocusScope {
    id: root
    readonly property int fs: Math.round(theme.fontSize * 1.33)
    readonly property int rowH: fs + 8
    property string rootDir: ""
    property var kids: ({})          // folder -> its entries, as last read
    property var expanded: ({})      // folders shown open
    property var rows: []            // the visible tree, flattened
    property int sel: -1
    property string mode: ""         // "" | new-file | new-dir | rename | delete
    property string target: ""       // the folder (new-*) or the entry (rename, delete) the mode acts on
    property string error: ""
    property bool lastHidden: settings.explorerHidden
    readonly property string activeFile: {
        const t = win.tab
        const e = t ? t.editor(t.focusedId) : null
        return e ? e.path : ""
    }

    function load(dir, force) { if (force || !kids[dir]) kids[dir] = files.list(dir, settings.explorerHidden) }
    function rebuild() {
        const out = []
        const walk = (dir, depth) => {
            load(dir, false)
            for (const e of kids[dir]) {
                out.push({ path: e.path, name: e.name, dir: e.dir, ignored: e.ignored, depth: depth })
                if (e.dir && expanded[e.path]) walk(e.path, depth + 1)
            }
        }
        if (rootDir !== "") walk(rootDir, 0)
        rows = out
        if (sel >= rows.length) sel = rows.length - 1
        files.watch([rootDir].concat(Object.keys(expanded).filter(k => expanded[k])))
    }
    function setRoot() {
        const r = app.projectDir()
        if (r === rootDir) return
        rootDir = r; kids = ({}); expanded = ({}); sel = -1; mode = ""
        rebuild()
    }
    function refresh() { kids = ({}); rebuild() }
    function focusTree() { if (sel < 0 && rows.length) sel = 0; list.forceActiveFocus() }
    function toggle(i) {
        const r = rows[i]
        if (!r || !r.dir) return
        expanded[r.path] = !expanded[r.path]
        rebuild()
    }
    // A click opens the file and leaves the keyboard here (arrows, F2, Delete keep working on the tree); Enter or
    // a double click opens it and moves the keyboard into it, as in other editors.
    function activate(i, focusFile) {
        const r = rows[i]
        if (!r) return
        sel = i
        if (r.dir) { toggle(i); return }
        const res = app.openFile(r.path)
        error = res.error ? res.error : ""
        if (!focusFile) Qt.callLater(() => list.forceActiveFocus())
    }
    // the folder new entries go into: the selected folder, or the selected file's folder, or the root
    function targetDir() {
        const r = rows[sel]
        if (!r) return rootDir
        return r.dir ? r.path : files.parentDir(r.path)
    }
    function begin(m, t) {
        mode = m; target = t; error = ""
        input.text = m === "rename" ? t.split("/").pop() : ""
        if (m !== "delete") { input.forceActiveFocus(); if (m === "rename") input.select(0, input.text.lastIndexOf(".") > 0 ? input.text.lastIndexOf(".") : input.text.length) }
        else list.forceActiveFocus()
    }
    function commit() {
        let err = ""
        if (mode === "new-file") {
            err = files.createFile(target, input.text)
            if (err === "") { expanded[target] = true; refresh(); app.openFile(files.join(target, input.text.trim())) }
        } else if (mode === "new-dir") {
            err = files.createDir(target, input.text)
            if (err === "") { expanded[target] = true; refresh() }
        } else if (mode === "rename") {
            err = files.rename(target, input.text)
            if (err === "") refresh()
        } else if (mode === "delete") {
            err = files.trash(target)
            if (err === "") refresh()
        }
        error = err
        if (err === "") { mode = ""; list.forceActiveFocus() }
    }
    function cancel() { mode = ""; error = ""; list.forceActiveFocus() }
    function contextMenu(i, x, y) {
        sel = i
        const r = rows[i]
        const items = []
        if (r && !r.dir) items.push({ label: "Open", run: () => root.activate(i, true) })
        items.push({ label: "New file…", run: () => root.begin("new-file", root.targetDir()) },
                   { label: "New folder…", run: () => root.begin("new-dir", root.targetDir()) })
        if (r) items.push({ separator: true },
                          { label: "Rename…", hint: "F2", run: () => root.begin("rename", r.path) },
                          { label: "Move to trash…", hint: "Delete", run: () => root.begin("delete", r.path) },
                          { separator: true },
                          { label: "Copy path", run: () => files.copyPath(r.path) },
                          { label: "Terminal here", run: () => app.terminalAt(r.dir ? r.path : files.parentDir(r.path)) })
        win.openItemsMenu(root, x, y, items)
    }

    // the project follows the space; asking git is cheap but not free, so only shortly after things settle
    Timer { id: rootTimer; interval: 300; onTriggered: root.setRoot() }
    Connections {
        target: app
        function onCurrentChanged() { rootTimer.restart() }
        function onSpacesChanged() { rootTimer.restart() }
    }
    Connections {
        target: files
        function onChanged(dir) { if (root.kids[dir] !== undefined) { root.load(dir, true); root.rebuild() } }
    }
    Connections {
        target: settings
        function onChanged() { if (settings.explorerHidden !== root.lastHidden) { root.lastHidden = settings.explorerHidden; root.refresh() } }
    }
    Component.onCompleted: setRoot()

    Rectangle { anchors.fill: parent; color: theme.bg }

    component Tool: IconButton {
        size: 12
        anchors.verticalCenter: parent.verticalCenter
    }

    // ── header ──
    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: root.fs + 18
        Text {
            anchors { left: parent.left; leftMargin: 12; right: tools.left; rightMargin: 6; verticalCenter: parent.verticalCenter }
            text: root.rootDir.split("/").pop() || "/"
            elide: Text.ElideRight
            color: theme.fg; font.bold: true
            font.family: theme.fontFamily; font.pixelSize: root.fs - 1
        }
        Row {
            id: tools
            anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
            Tool { icon: "plus"; onClicked: root.begin("new-file", root.targetDir()) }
            Tool { icon: "folder"; onClicked: root.begin("new-dir", root.targetDir()) }
            Tool { icon: "refresh"; onClicked: root.refresh() }
            Tool { icon: "collapse"; onClicked: { root.expanded = ({}); root.rebuild() } }
            Tool { icon: "preview"; color: settings.explorerHidden ? theme.accent : theme.muted; onClicked: settings.explorerHidden = !settings.explorerHidden }
        }
    }

    // ── new / rename / delete ──
    Rectangle {
        id: ask
        visible: root.mode !== "" || root.error !== ""
        anchors { left: parent.left; right: parent.right; top: header.bottom; margins: 6 }
        height: visible ? col.implicitHeight + 10 : 0
        radius: 4
        color: theme.panel
        border.width: 1; border.color: root.error !== "" ? theme.red : theme.border
        Column {
            id: col
            x: 6; y: 5
            width: parent.width - 12
            spacing: 4
            Text {
                width: parent.width; wrapMode: Text.WrapAnywhere
                text: root.error !== "" ? root.error
                    : root.mode === "new-file" ? "new file in " + root.target.replace(root.rootDir, root.rootDir.split("/").pop())
                    : root.mode === "new-dir" ? "new folder in " + root.target.replace(root.rootDir, root.rootDir.split("/").pop())
                    : root.mode === "rename" ? "rename " + root.target.split("/").pop()
                    : root.mode === "delete" ? "move " + root.target.split("/").pop() + " to the trash?   Enter / Esc" : ""
                color: root.error !== "" ? theme.red : theme.muted
                font.family: theme.fontFamily; font.pixelSize: root.fs - 3
            }
            Rectangle {
                visible: root.mode === "new-file" || root.mode === "new-dir" || root.mode === "rename"
                width: parent.width; height: root.fs + 10; radius: 3
                color: theme.bg; border.width: 1; border.color: theme.accent
                TextInput {
                    id: input
                    anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                    verticalAlignment: TextInput.AlignVCenter
                    color: theme.fg; selectionColor: theme.accent; selectedTextColor: theme.bg
                    font.family: theme.fontFamily; font.pixelSize: root.fs - 1
                    clip: true
                    Keys.onPressed: (e) => {
                        if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) { e.accepted = true; root.commit() }
                        else if (e.key === Qt.Key_Escape) { e.accepted = true; root.cancel() }
                    }
                }
            }
        }
    }

    // ── the tree ──
    ListView {
        id: list
        anchors { left: parent.left; right: parent.right; top: ask.visible ? ask.bottom : header.bottom; topMargin: 4; bottom: parent.bottom }
        clip: true
        focus: true
        model: root.rows
        boundsBehavior: Flickable.StopAtBounds
        currentIndex: root.sel
        highlightFollowsCurrentItem: false
        Keys.onPressed: (e) => {
            const r = root.rows[root.sel]
            if (root.mode === "delete") {
                if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) root.commit()
                else if (e.key === Qt.Key_Escape) root.cancel()
                e.accepted = true
                return
            }
            switch (e.key) {
            case Qt.Key_Down: root.sel = Math.min(root.rows.length - 1, root.sel + 1); break
            case Qt.Key_Up: root.sel = Math.max(0, root.sel - 1); break
            case Qt.Key_Right: if (r && r.dir && !root.expanded[r.path]) root.toggle(root.sel); else root.sel = Math.min(root.rows.length - 1, root.sel + 1); break
            case Qt.Key_Left:
                if (r && r.dir && root.expanded[r.path]) root.toggle(root.sel)
                else if (r) { const parent = files.parentDir(r.path); const i = root.rows.findIndex(x => x.path === parent); if (i >= 0) root.sel = i }
                break
            case Qt.Key_Return: case Qt.Key_Enter: root.activate(root.sel, true); break
            case Qt.Key_F2: if (r) root.begin("rename", r.path); break
            case Qt.Key_Delete: if (r) root.begin("delete", r.path); break
            default: return
            }
            e.accepted = true
            list.positionViewAtIndex(root.sel, ListView.Contain)
        }
        delegate: Rectangle {
            id: row
            required property var modelData
            required property int index
            width: list.width
            height: root.rowH
            color: index === root.sel && list.activeFocus ? theme.panel : hover.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
            readonly property bool active: modelData.path === root.activeFile
            HoverHandler { id: hover }
            Rectangle { visible: row.active; width: 2; height: parent.height; color: theme.accent }
            Text {
                id: chevron
                x: 8 + row.modelData.depth * 12
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.dir ? (root.expanded[row.modelData.path] ? "▾" : "▸") : ""
                width: 12
                color: theme.muted; font.pixelSize: root.fs - 2
            }
            // a small mark per kind of file
            Rectangle {
                id: mark
                anchors { left: chevron.right; verticalCenter: parent.verticalCenter }
                width: 7; height: 7; radius: row.modelData.dir ? 1 : 4
                color: {
                    if (row.modelData.dir) return theme.blue
                    const ext = row.modelData.name.split(".").pop().toLowerCase()
                    if (["c", "h", "cpp", "hpp", "cc", "rs", "go", "java", "kt", "zig"].indexOf(ext) >= 0) return theme.accent
                    if (["py", "js", "ts", "tsx", "jsx", "qml", "lua", "sh", "fish"].indexOf(ext) >= 0) return theme.yellow
                    if (["md", "neb", "txt", "rst"].indexOf(ext) >= 0) return theme.green
                    if (["json", "yaml", "yml", "toml", "ini", "conf", "lock"].indexOf(ext) >= 0) return theme.red
                    return theme.muted
                }
                opacity: row.modelData.ignored ? 0.4 : 0.85
            }
            Text {
                anchors { left: mark.right; leftMargin: 7; right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                text: row.modelData.name
                elide: Text.ElideMiddle
                color: row.active ? theme.accent : row.modelData.ignored ? theme.muted : theme.fg
                opacity: row.modelData.ignored ? 0.7 : 1
                font.family: theme.fontFamily; font.pixelSize: root.fs - 1
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onPressed: list.forceActiveFocus()
                onClicked: (m) => {
                    if (m.button === Qt.RightButton) root.contextMenu(row.index, m.x, row.y - list.contentY + list.y + m.y)
                    else root.activate(row.index, false)
                }
                onDoubleClicked: (m) => { if (m.button === Qt.LeftButton && !row.modelData.dir) root.activate(row.index, true) }
            }
        }
        // right-click on the empty part: new file / folder in the root
        MouseArea {
            anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: list.contentHeight - list.contentY; bottom: parent.bottom }
            acceptedButtons: Qt.RightButton
            onClicked: (m) => { root.sel = -1; root.contextMenu(-1, m.x, m.y + list.contentHeight - list.contentY + list.y) }
        }
        Text {
            visible: root.rows.length === 0
            anchors.centerIn: parent
            text: "empty folder"
            color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2
        }
    }
}
