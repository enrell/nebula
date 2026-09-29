import QtQuick

FocusScope {
    id: root
    readonly property int fs: Math.round(theme.fontSize * 1.33)
    readonly property var steps: ["Welcome", "Look & feel", "Your agents", "API keys", "Integrations", "Operator", "All set"]
    property int step: 0
    property string keyMsg: ""
    property bool keyOk: true
    property int providerIdx: 0
    property string intErr: ""

    readonly property var agentPresets: launcher.presets.filter(p => p.id !== "shell")
    readonly property int foundCount: agentPresets.filter(p => p.available).length
    readonly property bool last: step === steps.length - 1

    function next() { if (!last) step++; else finish() }
    function back() { if (step > 0) step-- }
    function finish() { app.hideOverlay() }
    function launchFirst() { finish(); app.runAction("launch-agent") }
    Component.onCompleted: forceActiveFocus()
    Component.onDestruction: settings.onboarded = true

    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.88) }
    MouseArea { anchors.fill: parent }

    component H1: Text {
        color: theme.fg; font.bold: true; wrapMode: Text.WordWrap
        font.family: theme.fontFamily; font.pixelSize: root.fs + 6
    }
    component P: Text {
        color: theme.muted; wrapMode: Text.WordWrap
        font.family: theme.fontFamily; font.pixelSize: root.fs - 1
        lineHeight: 1.25
    }
    component Line: Row {
        property string icon
        property string title
        property string text
        width: parent ? parent.width : 0
        spacing: 12
        Text { width: 22; text: parent.icon; color: theme.accent; font.pixelSize: root.fs + 4; horizontalAlignment: Text.AlignHCenter }
        Column {
            width: parent.width - 34
            spacing: 2
            Text { text: parent.parent.title; color: theme.fg; font.bold: true; font.family: theme.fontFamily; font.pixelSize: root.fs }
            P { width: parent.width; text: parent.parent.text }
        }
    }

    Rectangle {
        id: box
        anchors.centerIn: parent
        width: Math.min(root.width - 40, 680)
        height: Math.min(root.height - 40, 500)
        radius: 10
        color: theme.bg
        border.width: 1
        border.color: theme.accent
        clip: true
        MouseArea { anchors.fill: parent }

        // progress
        Row {
            id: dots
            x: 28; y: 22
            spacing: 6
            Repeater {
                model: root.steps
                delegate: Rectangle {
                    required property int index
                    width: index === root.step ? 28 : 10; height: 6; radius: 3
                    color: index <= root.step ? theme.accent : theme.border
                    Behavior on width { NumberAnimation { duration: 120 } }
                }
            }
        }
        Text {
            anchors { right: parent.right; rightMargin: 28; verticalCenter: dots.verticalCenter }
            text: (root.step + 1) + " / " + root.steps.length + "  ·  " + root.steps[root.step]
            color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2
        }

        // ── step content ─────────────────────────────────────
        Item {
            anchors { left: parent.left; right: parent.right; top: parent.top; bottom: footer.top; leftMargin: 28; rightMargin: 28; topMargin: 56; bottomMargin: 8 }

            // 0 welcome
            Column {
                visible: root.step === 0
                width: parent.width; spacing: 16
                H1 { width: parent.width; text: "Welcome to nebula" }
                P { width: parent.width; text: "A workspace for running several terminal AI agents side by side. This 1-minute setup gets you ready. Everything can be changed later in Settings." }
                Item { width: 1; height: 4 }
                Line { icon: "▦"; title: "Spaces, tabs and panes"; text: "One space per project, split panes for each agent or shell." }
                Line { icon: "●"; title: "Live agent status"; text: "The sidebar shows which agents are working, blocked or done, and notifies you." }
                Line { icon: "◎"; title: "An operator that runs nebula for you"; text: "Ask it to launch agents, answer their prompts and reorganize your layout." }
                Line { icon: "↻"; title: "Sessions survive"; text: "Close the window and your shells and agents keep running." }
            }

            // 1 look
            Column {
                visible: root.step === 1
                width: parent.width; spacing: 6
                H1 { width: parent.width; text: "Look & feel" }
                P { width: parent.width; text: "nebula follows your Omarchy theme and terminal font automatically." }
                Item { width: 1; height: 8 }
                SettingRow {
                    label: "Font size"; description: "Auto follows your terminal config"
                    Row {
                        spacing: 8
                        Stepper { value: theme.fontSize; from: 6; to: 32; onMoved: (v) => settings.fontSize = v }
                        Button { label: "Auto"; visible: settings.fontSize > 0; onClicked: settings.fontSize = 0 }
                    }
                }
                SettingRow { label: "Desktop notifications"; description: "Tell me when an agent finishes or needs input"
                    Toggle { checked: settings.notifications; onToggled: (v) => settings.notifications = v } }
                SettingRow { label: "Copy on select"; description: "Selecting text copies it"
                    Toggle { checked: settings.copyOnSelect; onToggled: (v) => settings.copyOnSelect = v } }
                SettingRow { label: "Focus follows mouse"; description: "Hovering a pane focuses it"
                    Toggle { checked: settings.focusFollowsMouse; onToggled: (v) => settings.focusFollowsMouse = v } }
            }

            // 2 agents
            Column {
                visible: root.step === 2
                width: parent.width; spacing: 10
                H1 { width: parent.width; text: "Your agents" }
                P {
                    width: parent.width
                    text: root.foundCount > 0
                        ? "Found " + root.foundCount + " agent CLI" + (root.foundCount === 1 ? "" : "s") + " on this machine. Launch any of them with Ctrl+Shift+L."
                        : "No agent CLI found in your PATH yet. Install one (for example Claude Code or Codex), then come back with Settings › Setup wizard. A plain shell always works."
                }
                Flow {
                    width: parent.width; spacing: 8
                    Repeater {
                        model: root.agentPresets
                        delegate: Rectangle {
                            required property var modelData
                            width: (box.width - 56 - 8) / 2; height: 46; radius: 6
                            color: theme.panel; border.width: 1; border.color: modelData.available ? theme.green : theme.border
                            opacity: modelData.available ? 1 : 0.6
                            Column {
                                x: 14; anchors.verticalCenter: parent.verticalCenter; spacing: 2
                                Text { text: modelData.label; color: theme.fg; font.bold: true; font.family: theme.fontFamily; font.pixelSize: root.fs }
                                Text { text: modelData.available ? "ready" : "not installed"; color: modelData.available ? theme.green : theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 3 }
                            }
                        }
                    }
                }
                P { width: parent.width; text: "Add your own agents in ~/.config/nebula/agents.conf (one “name = command” per line)." }
            }

            // 3 api keys
            Column {
                visible: root.step === 3
                width: parent.width; spacing: 10
                H1 { width: parent.width; text: "API keys" }
                P { width: parent.width; text: "If you sign in to your agents with a subscription (Claude, ChatGPT, Gemini), skip this step. Otherwise save an API key as a profile: it is injected into agent panes and stored in your " + (profiles.backend === "keyring" ? "system keyring." : "0600 secrets file.") }
                Row {
                    spacing: 8
                    Field { id: pfName; width: 170; placeholder: "profile name"; text: "default" }
                    Choice { width: 220; model: profiles.providers.map(p => p.label); index: root.providerIdx; onPicked: (i) => root.providerIdx = i }
                }
                Field { id: pfKey; width: parent.width; secret: true; placeholder: "API key"; onSubmitted: root.saveKey() }
                Row {
                    spacing: 10
                    Button { label: "Save key"; primary: true; onClicked: root.saveKey() }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: root.keyMsg; color: root.keyOk ? theme.green : theme.red; font.family: theme.fontFamily; font.pixelSize: root.fs - 1 }
                }
                P { visible: profiles.list.length > 0; width: parent.width; text: "Profiles: " + profiles.list.map(p => p.name).join(", ") }
            }

            // 4 integrations
            Column {
                visible: root.step === 4
                width: parent.width; spacing: 8
                H1 { width: parent.width; text: "Integrations" }
                P { width: parent.width; text: "Hooks give exact working / blocked / done status instead of guessing from the screen (recommended). MCP lets agents control other panes: only enable it for agents you trust." }
                Repeater {
                    model: integrations.list.filter(i => i.found)
                    delegate: Rectangle {
                        required property var modelData
                        width: parent.width; height: 48; radius: 6
                        color: theme.panel; border.width: 1; border.color: theme.border
                        Text { x: 14; anchors.verticalCenter: parent.verticalCenter; text: modelData.id; color: theme.fg; font.bold: true; font.family: theme.fontFamily; font.pixelSize: root.fs }
                        Row {
                            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                            spacing: 6
                            Button {
                                visible: modelData.hookSupport
                                label: modelData.hooks ? "Hooks ✓" : "Enable hooks"; primary: !modelData.hooks
                                onClicked: root.intErr = modelData.hooks ? integrations.remove(modelData.id, "hooks") : integrations.install(modelData.id, "hooks")
                            }
                            Button {
                                label: modelData.mcp ? "MCP ✓" : "Enable MCP"
                                onClicked: root.intErr = modelData.mcp ? integrations.remove(modelData.id, "mcp") : integrations.install(modelData.id, "mcp")
                            }
                        }
                    }
                }
                P { visible: integrations.list.filter(i => i.found).length === 0; width: parent.width; text: "No supported agent found yet, nothing to configure. You can do this later in Settings › Integrations." }
                P { visible: root.intErr !== ""; width: parent.width; text: root.intErr; color: theme.red }
            }

            // 5 operator
            Column {
                visible: root.step === 5
                width: parent.width; spacing: 10
                H1 { width: parent.width; text: "Operator" }
                P { width: parent.width; text: "The operator (Ctrl+Shift+I) is an agent that operates nebula itself. It reuses the login of an agent CLI you already have, so no extra setup is needed." }
                Card {
                    width: parent.width
                    accent: operatorAgent.available
                    Column {
                        width: parent.width; spacing: 4
                        Text {
                            text: operatorAgent.available ? "Ready: " + operatorAgent.agentName : "No compatible agent detected"
                            color: operatorAgent.available ? theme.green : theme.yellow; font.bold: true
                            font.family: theme.fontFamily; font.pixelSize: root.fs
                        }
                        P { width: parent.width; text: operatorAgent.available ? "Try: “launch claude and codex on the flaky test, each in a worktree”." : "Install Codex, Claude Code or Gemini CLI, or set a custom ACP command in Settings › Agents & AI." }
                    }
                }
                Flow {
                    width: parent.width; spacing: 8
                    Repeater {
                        model: operatorAgent.agentPresets
                        delegate: Badge { required property var modelData; text: modelData.label + (modelData.available ? "" : " ✕"); tone: modelData.available ? theme.green : theme.muted }
                    }
                }
            }

            // 6 done
            Column {
                visible: root.step === 6
                width: parent.width; spacing: 12
                H1 { width: parent.width; text: "You're all set" }
                P { width: parent.width; text: "A few shortcuts to get going. Press Ctrl+Shift+? anytime for the full list." }
                Item { width: 1; height: 2 }
                Repeater {
                    model: [
                        { d: "Launch an agent", k: "Ctrl+Shift+L" },
                        { d: "Split right / down", k: "Ctrl+Shift+D" },
                        { d: "Jump to agent that needs you", k: "Ctrl+Shift+A" },
                        { d: "Operator", k: "Ctrl+Shift+I" },
                        { d: "Settings", k: "Ctrl+," }
                    ]
                    delegate: Item {
                        required property var modelData
                        width: parent.width; height: root.fs + 14
                        Text { anchors.verticalCenter: parent.verticalCenter; text: modelData.d; color: theme.fg; font.family: theme.fontFamily; font.pixelSize: root.fs }
                        Kbd { anchors { right: parent.right; verticalCenter: parent.verticalCenter } keys: modelData.k }
                    }
                }
            }
        }

        // ── footer ───────────────────────────────────────────
        Item {
            id: footer
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 60
            Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: theme.border }
            Button { x: 28; anchors.verticalCenter: parent.verticalCenter; label: "Skip setup"; visible: !root.last; onClicked: root.finish() }
            Row {
                anchors { right: parent.right; rightMargin: 28; verticalCenter: parent.verticalCenter }
                spacing: 8
                Button { label: "Back"; visible: root.step > 0; onClicked: root.back() }
                Button {
                    label: root.last ? "Launch my first agent" : "Next  →"
                    primary: true
                    onClicked: root.last ? root.launchFirst() : root.next()
                }
                Button { label: "Finish"; visible: root.last; onClicked: root.finish() }
            }
        }
    }

    function saveKey() {
        const pr = profiles.providers[providerIdx]
        if (pfKey.value === "") { keyOk = false; keyMsg = "Paste an API key first"; return }
        const ok = profiles.save(pfName.value || "default", pr.id, pfKey.value, {})
        keyOk = ok
        keyMsg = ok ? "Saved “" + (pfName.value || "default") + "”" : "Could not save (secret store unavailable?)"
        if (ok) pfKey.clear()
    }

    Keys.onReturnPressed: if (step !== 3) next()
}
