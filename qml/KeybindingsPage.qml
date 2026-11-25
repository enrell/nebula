import QtQuick

// Settings > Keybindings: search, record, remove and reset shortcuts. Everything is written to keys.conf.
Column {
    id: root
    readonly property int fs: Math.round(theme.fontSize * 1.33)
    property string query: ""
    property var keymap: app.keymap()
    property string recording: ""        // the action whose new shortcut is being recorded
    property string captured: ""         // the combination pressed while recording
    property var conflict: ({})          // {action, desc} already bound to `captured`
    spacing: 0

    Connections { target: app; function onBindingsChanged() { root.keymap = app.keymap() } }
    onVisibleChanged: if (!visible) stopRecording()

    function matches(e) {
        const q = query.trim().toLowerCase()
        if (q === "") return true
        const hay = (e.desc + " " + e.action + " " + e.keys.join(" ")).toLowerCase()
        return q.split(/\s+/).every(w => hay.indexOf(w) >= 0 || hay.replace(/\+/g, " ").indexOf(w) >= 0)
    }
    function startRecording(action) {
        recording = action; captured = ""; conflict = ({})
        app.capturingKeys = true
        recorder.forceActiveFocus()
    }
    function stopRecording() {
        recording = ""; captured = ""; conflict = ({})
        app.capturingKeys = false
    }
    function commit() {
        if (recording !== "" && captured !== "") app.addBinding(recording, captured)
        stopRecording()
    }

    // receives every key while recording (the workspace bindings are paused by app.capturingKeys)
    Item {
        id: recorder
        width: 0; height: 0
        Keys.onPressed: (e) => {
            e.accepted = true
            if (root.recording === "") return
            if (e.key === Qt.Key_Escape && e.modifiers === Qt.NoModifier) { root.stopRecording(); return }
            if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && e.modifiers === Qt.NoModifier && root.captured !== "") { root.commit(); return }
            const k = app.keysFor(e.key, e.modifiers)
            if (k === "") return
            root.captured = k
            const owner = app.bindingOwner(k)
            root.conflict = owner.action && owner.action !== root.recording ? owner : ({})
            if (!owner.action) root.commit()                       // free: take it at once
            else if (owner.action === root.recording) root.stopRecording()   // already this action's
        }
        onActiveFocusChanged: if (!activeFocus && root.recording !== "") root.stopRecording()
    }

    Row {
        width: root.width
        spacing: 10
        bottomPadding: 10
        Field {
            id: search
            width: root.width - resetAll.width - 10
            placeholder: "Search by action or keys (e.g. split, ctrl+shift+d)"
            onValueChanged: root.query = value
        }
        Button { id: resetAll; label: "Reset all"; onClicked: { root.stopRecording(); app.resetAllBindings() } }
    }

    Text {
        visible: root.keymap.filter(e => root.matches(e)).length === 0
        text: "No shortcut matches “" + root.query + "”"
        color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 1
        topPadding: 8
    }

    Repeater {
        model: root.keymap
        delegate: Item {
            id: row
            required property var modelData
            readonly property bool isRecording: root.recording === modelData.action
            visible: root.matches(modelData)
            width: root.width
            height: visible ? (isRecording ? band + root.fs + 22 : band) : 0

            HoverHandler { id: hover }

            // the shortcut line (two text lines while recording); the recording strip goes below it
            readonly property real band: isRecording ? root.fs * 2 + 14 : root.fs + 18
            Column {
                x: 0; y: (row.band - height) / 2
                width: parent.width * 0.45
                spacing: 2
                Text {
                    text: modelData.desc + (modelData.changed ? "  •" : "")
                    color: theme.fg; font.family: theme.fontFamily; font.pixelSize: root.fs - 1
                    elide: Text.ElideRight; width: parent.width
                }
                Text {
                    visible: hover.hovered || row.isRecording
                    text: modelData.action + (modelData.changed ? "   default: " + (modelData.defaults.length ? modelData.defaults.join("  ") : "none") : "")
                    color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 3
                }
            }

            Row {
                id: right
                anchors.right: parent.right
                y: (row.band - height) / 2
                spacing: 8
                layoutDirection: Qt.RightToLeft

                // record a new shortcut: always the rightmost control, so it never moves under the pointer
                IconButton {
                    visible: modelData.editable
                    opacity: row.isRecording ? 0 : 1
                    enabled: !row.isRecording
                    anchors.verticalCenter: parent.verticalCenter
                    size: 12; icon: "plus"
                    onClicked: root.startRecording(modelData.action)
                }
                // reset to the default; its place is kept when hidden so nothing shifts
                IconButton {
                    visible: modelData.editable
                    opacity: modelData.changed && !row.isRecording ? 1 : 0
                    enabled: modelData.changed && !row.isRecording
                    anchors.verticalCenter: parent.verticalCenter
                    size: 12; icon: "back"
                    onClicked: app.resetBinding(modelData.action)
                }
                Repeater {
                    model: modelData.keys
                    delegate: Row {
                        required property string modelData
                        spacing: 2
                        anchors.verticalCenter: parent.verticalCenter
                        Kbd { keys: modelData; anchors.verticalCenter: parent.verticalCenter }
                        IconButton {
                            visible: row.modelData.editable && hover.hovered && !row.isRecording
                            anchors.verticalCenter: parent.verticalCenter
                            size: 10; icon: "close"
                            onClicked: app.removeBinding(row.modelData.action, modelData)
                        }
                    }
                }
                Text {
                    visible: modelData.keys.length === 0 && !row.isRecording
                    anchors.verticalCenter: parent.verticalCenter
                    text: "unbound"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                }
            }

            // recording strip
            Rectangle {
                visible: row.isRecording
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: 4 }
                height: root.fs + 14
                radius: 4
                color: theme.panel
                border.width: 1; border.color: root.conflict.action ? theme.red : theme.accent
                Row {
                    anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                    spacing: 10
                    Kbd { visible: root.captured !== ""; keys: root.captured; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.captured === "" ? "Press the new shortcut…   Esc cancels"
                              : root.conflict.action ? "already used by “" + root.conflict.desc + "”.   Enter replaces it, Esc cancels" : ""
                        color: root.conflict.action ? theme.red : theme.fg
                        font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                    }
                }
            }

            Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: theme.border; opacity: 0.4 }
        }
    }
}
