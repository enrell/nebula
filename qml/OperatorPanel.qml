import QtQuick

// The operator as a real chat window: connection + model controls on top, message list, composer at the bottom.
FocusScope {
    id: root
    readonly property int fs: Math.round(theme.fontSize * 1.33)
    readonly property string context: app.overlayData.context || ""
    property bool contextSent: false
    readonly property string conn: operatorAgent.connection
    readonly property bool busy: operatorAgent.busy
    readonly property var log: operatorAgent.transcript

    readonly property var suggestions: [
        "Which agents are blocked? Approve the first one.",
        "Launch claude and codex on the flaky test, each in a worktree",
        "Summarize what every agent did so far",
        "Split this pane and open a shell next to it"
    ]

    function connColor() {
        return conn === "ready" ? theme.green : conn === "connecting" ? theme.yellow : conn === "error" ? theme.red : theme.muted
    }
    function connText() {
        if (!operatorAgent.available) return "No agent installed"
        if (conn === "ready") return "Connected · " + operatorAgent.agentName
        if (conn === "connecting") return operatorAgent.status || "Connecting…"
        if (conn === "error") return "Connection failed"
        return "Not connected"
    }
    function modelName() {
        const cur = operatorAgent.currentModel
        for (const m of operatorAgent.models) if (m.value === cur) return m.name
        return cur
    }
    function lastUser() {
        for (let i = log.length - 1; i >= 0; --i) if (log[i].kind === "user") return log[i].text
        return ""
    }
    function submit(t) {
        t = (t || "").trim()
        if (t === "" || busy) return
        operatorAgent.send(t, contextSent ? "" : context)
        contextSent = true
        edit.text = ""
    }
    function newChat() {
        operatorAgent.reset()
        contextSent = true
        Qt.callLater(() => { operatorAgent.connectNow(); edit.forceActiveFocus() })
    }
    function toBottom() { Qt.callLater(() => msgs.positionViewAtEnd()) }

    Connections { target: operatorAgent; function onTranscriptChanged() { root.toBottom() } }
    Component.onCompleted: {
        edit.forceActiveFocus()
        if (operatorAgent.available && conn === "off") operatorAgent.connectNow()
        toBottom()
    }

    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.72) }
    MouseArea { anchors.fill: parent; onClicked: app.hideOverlay() }

    Rectangle {
        id: box
        anchors.centerIn: parent
        width: Math.min(root.width - 48, 860)
        height: Math.min(root.height - 48, 720)
        radius: 12
        color: theme.bg
        border.width: 1
        border.color: theme.border
        MouseArea { anchors.fill: parent }

        // ── header ────────────────────────────────────────────
        Item {
            id: header
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 56
            Rectangle {
                id: avatar
                x: 18; anchors.verticalCenter: parent.verticalCenter
                width: 32; height: 32; radius: 16
                color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.18)
                border.width: 1; border.color: theme.accent
                Icon { anchors.centerIn: parent; name: "operator"; size: 16; color: theme.accent }
            }
            Column {
                anchors { left: avatar.right; leftMargin: 12; verticalCenter: parent.verticalCenter }
                spacing: 2
                Text { text: "Operator"; color: theme.fg; font.bold: true; font.family: theme.fontFamily; font.pixelSize: root.fs + 1 }
                Row {
                    spacing: 6
                    Rectangle {
                        id: dot
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8; height: 8; radius: 4; color: root.connColor()
                        SequentialAnimation on opacity {
                            running: root.conn === "connecting"; loops: Animation.Infinite
                            NumberAnimation { to: 0.25; duration: 500 }
                            NumberAnimation { to: 1; duration: 500 }
                            onRunningChanged: if (!running) dot.opacity = 1
                        }
                    }
                    Text { text: root.connText(); color: root.connColor(); font.family: theme.fontFamily; font.pixelSize: root.fs - 2 }
                }
            }
            Row {
                anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                spacing: 6
                Button {
                    visible: root.conn === "off" || root.conn === "error"
                    enabled: operatorAgent.available
                    label: root.conn === "error" ? "Retry" : "Connect"
                    primary: true
                    onClicked: operatorAgent.connectNow()
                }
                Button { label: "New chat"; visible: root.log.length > 0; onClicked: root.newChat() }
                IconButton { anchors.verticalCenter: parent.verticalCenter; icon: "close"; size: 12; onClicked: app.hideOverlay() }
            }
            Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: theme.border }
        }

        // ── agent / model bar ─────────────────────────────────
        Item {
            id: bar
            anchors { left: parent.left; right: parent.right; top: header.bottom }
            height: 46
            z: 5
            Row {
                x: 18; anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Dropdown {
                    scope: box
                    prefix: "Agent"
                    width: 260
                    text: operatorAgent.agentName
                    placeholder: "none"
                    items: operatorAgent.agentPresets().map(p => ({ label: p.label, hint: p.available ? p.hint : "not installed", value: p.command, enabled: p.available, name: p.label }))
                    onPicked: (cmd) => {
                        settings.operatorAgent = cmd
                        operatorAgent.reset()
                        root.contextSent = true
                        Qt.callLater(() => operatorAgent.connectNow())
                    }
                    currentValue: { const p = operatorAgent.agentPresets().find(p => p.label === operatorAgent.agentName); return p ? p.command : "" }
                }
                Dropdown {
                    scope: box
                    prefix: "Model"
                    width: 280
                    enabled: !root.busy && operatorAgent.models.length > 0
                    text: operatorAgent.models.length > 0 ? root.modelName() : ""
                    placeholder: root.conn === "ready" ? "agent default" : "connect to choose"
                    currentValue: operatorAgent.currentModel
                    items: operatorAgent.models.map(m => ({ label: m.name, hint: m.description, value: m.value }))
                    onPicked: (v) => operatorAgent.setModel(v)
                }
            }
            Rectangle {
                anchors { right: parent.right; rightMargin: 18; verticalCenter: parent.verticalCenter }
                height: root.fs + 8
                width: yoloText.implicitWidth + 20
                radius: height / 2
                color: settings.operatorApproveAll ? Qt.rgba(theme.yellow.r, theme.yellow.g, theme.yellow.b, 0.16) : "transparent"
                border.width: 1
                border.color: settings.operatorApproveAll ? theme.yellow : theme.border
                Text {
                    id: yoloText
                    anchors.centerIn: parent
                    text: settings.operatorApproveAll ? "auto-approve on" : "asks before acting"
                    color: settings.operatorApproveAll ? theme.yellow : theme.muted
                    font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: settings.operatorApproveAll = !settings.operatorApproveAll }
            }
            Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: theme.border; opacity: 0.6 }
        }

        // ── messages ──────────────────────────────────────────
        ListView {
            id: msgs
            anchors { left: parent.left; right: parent.right; top: bar.bottom; bottom: composer.top }
            clip: true
            topMargin: 16; bottomMargin: 8
            spacing: 12
            model: root.log
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 4000

            delegate: Item {
                id: msg
                required property var modelData
                required property int index
                readonly property string kind: modelData.kind
                width: msgs.width
                height: col.implicitHeight

                Column {
                    id: col
                    width: parent.width

                    // user: right-aligned bubble
                    Item {
                        visible: msg.kind === "user"
                        width: parent.width; height: visible ? ub.height : 0
                        Rectangle {
                            id: ub
                            anchors { right: parent.right; rightMargin: 20 }
                            width: Math.min(msgs.width * 0.78, uText.implicitWidth + 28)
                            height: uText.implicitHeight + 20
                            radius: 12
                            color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.20)
                            border.width: 1; border.color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.5)
                            TextEdit {
                                id: uText
                                x: 14; y: 10; width: parent.width - 28
                                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                                text: msg.kind === "user" ? msg.modelData.text : ""
                                color: theme.fg; selectionColor: theme.accent; selectedTextColor: theme.bg
                                font.family: theme.fontFamily; font.pixelSize: root.fs
                            }
                        }
                    }

                    // assistant: avatar + markdown bubble
                    Item {
                        visible: msg.kind === "assistant"
                        width: parent.width; height: visible ? ab.height : 0
                        Rectangle {
                            x: 20; width: 26; height: 26; radius: 13
                            color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.18)
                            Icon { anchors.centerIn: parent; name: "operator"; size: 13; color: theme.accent }
                        }
                        Rectangle {
                            id: ab
                            x: 56
                            width: Math.min(msgs.width - 56 - 20, aText.implicitWidth + 28)
                            height: aText.implicitHeight + 20
                            radius: 12
                            color: theme.panel
                            border.width: 1; border.color: theme.border
                            TextEdit {
                                id: aText
                                x: 14; y: 10; width: msgs.width - 56 - 20 - 28
                                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                                textFormat: TextEdit.MarkdownText
                                text: msg.kind === "assistant" ? msg.modelData.text : ""
                                color: theme.fg; selectionColor: theme.accent; selectedTextColor: theme.bg
                                font.family: theme.fontFamily; font.pixelSize: root.fs
                                onLinkActivated: (l) => Qt.openUrlExternally(l)
                            }
                        }
                    }

                    // thinking
                    Item {
                        visible: msg.kind === "thought"
                        width: parent.width; height: visible ? tText.implicitHeight : 0
                        Text {
                            id: tText
                            x: 56; width: parent.width - 76
                            text: msg.kind === "thought" ? "thinking · " + msg.modelData.text.replace(/\*+/g, "").replace(/\s+/g, " ") : ""
                            maximumLineCount: 2; elide: Text.ElideRight; wrapMode: Text.WordWrap
                            color: theme.muted; font.italic: true; opacity: 0.8
                            font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                        }
                    }

                    // tool call chip: what it is, what it was given, how it went
                    Item {
                        visible: msg.kind === "tool"
                        width: parent.width; height: visible ? chip.height : 0
                        Rectangle {
                            id: chip
                            readonly property string st: msg.kind === "tool" ? (msg.modelData.status || "") : ""
                            readonly property bool running: st === "" || st === "in_progress" || st === "pending"
                            readonly property color tone: st === "completed" ? theme.green : (st === "failed" || st === "stopped") ? theme.red : theme.yellow
                            x: 56
                            width: Math.min(parent.width - 76, cRow.implicitWidth + 24)
                            height: root.fs + 12
                            radius: height / 2
                            color: theme.panel
                            border.width: 1; border.color: theme.border
                            Row {
                                id: cRow
                                x: 12; anchors.verticalCenter: parent.verticalCenter
                                spacing: 8
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 7; height: 7; radius: 4; color: chip.tone
                                    SequentialAnimation on opacity {
                                        running: chip.running && root.busy; loops: Animation.Infinite
                                        NumberAnimation { to: 0.25; duration: 450 }
                                        NumberAnimation { to: 1; duration: 450 }
                                        onRunningChanged: if (!running) parent.opacity = 1
                                    }
                                }
                                Text {
                                    id: chipTitle
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: msg.kind === "tool" ? msg.modelData.text : ""
                                    color: theme.fg; font.family: theme.fontFamily; font.pixelSize: root.fs - 2; font.bold: true
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: text !== ""
                                    width: Math.min(implicitWidth, box.width - 160 - chipTitle.implicitWidth - chipStatus.implicitWidth)
                                    elide: Text.ElideRight
                                    text: msg.kind === "tool" ? (msg.modelData.detail || "") : ""
                                    color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                                }
                                Text {
                                    id: chipStatus
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: chip.st !== ""
                                    text: chip.st === "completed" ? "✓" : chip.st === "failed" ? "✗ failed" : chip.st === "stopped" ? "■ stopped" : chip.st.replace("_", " ")
                                    color: chip.tone; font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                                }
                            }
                        }
                    }

                    // error card
                    Item {
                        visible: msg.kind === "error"
                        width: parent.width; height: visible ? ec.height : 0
                        Rectangle {
                            id: ec
                            x: 20; width: parent.width - 40
                            height: eCol.implicitHeight + 24
                            radius: 10
                            color: Qt.rgba(theme.red.r, theme.red.g, theme.red.b, 0.10)
                            border.width: 1; border.color: Qt.rgba(theme.red.r, theme.red.g, theme.red.b, 0.6)
                            Column {
                                id: eCol
                                x: 14; y: 12; width: parent.width - 28
                                spacing: 8
                                Text {
                                    width: parent.width; wrapMode: Text.WrapAnywhere
                                    text: msg.kind === "error" ? msg.modelData.text : ""
                                    color: theme.red; font.family: theme.fontFamily; font.pixelSize: root.fs - 1
                                }
                                Button { label: "Retry"; visible: msg.index === root.log.length - 1 && root.lastUser() !== "" && !root.busy; onClicked: operatorAgent.send(root.lastUser()) }
                            }
                        }
                    }
                }
            }

            footer: Item {
                width: msgs.width
                height: (root.busy ? 34 : 0) + (confirmCard.visible ? confirmCard.height + 12 : 0)
                Row {
                    x: 56; y: 6
                    visible: root.busy && operatorAgent.pendingConfirm.tool === undefined
                    spacing: 10
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Repeater {
                            model: 3
                            delegate: Rectangle {
                                required property int index
                                width: 7; height: 7; radius: 4; color: theme.accent
                                SequentialAnimation on opacity {
                                    running: root.busy; loops: Animation.Infinite
                                    PauseAnimation { duration: index * 150 }
                                    NumberAnimation { to: 0.2; duration: 350 }
                                    NumberAnimation { to: 1; duration: 350 }
                                    PauseAnimation { duration: (2 - index) * 150 }
                                }
                            }
                        }
                    }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: operatorAgent.status || "working…"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2 }
                }
                Rectangle {
                    id: confirmCard
                    visible: operatorAgent.pendingConfirm.tool !== undefined
                    x: 20; y: 4; width: parent.width - 40
                    height: cc.implicitHeight + 24
                    radius: 10
                    color: Qt.rgba(theme.yellow.r, theme.yellow.g, theme.yellow.b, 0.10)
                    border.width: 1; border.color: theme.yellow
                    Column {
                        id: cc
                        x: 14; y: 12; width: parent.width - 28; spacing: 10
                        Text {
                            width: parent.width; wrapMode: Text.WrapAnywhere
                            text: "The operator wants to: " + (operatorAgent.pendingConfirm.summary || "")
                            color: theme.yellow; font.family: theme.fontFamily; font.pixelSize: root.fs - 1
                        }
                        Row {
                            spacing: 8
                            Button { label: "Allow"; primary: true; onClicked: operatorAgent.confirm(true) }
                            Button { label: "Deny"; danger: true; onClicked: operatorAgent.confirm(false) }
                        }
                    }
                }
            }
        }

        // empty state
        Column {
            visible: root.log.length === 0
            anchors { horizontalCenter: msgs.horizontalCenter; verticalCenter: msgs.verticalCenter }
            width: Math.min(msgs.width - 48, 560)
            spacing: 14
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 52; height: 52; radius: 26
                color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.15)
                border.width: 1; border.color: theme.accent
                Icon { anchors.centerIn: parent; name: "operator"; size: 26; color: theme.accent }
            }
            Text {
                width: parent.width; horizontalAlignment: Text.AlignHCenter
                text: "What should I do?"; color: theme.fg; font.bold: true
                font.family: theme.fontFamily; font.pixelSize: root.fs + 5
            }
            Text {
                width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                text: !operatorAgent.available
                    ? "No compatible agent is installed. Install Codex, Claude Code or opencode, or set a custom command in Settings › Agents & AI."
                    : "I run nebula for you: I can launch and prompt your coding agents, read their output, answer their prompts and rearrange panes."
                color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 1
            }
            Column {
                width: parent.width; spacing: 8
                visible: operatorAgent.available
                Repeater {
                    model: root.suggestions
                    delegate: Rectangle {
                        required property string modelData
                        width: parent.width; height: root.fs + 18; radius: 8
                        color: sma.containsMouse ? theme.panel : "transparent"
                        border.width: 1; border.color: sma.containsMouse ? theme.accent : theme.border
                        Text { x: 14; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 28; elide: Text.ElideRight; text: modelData; color: theme.fg; font.family: theme.fontFamily; font.pixelSize: root.fs - 1 }
                        MouseArea { id: sma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.submit(modelData) }
                    }
                }
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !operatorAgent.available
                label: "Open settings"; primary: true
                onClicked: { app.hideOverlay(); app.setSettingsVisible(true) }
            }
        }

        // ── composer ──────────────────────────────────────────
        Item {
            id: composer
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: ctxChip.height + inputBox.height + hint.height + 30

            Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: theme.border }

            Rectangle {
                id: ctxChip
                visible: root.context !== "" && !root.contextSent
                x: 18; y: 10; width: parent.width - 36
                height: visible ? root.fs + 14 : 0
                radius: 8
                color: theme.panel; border.width: 1; border.color: theme.border
                Text {
                    x: 12; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 48; elide: Text.ElideRight
                    text: "Selected text attached · " + root.context.split("\n").filter(l => l.trim() !== "")[0]
                    color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                }
                IconButton { anchors { right: parent.right; rightMargin: 4; verticalCenter: parent.verticalCenter } icon: "close"; size: 10; onClicked: root.contextSent = true }
            }

            Rectangle {
                id: inputBox
                x: 18; y: ctxChip.height + (ctxChip.visible ? 18 : 10)
                width: parent.width - 36
                height: Math.max(46, Math.min(150, edit.contentHeight + 24))
                radius: 12
                color: theme.panel
                border.width: 1
                border.color: edit.activeFocus ? theme.accent : theme.border

                Flickable {
                    id: fl
                    anchors { left: parent.left; right: sendBtn.left; top: parent.top; bottom: parent.bottom; leftMargin: 14; rightMargin: 8; topMargin: 12; bottomMargin: 12 }
                    clip: true
                    contentWidth: width
                    contentHeight: edit.contentHeight
                    boundsBehavior: Flickable.StopAtBounds
                    function ensureVisible(r) {
                        if (contentY >= r.y) contentY = r.y
                        else if (contentY + height <= r.y + r.height) contentY = r.y + r.height - height
                    }
                    TextEdit {
                        id: edit
                        width: fl.width
                        wrapMode: TextEdit.Wrap
                        color: theme.fg
                        selectionColor: theme.accent; selectedTextColor: theme.bg
                        font.family: theme.fontFamily; font.pixelSize: root.fs
                        onCursorRectangleChanged: fl.ensureVisible(cursorRectangle)
                        Keys.onPressed: (e) => {
                            if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && !(e.modifiers & Qt.ShiftModifier)) {
                                root.submit(text)
                                e.accepted = true
                            }
                        }
                        Text {
                            visible: edit.text === ""
                            text: root.conn === "ready" ? "Message the operator…" : "Message the operator (it connects on send)…"
                            color: theme.muted
                            font: edit.font
                        }
                    }
                }

                Rectangle {
                    id: sendBtn
                    readonly property bool canSend: root.busy || edit.text.trim() !== ""
                    anchors { right: parent.right; rightMargin: 8; bottom: parent.bottom; bottomMargin: 8 }
                    width: 30; height: 30; radius: 15
                    color: root.busy ? theme.red : theme.accent
                    opacity: canSend ? 1 : 0.35
                    Text {
                        anchors.centerIn: parent
                        text: root.busy ? "■" : "↑"
                        color: theme.bg; font.bold: true; font.pixelSize: root.busy ? root.fs - 2 : root.fs + 2
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: sendBtn.canSend
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.busy ? operatorAgent.cancel() : root.submit(edit.text)
                    }
                }
            }

            Text {
                id: hint
                x: 20; y: inputBox.y + inputBox.height + 6
                text: "Enter to send  ·  Shift+Enter for a new line  ·  Esc to close"
                color: theme.muted; opacity: 0.8
                font.family: theme.fontFamily; font.pixelSize: root.fs - 3
            }
        }
    }
}
