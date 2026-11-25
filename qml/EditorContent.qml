import Nebula
import QtQuick

// The body of an editor pane: the text (EditorView, native), a find / replace / go-to bar, a message bar (the file
// changed on disk, a save error), the "save changes?" question when closing, and a status line.
FocusScope {
    id: root
    property var pane                  // EditorPane
    property bool paneFocused: false
    signal activated()
    readonly property int fs: Math.round(theme.fontSize * 1.33)
    property string mode: ""           // "" | find | replace | goto | saveas
    property bool optCase: false
    property bool optWord: false
    property bool optRegex: false
    property int revision: 0           // bumped on every edit, so the match count follows replacements
    readonly property int matches: (revision, mode === "find" || mode === "replace") && root.pane ? root.pane.countMatches(query.text, optCase, optWord, optRegex) : 0

    function openBar(m, text) {
        mode = m
        if (m === "goto") { query.text = ""; }
        else if (m === "saveas") { query.text = text || "" }
        else if (text) query.text = text
        query.forceActiveFocus()
        query.selectAll()
    }
    function closeBar() { mode = ""; editor.forceActiveFocus() }
    function next(back) { if (root.pane && query.text !== "") root.pane.find(query.text, back, optCase, optWord, optRegex) }

    onActiveFocusChanged: if (activeFocus && mode === "") editor.forceActiveFocus()

    Connections {
        target: root.pane
        function onTextChanged() { if (root.mode !== "") root.revision++ }
        function onFindRequested(m, text) {
            if (m === "next" || m === "previous") { if (query.text !== "") root.next(m === "previous"); else root.openBar("find", "") }
            else root.openBar(m, text)
        }
    }

    EditorView {
        id: editor
        // the find bar sits above the text instead of over its first lines
        anchors { left: parent.left; right: parent.right; bottom: status.top
                  top: bar.visible ? bar.bottom : msg.visible ? msg.bottom : parent.top; topMargin: bar.visible ? 4 : 0 }
        pane: root.pane
        focus: true
        paneFocused: root.paneFocused && root.mode === "" && !(root.pane && root.pane.closeRequested)
        searchText: root.mode === "find" || root.mode === "replace" ? query.text : ""
        searchCase: root.optCase
        searchWord: root.optWord
        searchRegex: root.optRegex
        onActivated: root.activated()
    }

    // ── message bar: the file changed on disk, a save error, restored unsaved text ──
    Rectangle {
        id: msg
        visible: !!root.pane && root.pane.message !== ""
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: visible ? root.fs + 14 : 0
        color: theme.panel
        Text {
            anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
            text: root.pane ? root.pane.message : ""
            color: root.pane && root.pane.changedOnDisk ? theme.yellow : theme.fg
            font.family: theme.fontFamily; font.pixelSize: root.fs - 2
        }
        Row {
            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            spacing: 6
            Button { visible: !!root.pane && root.pane.changedOnDisk; label: "Reload"; onClicked: root.pane.reload() }
            Button { visible: !!root.pane && root.pane.changedOnDisk; label: "Keep mine"; onClicked: root.pane.keepMine() }
            IconButton { visible: !!root.pane && !root.pane.changedOnDisk; size: 10; icon: "close"; onClicked: root.pane.dismissMessage() }
        }
    }

    // ── find / replace / go to line / save as ──
    Rectangle {
        id: bar
        visible: root.mode !== ""
        z: 2
        anchors { right: parent.right; rightMargin: 14; top: msg.visible ? msg.bottom : parent.top; topMargin: 4 }
        width: Math.min(parent.width - 28, 560)
        height: col.implicitHeight + 12
        radius: 5
        color: theme.panel
        border.width: 1; border.color: theme.border

        component BarInput: Rectangle {
            id: bi
            property alias input: ti
            property alias text: ti.text
            property string placeholder
            signal dismissed()
            signal submitted(bool shift)
            height: root.fs + 12
            radius: 4
            color: theme.bg
            border.width: 1; border.color: ti.activeFocus ? theme.accent : theme.border
            TextInput {
                id: ti
                anchors { fill: parent; leftMargin: 7; rightMargin: 7 }
                verticalAlignment: TextInput.AlignVCenter
                color: theme.fg; selectionColor: theme.accent; selectedTextColor: theme.bg
                font.family: theme.fontFamily; font.pixelSize: root.fs - 1
                clip: true
                Keys.onPressed: (e) => {
                    if (e.key === Qt.Key_Escape) { e.accepted = true; bi.dismissed() }
                    else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) { e.accepted = true; bi.submitted((e.modifiers & Qt.ShiftModifier) !== 0) }
                }
                Text {
                    visible: !ti.text; anchors.verticalCenter: parent.verticalCenter
                    text: bi.placeholder; color: theme.muted; font: ti.font
                }
            }
        }
        component Toggle2: Rectangle {
            property string label
            property bool on: false
            signal toggled
            width: t2.implicitWidth + 10; height: root.fs + 8; radius: 3
            color: on ? theme.accent : "transparent"
            border.width: 1; border.color: on ? theme.accent : theme.border
            Text { id: t2; anchors.centerIn: parent; text: parent.label; color: parent.on ? theme.bg : theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 3 }
            TapHandler { onTapped: parent.toggled() }
        }

        Column {
            id: col
            x: 6; y: 6
            width: parent.width - 12
            spacing: 6
            Row {
                spacing: 6
                width: parent.width
                BarInput {
                    id: queryBox
                    width: parent.width - opts.width - 6
                    placeholder: root.mode === "goto" ? "line[:column]" : root.mode === "saveas" ? "save as (path)" : "find"
                    onDismissed: root.closeBar()
                    onSubmitted: (shift) => {
                        if (root.mode === "goto") {
                            const parts = queryBox.text.split(":")
                            const l = parseInt(parts[0]), c = parts.length > 1 ? parseInt(parts[1]) : 1
                            if (!isNaN(l)) root.pane.gotoLine(l, isNaN(c) ? 1 : c)
                            root.closeBar()
                        } else if (root.mode === "saveas") {
                            if (root.pane.saveAs(queryBox.text)) root.closeBar()
                        } else root.next(shift)
                    }
                }
                Row {
                    id: opts
                    spacing: 4
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.mode === "find" || root.mode === "replace"
                    width: visible ? implicitWidth : 0
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: query.text === "" ? "" : root.matches === 0 ? "no results" : root.matches + (root.matches === 1 ? " match" : " matches")
                        color: root.matches === 0 && query.text !== "" ? theme.red : theme.muted
                        font.family: theme.fontFamily; font.pixelSize: root.fs - 3
                    }
                    Toggle2 { label: "Aa"; on: root.optCase; onToggled: root.optCase = !root.optCase }
                    Toggle2 { label: "W"; on: root.optWord; onToggled: root.optWord = !root.optWord }
                    Toggle2 { label: ".*"; on: root.optRegex; onToggled: root.optRegex = !root.optRegex }
                    IconButton { size: 10; icon: "back"; rotation: 90; onClicked: root.next(true) }
                    IconButton { size: 10; icon: "back"; rotation: -90; onClicked: root.next(false) }
                    IconButton { size: 10; icon: "close"; onClicked: root.closeBar() }
                }
            }
            Row {
                visible: root.mode === "replace"
                spacing: 6
                width: parent.width
                BarInput {
                    id: replaceBox; width: parent.width - rbtn.width - 6; placeholder: "replace with"
                    onDismissed: root.closeBar()
                    onSubmitted: root.pane.replaceNext(query.text, replaceBox.text, root.optCase, root.optWord, root.optRegex)
                }
                Row {
                    id: rbtn
                    spacing: 4
                    Button { label: "Replace"; onClicked: root.pane.replaceNext(query.text, replaceBox.text, root.optCase, root.optWord, root.optRegex) }
                    Button { label: "All"; onClicked: root.pane.replaceAll(query.text, replaceBox.text, root.optCase, root.optWord, root.optRegex) }
                }
            }
        }
    }
    // the query field, reachable by id from the functions above
    readonly property var query: queryBox.input

    // ── status line ──
    Item {
        id: status
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: root.fs + 6
        Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: theme.border; opacity: 0.6 }
        Text {
            id: statusText
            anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
            width: Math.min(implicitWidth, parent.width - 16)
            elide: Text.ElideRight
            text: root.pane ? root.pane.status : ""
            color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 3
        }
        Text {
            // the path takes what the status leaves, and disappears when there is no room for it
            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            width: Math.max(0, Math.min(implicitWidth, parent.width - statusText.width - 40))
            visible: width > 60
            elide: Text.ElideLeft
            text: root.pane ? (root.pane.dirty ? "● " : "") + root.pane.path : ""
            color: root.pane && root.pane.dirty ? theme.yellow : theme.muted
            font.family: theme.fontFamily; font.pixelSize: root.fs - 3
        }
    }

    // ── "save changes?" when a modified file is closed ──
    Rectangle {
        anchors.fill: parent
        visible: !!root.pane && root.pane.closeRequested
        z: 5
        color: Qt.rgba(0, 0, 0, 0.55)
        MouseArea { anchors.fill: parent }
        onVisibleChanged: if (visible) confirm.forceActiveFocus()
        FocusScope {
            id: confirm
            anchors.centerIn: parent
            width: Math.min(parent.width - 40, 460)
            height: cc.implicitHeight + 28
            Keys.onPressed: (e) => {
                if (e.key === Qt.Key_Escape) { root.pane.cancelClose(); editor.forceActiveFocus(); e.accepted = true }
                else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) { root.pane.resolveClose(true); e.accepted = true }
            }
            Rectangle {
                anchors.fill: parent
                radius: 6; color: theme.bg; border.width: 1; border.color: theme.accent
                Column {
                    id: cc
                    x: 14; y: 14
                    width: parent.width - 28
                    spacing: 12
                    Text {
                        width: parent.width; wrapMode: Text.WordWrap
                        text: "Save changes to " + (root.pane ? root.pane.fileName : "") + " before closing?"
                        color: theme.fg; font.family: theme.fontFamily; font.pixelSize: root.fs; font.bold: true
                    }
                    Row {
                        spacing: 8
                        Button { label: "Save  ⏎"; primary: true; onClicked: root.pane.resolveClose(true) }
                        Button { label: "Don't save"; danger: true; onClicked: root.pane.resolveClose(false) }
                        Button { label: "Cancel  esc"; onClicked: { root.pane.cancelClose(); editor.forceActiveFocus() } }
                    }
                }
            }
        }
    }
}
