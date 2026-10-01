import QtQuick

// The operator: a keyboard-first transcript over the workspace, in the same language as the rest of nebula
// (the view modal's frame and header, pane-title typography, the terminal's prompt). One header line carries the
// agent, model, connection and approval mode; messages read like a terminal session, not chat bubbles.
FocusScope {
    id: root
    readonly property int fs: Math.round(theme.fontSize * 1.33)
    readonly property string context: app.overlayData.context || ""
    property bool contextSent: false
    readonly property string conn: operatorAgent.connection
    readonly property bool busy: operatorAgent.busy
    readonly property var log: operatorAgent.transcript
    readonly property bool asking: operatorAgent.pendingConfirm.tool !== undefined
    readonly property int pad: 18   // left edge of every transcript line

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
        if (!operatorAgent.available) return "no agent installed"
        if (conn === "ready") return "ready"
        if (conn === "connecting") return (operatorAgent.status || "connecting").toLowerCase().replace(/…$/, "") + "…"
        if (conn === "error") return "connection failed"
        return "idle"
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
    // "Claude Code (subscription sign-in)" -> "claude code": the header is a status line, not a title
    function brief(s) { return (s || "").replace(/\s*\(.*\)\s*$/, "").toLowerCase() }

    Connections { target: operatorAgent; function onTranscriptChanged() { root.toBottom() } }
    Component.onCompleted: {
        edit.forceActiveFocus()
        if (operatorAgent.available && conn === "off") operatorAgent.connectNow()
        toBottom()
    }

    // a braille spinner, like a terminal program's
    property int spin: 0
    readonly property var frames: ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]
    Timer { interval: 80; repeat: true; running: root.busy || root.conn === "connecting"; onTriggered: root.spin = (root.spin + 1) % root.frames.length }

    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.72) }
    MouseArea { anchors.fill: parent; onClicked: app.hideOverlay() }

    // a stable chat window: centered, the same size whatever the conversation holds
    Rectangle {
        id: box
        anchors.centerIn: parent
        width: Math.min(root.width - 48, 860)
        height: Math.min(root.height - 48, 700)
        radius: 6
        color: theme.bg
        border.width: 1
        border.color: theme.accent
        MouseArea { anchors.fill: parent }

        component HeaderButton: Rectangle {
            property string label
            property color tone: theme.muted
            signal clicked
            width: hbText.implicitWidth + 14; height: root.fs + 8; radius: 4
            color: hbHover.hovered ? theme.panel : "transparent"
            Text { id: hbText; anchors.centerIn: parent; text: parent.label; color: hbHover.hovered ? theme.fg : parent.tone; font.family: theme.fontFamily; font.pixelSize: root.fs - 2 }
            HoverHandler { id: hbHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: parent.clicked() }
        }

        // ── one header line: name · agent · model · state            approvals  new  close ──
        Item {
            id: header
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: root.fs + 22
            z: 5
            Row {
                anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                spacing: 2
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "operator"; color: theme.accent; font.bold: true
                    font.family: theme.fontFamily; font.pixelSize: root.fs + 1
                    rightPadding: 8
                }
                Dropdown {
                    anchors.verticalCenter: parent.verticalCenter
                    flat: true; scope: box
                    width: Math.min(implicitWidth, 220)
                    text: root.brief(operatorAgent.agentName)
                    placeholder: "no agent"
                    items: operatorAgent.agentPresets().map(p => ({ label: p.label, hint: p.available ? p.hint : "not installed", value: p.command, enabled: p.available }))
                    currentValue: { const p = operatorAgent.agentPresets().find(p => p.label === operatorAgent.agentName); return p ? p.command : "" }
                    onPicked: (cmd) => {
                        settings.operatorAgent = cmd
                        operatorAgent.reset()
                        root.contextSent = true
                        Qt.callLater(() => operatorAgent.connectNow())
                    }
                }
                Dropdown {
                    anchors.verticalCenter: parent.verticalCenter
                    flat: true; scope: box
                    width: Math.min(implicitWidth, 220)
                    enabled: !root.busy && operatorAgent.models.length > 0
                    text: operatorAgent.models.length > 0 ? root.brief(root.modelName()) : ""
                    placeholder: root.conn === "ready" ? "default model" : "model"
                    currentValue: operatorAgent.currentModel
                    items: operatorAgent.models.map(m => ({ label: m.name, hint: m.description, value: m.value }))
                    onPicked: (v) => operatorAgent.setModel(v)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: 6
                    text: (root.conn === "connecting" ? root.frames[root.spin] : "●") + " " + root.connText()
                    color: root.connColor(); font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                }
            }
            Row {
                anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                spacing: 2
                HeaderButton {
                    visible: root.conn === "off" || root.conn === "error"
                    label: root.conn === "error" ? "retry" : "connect"; tone: theme.accent
                    onClicked: operatorAgent.connectNow()
                }
                HeaderButton {
                    label: settings.operatorApproveAll ? "auto-approve" : "asks first"
                    tone: settings.operatorApproveAll ? theme.yellow : theme.muted
                    onClicked: settings.operatorApproveAll = !settings.operatorApproveAll
                }
                HeaderButton { visible: root.log.length > 0; label: "new"; onClicked: root.newChat() }
                HeaderButton { label: "close  esc"; onClicked: app.hideOverlay() }
            }
            Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: theme.border }
        }

        // ── transcript ──
        ListView {
            id: msgs
            anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: composer.top; leftMargin: 1; rightMargin: 1 }
            clip: true
            topMargin: 14; bottomMargin: 6
            spacing: 10
            model: root.log
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 4000

            delegate: Item {
                id: msg
                required property var modelData
                required property int index
                readonly property string kind: modelData.kind
                // a user turn after an answer starts a new exchange: a little more air above it
                readonly property bool newTurn: kind === "user" && index > 0
                width: msgs.width
                height: col.implicitHeight + (newTurn ? 8 : 0)

                Column {
                    id: col
                    y: msg.newTurn ? 8 : 0
                    width: parent.width

                    // you: a bubble on the right
                    Item {
                        visible: msg.kind === "user"
                        width: parent.width; height: visible ? ub.height : 0
                        Rectangle {
                            id: ub
                            anchors { right: parent.right; rightMargin: root.pad }
                            width: Math.min(msgs.width * 0.75, uText.implicitWidth + 24)
                            height: uText.implicitHeight + 16
                            radius: 6
                            color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.16)
                            border.width: 1; border.color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.45)
                            TextEdit {
                                id: uText
                                x: 12; y: 8; width: Math.min(implicitWidth, msgs.width * 0.75 - 24)
                                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                                text: msg.kind === "user" ? msg.modelData.text : ""
                                color: theme.fg; selectionColor: theme.accent; selectedTextColor: theme.bg
                                font.family: theme.fontFamily; font.pixelSize: root.fs
                            }
                        }
                    }

                    // the operator: a bubble on the left
                    Item {
                        visible: msg.kind === "assistant"
                        width: parent.width; height: visible ? ab.height : 0
                        Rectangle {
                            id: ab
                            x: root.pad
                            width: Math.min(msgs.width * 0.82, aText.implicitWidth + 24)
                            height: aText.implicitHeight + 16
                            radius: 6
                            color: theme.panel
                            border.width: 1; border.color: theme.border
                            TextEdit {
                                id: aText
                                x: 12; y: 8; width: Math.min(implicitWidth, msgs.width * 0.82 - 24)
                                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                                textFormat: TextEdit.MarkdownText
                                text: msg.kind === "assistant" ? msg.modelData.text : ""
                                color: theme.fg; selectionColor: theme.accent; selectedTextColor: theme.bg
                                font.family: theme.fontFamily; font.pixelSize: root.fs
                                onLinkActivated: (l) => Qt.openUrlExternally(l)
                            }
                        }
                    }

                    // thinking: one dim line
                    Text {
                        visible: msg.kind === "thought"
                        x: root.pad + 4; width: msgs.width - root.pad * 2 - 4
                        text: msg.kind === "thought" ? "∴ " + msg.modelData.text.replace(/\*+/g, "").replace(/\s+/g, " ") : ""
                        maximumLineCount: 1; elide: Text.ElideRight
                        color: theme.muted; font.italic: true
                        font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                    }

                    // a tool call: one line, status mark first
                    Row {
                        id: tool
                        readonly property string st: msg.kind === "tool" ? (msg.modelData.status || "") : ""
                        readonly property bool running: st === "" || st === "in_progress" || st === "pending"
                        readonly property color tone: st === "completed" ? theme.green : (st === "failed" || st === "stopped") ? theme.red : theme.yellow
                        visible: msg.kind === "tool"
                        x: root.pad + 4; spacing: 8
                        Text {
                            text: tool.running && root.busy ? root.frames[root.spin] : tool.st === "completed" ? "✓" : tool.st === "failed" ? "✗" : tool.st === "stopped" ? "■" : "·"
                            color: tool.tone; font.family: theme.fontFamily; font.pixelSize: root.fs - 1
                        }
                        Text {
                            id: toolName
                            text: msg.kind === "tool" ? msg.modelData.text.replace(/^nebula\./, "") : ""
                            color: theme.fg; font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                        }
                        Text {
                            visible: text !== ""
                            width: Math.min(implicitWidth, msgs.width - root.pad * 2 - 60 - toolName.implicitWidth - toolState.implicitWidth)
                            elide: Text.ElideRight
                            text: msg.kind === "tool" ? (msg.modelData.detail || "") : ""
                            color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                        }
                        Text {
                            id: toolState
                            visible: tool.st === "failed" || tool.st === "stopped"
                            text: tool.st
                            color: tool.tone; font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                        }
                    }

                    // restored history and other notes
                    Text {
                        visible: msg.kind === "info"
                        x: root.pad; width: msgs.width - root.pad * 2
                        wrapMode: Text.WordWrap
                        text: msg.kind === "info" ? "── " + msg.modelData.text : ""
                        color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                    }

                    // an error: red rule, message, keyboard-style actions
                    Item {
                        visible: msg.kind === "error"
                        x: root.pad + 4; width: msgs.width - root.pad * 2 - 4
                        height: visible ? eCol.implicitHeight : 0
                        Rectangle { width: 2; height: parent.height; color: theme.red }
                        Column {
                            id: eCol
                            x: 12; width: parent.width - 12; spacing: 4
                            Text {
                                width: parent.width; wrapMode: Text.WrapAnywhere
                                text: msg.kind === "error" ? msg.modelData.text : ""
                                color: theme.red; font.family: theme.fontFamily; font.pixelSize: root.fs - 1
                            }
                            Row {
                                spacing: 2
                                HeaderButton {
                                    visible: msg.index === root.log.length - 1 && root.lastUser() !== "" && !root.busy
                                    label: "retry"; tone: theme.accent
                                    onClicked: operatorAgent.send(root.lastUser())
                                }
                                HeaderButton {
                                    property bool copied: false
                                    label: copied ? "copied" : "copy log"
                                    onClicked: { app.copyToClipboard(operatorAgent.diagnostics()); copied = true }
                                }
                            }
                        }
                    }
                }
            }

            footer: Item {
                width: msgs.width
                height: (root.busy && !root.asking ? root.fs + 14 : 0) + (root.asking ? ask.height + 14 : 0)
                // working
                Text {
                    visible: root.busy && !root.asking
                    x: root.pad + 4; y: 8
                    text: root.frames[root.spin] + " " + (operatorAgent.status || "working").toLowerCase()
                    color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                }
                // an approval, answered with y / n or a click
                Item {
                    id: ask
                    visible: root.asking
                    x: root.pad + 4; y: 8; width: parent.width - root.pad * 2 - 4
                    height: aCol.implicitHeight
                    Rectangle { width: 2; height: parent.height; color: theme.yellow }
                    Column {
                        id: aCol
                        x: 12; width: parent.width - 12; spacing: 6
                        Text {
                            width: parent.width; wrapMode: Text.WrapAnywhere
                            text: "allow " + (operatorAgent.pendingConfirm.summary || "").replace(/^nebula\./, "") + "?"
                            color: theme.yellow; font.family: theme.fontFamily; font.pixelSize: root.fs - 1
                        }
                        Row {
                            spacing: 2
                            HeaderButton { label: "y  allow"; tone: theme.green; onClicked: operatorAgent.confirm(true) }
                            HeaderButton { label: "n  deny"; tone: theme.red; onClicked: operatorAgent.confirm(false) }
                        }
                    }
                }
            }
        }

        // ── empty: a centered welcome with a few things to try ──
        Column {
            id: empty
            visible: root.log.length === 0
            anchors { horizontalCenter: msgs.horizontalCenter; verticalCenter: msgs.verticalCenter }
            width: Math.min(msgs.width - root.pad * 2, 560)
            spacing: 10
            Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "operator"; size: 22; color: theme.accent }
            Text {
                width: parent.width; horizontalAlignment: Text.AlignHCenter
                text: "What should I do?"; color: theme.fg; font.bold: true
                font.family: theme.fontFamily; font.pixelSize: root.fs + 3
            }
            Text {
                width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                text: !operatorAgent.available
                    ? "No compatible agent is installed. Install Codex, Claude Code or opencode, or set a command in Settings › Agents & AI."
                    : "I run nebula for you: I launch and prompt your agents, read their panes, answer their prompts and arrange the workspace."
                color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 1
                bottomPadding: 6
            }
            Repeater {
                model: operatorAgent.available ? root.suggestions : []
                delegate: Rectangle {
                    required property string modelData
                    width: parent.width; height: root.fs + 16; radius: 6
                    color: sHover.hovered ? theme.panel : "transparent"
                    border.width: 1; border.color: sHover.hovered ? theme.accent : theme.border
                    Text {
                        x: 12; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 24; elide: Text.ElideRight
                        text: modelData; color: sHover.hovered ? theme.fg : theme.muted
                        font.family: theme.fontFamily; font.pixelSize: root.fs - 1
                    }
                    HoverHandler { id: sHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.submit(modelData) }
                }
            }
            HeaderButton {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !operatorAgent.available
                label: "open settings"; tone: theme.accent
                onClicked: { app.hideOverlay(); app.setSettingsVisible(true) }
            }
        }

        // ── composer: a prompt line ──
        Item {
            id: composer
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: ctx.height + inputRow.height + hint.height + 22

            Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: theme.border }

            Row {
                id: ctx
                visible: root.context !== "" && !root.contextSent
                x: root.pad; y: 8
                height: visible ? root.fs + 6 : 0
                spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, box.width - root.pad * 2 - 60)
                    elide: Text.ElideRight
                    text: "+ selection · " + root.context.split("\n").filter(l => l.trim() !== "")[0]
                    color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2
                }
                HeaderButton { anchors.verticalCenter: parent.verticalCenter; label: "drop"; onClicked: root.contextSent = true }
            }

            // a chat input box: framed, accent border while typing
            Rectangle {
                id: inputRow
                x: root.pad; y: ctx.height + 12
                width: parent.width - root.pad * 2
                height: Math.max(root.fs + 22, Math.min(160, edit.contentHeight + 22))
                radius: 6
                color: theme.panel
                border.width: 1
                border.color: edit.activeFocus ? theme.accent : theme.border
                Flickable {
                    id: fl
                    anchors { fill: parent; leftMargin: 12; rightMargin: 12; topMargin: 11; bottomMargin: 11 }
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
                            const ctrl = e.modifiers & Qt.ControlModifier
                            if (root.asking && text === "" && (e.key === Qt.Key_Y || e.key === Qt.Key_N)) {
                                operatorAgent.confirm(e.key === Qt.Key_Y)
                                e.accepted = true
                            } else if (root.busy && ctrl && e.key === Qt.Key_C && selectedText === "") {
                                operatorAgent.cancel()
                                e.accepted = true
                            } else if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && !(e.modifiers & Qt.ShiftModifier)) {
                                root.submit(text)
                                e.accepted = true
                            }
                        }
                        Text {
                            visible: edit.text === ""
                            text: root.asking ? "y to allow, n to deny" : root.conn === "ready" ? "Message the operator" : "Message the operator (it connects when you send)"
                            color: theme.muted; opacity: 0.8
                            font: edit.font
                        }
                    }
                }
            }

            Text {
                id: hint
                x: root.pad; y: inputRow.y + inputRow.height + 6
                width: parent.width - root.pad * 2
                horizontalAlignment: Text.AlignRight
                text: root.busy ? "ctrl+c stop  ·  esc close" : "enter send  ·  shift+enter newline  ·  esc close"
                color: theme.muted; opacity: 0.7
                font.family: theme.fontFamily; font.pixelSize: root.fs - 3
            }
        }
    }
}
