import QtQuick

FocusScope {
    id: root
    readonly property int fs: Math.round(theme.fontSize * 1.33)
    onVisibleChanged: if (visible) forceActiveFocus()

    property int page: 0
    property int providerIdx: 0
    property string profileMsg: ""
    property bool profileOk: true

    readonly property var pages: [
        { title: "General", hint: "Look and behavior" },
        { title: "Terminal", hint: "Shell, scrollback, links" },
        { title: "Providers", hint: "API keys for your agents" },
        { title: "Agents & AI", hint: "Operator and summaries" },
        { title: "Integrations", hint: "Hooks and MCP" },
        { title: "Keybindings", hint: "Shortcuts" },
        { title: "Session & About", hint: "Persistence, reset" }
    ]

    function openPage(i) { page = i; flick.contentY = 0 }

    function editProfile(p) {
        pfName.setText(p.name)
        for (let i = 0; i < profiles.providers.length; ++i) if (profiles.providers[i].id === p.provider) providerIdx = i
        pfBase.setText(p.env.NEBULA_BASE_URL || "")
        pfKey.focusInput()
    }
    function runTest(name, row) {
        const id = llm.test(name)
        llm.finished.connect(function handler(rid, kind, text, err) {
            if (rid !== id) return
            llm.finished.disconnect(handler)
            row.result = err === "" ? "OK" : err
        })
    }

    Rectangle { anchors.fill: parent; color: theme.bg }
    MouseArea { anchors.fill: parent; onPressed: root.forceActiveFocus() }

    component Sub: Text {
        color: theme.muted
        font.family: theme.fontFamily
        font.pixelSize: root.fs - 2
        wrapMode: Text.WordWrap
    }
    component Group: Text {
        property string title
        text: title
        color: theme.accent
        font.bold: true
        font.family: theme.fontFamily
        font.pixelSize: root.fs
        topPadding: 20
        bottomPadding: 4
    }
    component PageTitle: Column {
        property string title
        property string subtitle
        width: parent ? parent.width : 0
        spacing: 4
        bottomPadding: 6
        Text { text: parent.title; color: theme.fg; font.bold: true; font.family: theme.fontFamily; font.pixelSize: root.fs + 6 }
        Sub { text: parent.subtitle; width: parent.width }
    }

    // ── header ───────────────────────────────────────────────
    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: root.fs + 28
        IconButton {
            x: 14; anchors.verticalCenter: parent.verticalCenter
            icon: "back"
            onClicked: app.setSettingsVisible(false)
        }
        Text {
            x: 50; anchors.verticalCenter: parent.verticalCenter
            text: "Settings"; color: theme.fg; font.bold: true
            font.family: theme.fontFamily; font.pixelSize: root.fs + 2
        }
        Row {
            anchors { right: parent.right; rightMargin: 20; verticalCenter: parent.verticalCenter }
            spacing: 10
            Button { label: "Setup wizard"; onClicked: { app.setSettingsVisible(false); app.runAction("setup-wizard") } }
            Kbd { anchors.verticalCenter: parent.verticalCenter; keys: "Esc" }
        }
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: theme.border }
    }

    // ── category nav ─────────────────────────────────────────
    Rectangle {
        id: nav
        anchors { left: parent.left; top: header.bottom; bottom: parent.bottom }
        width: Math.min(220, Math.max(150, parent.width * 0.24))
        color: Qt.darker(theme.bg, 1.15)
        Rectangle { anchors { right: parent.right; top: parent.top; bottom: parent.bottom } width: 1; color: theme.border }
        Column {
            anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 10 }
            Repeater {
                model: root.pages
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool cur: root.page === index
                    width: nav.width - 1
                    height: root.fs * 2 + 14
                    color: cur ? theme.panel : (ma.containsMouse ? Qt.rgba(1, 1, 1, 0.03) : "transparent")
                    Rectangle { visible: parent.cur; width: 3; height: parent.height; color: theme.accent }
                    Column {
                        x: 18; anchors.verticalCenter: parent.verticalCenter; spacing: 2
                        Text { text: modelData.title; color: cur ? theme.fg : theme.muted; font.bold: cur; font.family: theme.fontFamily; font.pixelSize: root.fs }
                        Text { text: modelData.hint; color: theme.muted; opacity: 0.7; font.family: theme.fontFamily; font.pixelSize: root.fs - 3 }
                    }
                    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.openPage(index) }
                }
            }
        }
    }

    // ── content ──────────────────────────────────────────────
    Flickable {
        id: flick
        anchors { left: nav.right; right: parent.right; top: header.bottom; bottom: parent.bottom }
        contentHeight: pagesCol.height + 60
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: pagesCol
            x: Math.max(24, (parent.width - width) / 2)
            y: 24
            width: Math.min(parent.width - 48, 700)

            // ═══ General ═══
            Column {
                id: pGeneral
                visible: root.page === 0
                width: parent.width
                PageTitle { title: "General"; subtitle: "How nebula looks and behaves. Changes apply instantly." }
                Group { title: "Appearance" }
                SettingRow {
                    label: "Font family"
                    description: "Empty follows your terminal config (" + theme.autoFontFamily + ")"
                    Field {
                        width: 240
                        text: settings.fontFamily
                        placeholder: theme.autoFontFamily
                        onAccepted: (t) => settings.fontFamily = t
                    }
                }
                SettingRow {
                    label: "Font size"
                    description: "Also Ctrl +/- and Ctrl+wheel. Reset returns to automatic."
                    Row {
                        spacing: 10
                        Stepper {
                            value: theme.fontSize; from: 6; to: 32
                            onMoved: (v) => settings.fontSize = v
                        }
                        Button { label: "Auto"; visible: settings.fontSize > 0; onClicked: settings.fontSize = 0 }
                    }
                }
                SettingRow {
                    label: "Sidebar width"
                    description: "You can also drag the sidebar edge"
                    Stepper {
                        value: settings.sidebarWidth; from: 140; to: 600; step: 10; suffix: " px"
                        onMoved: (v) => settings.sidebarWidth = v
                    }
                }
                SettingRow {
                    label: "Show sidebar"
                    Row {
                        spacing: 10
                        Kbd { anchors.verticalCenter: parent.verticalCenter; keys: "Ctrl+Shift+B" }
                        Toggle { checked: app.sidebarVisible; onToggled: app.runAction("toggle-sidebar") }
                    }
                }
                Group { title: "Behavior" }
                SettingRow {
                    label: "Focus follows mouse"
                    description: "Hovering a pane focuses it"
                    Toggle { checked: settings.focusFollowsMouse; onToggled: (v) => settings.focusFollowsMouse = v }
                }
                SettingRow {
                    label: "Desktop notifications"
                    description: "When an unfocused agent finishes or needs input"
                    Toggle { checked: settings.notifications; onToggled: (v) => settings.notifications = v }
                }
            }

            // ═══ Terminal ═══
            Column {
                id: pTerminal
                visible: root.page === 1
                width: parent.width
                PageTitle { title: "Terminal"; subtitle: "Applies to new panes and the way you interact with the terminal." }
                SettingRow {
                    label: "Shell"
                    description: "Used for new panes. Empty uses $SHELL"
                    Field {
                        width: 240
                        text: settings.shell
                        placeholder: "$SHELL"
                        onAccepted: (t) => settings.shell = t.trim()
                    }
                }
                SettingRow {
                    label: "Scrollback lines"
                    Stepper {
                        value: settings.scrollback; from: 0; to: 100000; step: 1000
                        onMoved: (v) => settings.scrollback = v
                    }
                }
                SettingRow {
                    label: "Copy on select"
                    description: "Selecting text copies it (middle click pastes)"
                    Toggle { checked: settings.copyOnSelect; onToggled: (v) => settings.copyOnSelect = v }
                }
                SettingRow {
                    label: "Ctrl+click opens links"
                    Toggle { checked: settings.urlClick; onToggled: (v) => settings.urlClick = v }
                }
            }

            // ═══ Providers ═══
            Column {
                id: pProviders
                visible: root.page === 2
                width: parent.width
                spacing: 10
                PageTitle {
                    title: "Providers"
                    subtitle: "A profile is a named API key injected as environment variables into the panes that use it. "
                        + (profiles.backend === "keyring"
                            ? "Keys are kept in your system keyring and never written to settings or session files."
                            : "No system keyring found: keys are stored in ~/.config/nebula/secrets.json (mode 0600).")
                }
                Card {
                    visible: profiles.list.length === 0
                    width: parent.width
                    Column {
                        width: parent.width
                        spacing: 6
                        Text { text: "No profiles yet"; color: theme.fg; font.bold: true; font.family: theme.fontFamily; font.pixelSize: root.fs }
                        Sub { width: parent.width; text: "Using a subscription login (Claude, ChatGPT, Gemini)? You can skip this: agents authenticate themselves. Add a profile only if you want to use an API key." }
                    }
                }
                Repeater {
                    model: profiles.list
                    delegate: Card {
                        id: prow
                        required property var modelData
                        property string result: ""
                        readonly property bool allSet: modelData.vars.every(v => v.set)
                        width: pProviders.width
                        Column {
                            width: parent.width
                            spacing: 10
                            Row {
                                spacing: 8
                                Text { text: prow.modelData.name; color: theme.fg; font.bold: true; font.family: theme.fontFamily; font.pixelSize: root.fs + 1 }
                                Badge { visible: prow.modelData.default; text: "default"; tone: theme.accent }
                                Badge { text: prow.modelData.provider; tone: theme.muted }
                                Badge { text: prow.allSet ? "key set" : "key missing"; tone: prow.allSet ? theme.green : theme.red }
                                Badge {
                                    visible: prow.result !== ""
                                    text: prow.result === "OK" ? "test passed" : (prow.result === "…" ? "testing…" : "test failed")
                                    tone: prow.result === "OK" ? theme.green : (prow.result === "…" ? theme.yellow : theme.red)
                                }
                            }
                            Sub { visible: prow.result !== "" && prow.result !== "OK" && prow.result !== "…"; width: parent.width; text: prow.result; color: theme.red }
                            Row {
                                spacing: 6
                                Button { label: "Test connection"; onClicked: { prow.result = "…"; root.runTest(prow.modelData.name, prow) } }
                                Button { label: "Edit"; onClicked: root.editProfile(prow.modelData) }
                                Button { label: "Make default"; visible: !prow.modelData.default; onClicked: profiles.setDefault(prow.modelData.name) }
                                Button { label: "Delete"; danger: true; onClicked: profiles.remove(prow.modelData.name) }
                            }
                        }
                    }
                }
                Card {
                    width: parent.width
                    accent: true
                    Column {
                        width: parent.width
                        spacing: 10
                        Text { text: "Add or update a profile"; color: theme.accent; font.bold: true; font.family: theme.fontFamily; font.pixelSize: root.fs }
                        Row {
                            spacing: 8
                            Field { id: pfName; width: 180; placeholder: "profile name" }
                            Choice {
                                width: 220
                                model: profiles.providers.map(p => p.label)
                                index: root.providerIdx
                                onPicked: (i) => root.providerIdx = i
                            }
                        }
                        Field { id: pfKey; width: parent.width; secret: true; placeholder: "API key (leave empty to keep the stored one)" }
                        Field { id: pfBase; width: parent.width; placeholder: "base URL (optional: proxy or local server)" }
                        Row {
                            spacing: 10
                            Button {
                                label: "Save profile"; primary: true
                                onClicked: {
                                    const pr = profiles.providers[root.providerIdx]
                                    const ok = profiles.save(pfName.value, pr.id, pfKey.value, pfBase.value ? { "NEBULA_BASE_URL": pfBase.value } : {})
                                    root.profileOk = ok
                                    root.profileMsg = ok ? "Saved “" + pfName.value + "”" : "Could not save: name/provider missing or secret store unavailable"
                                    if (ok) pfKey.clear()
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.profileMsg
                                color: root.profileOk ? theme.green : theme.red
                                font.family: theme.fontFamily; font.pixelSize: root.fs - 1
                            }
                        }
                    }
                }
            }

            // ═══ Agents & AI ═══
            Column {
                id: pAgents
                visible: root.page === 3
                width: parent.width
                PageTitle { title: "Agents & AI"; subtitle: "The operator is an agent that runs nebula for you. Summaries are an optional one-line recap of finished agents." }
                Group { title: "Operator" }
                Card {
                    width: parent.width
                    Row {
                        spacing: 12
                        Text { text: "◎"; color: theme.accent; font.pixelSize: root.fs * 2 }
                        Sub {
                            width: parent.parent.width - 50
                            text: "Press Ctrl+Shift+I and tell it what you need: it launches and prompts your coding agents, answers their permission prompts, reads their output and reorganizes panes. It talks ACP to a real agent CLI, so it uses whatever login that CLI already has."
                        }
                    }
                }
                Item { width: 1; height: 6 }
                SettingRow {
                    label: "Agent"
                    description: "Empty auto-detects. Installed: " + (operatorAgent.agentPresets.filter(p => p.available).map(p => p.label).join(", ") || "none found")
                    Choice {
                        width: 240
                        model: ["auto-detect"].concat(operatorAgent.agentPresets.map(p => p.label + (p.available ? "" : " (missing)")))
                        index: 0
                        onPicked: (i) => { const c = i > 0 ? operatorAgent.agentPresets[i - 1].command : ""; opCmdField.setText(c); settings.operatorAgent = c }
                    }
                }
                SettingRow {
                    label: "Custom command"
                    description: "Any ACP-compatible command. Overrides the choice above"
                    Field { id: opCmdField; width: 300; text: settings.operatorAgent; placeholder: "auto"; onAccepted: (t) => settings.operatorAgent = t.trim() }
                }
                SettingRow {
                    label: "Model"
                    description: "Empty uses the agent's default model"
                    Field { width: 200; text: settings.operatorModel; placeholder: "agent default"; onAccepted: (t) => settings.operatorModel = t.trim() }
                }
                SettingRow {
                    label: "Auto-approve tool calls (yolo)"
                    description: "On by default: the operator never asks before running its tools (launch, prompt, close panes…). Turn off to get an Allow/Deny prompt for each call."
                    Toggle { checked: settings.operatorApproveAll; onToggled: (v) => settings.operatorApproveAll = v }
                }
                Group { title: "Summaries" }
                SettingRow {
                    label: "Summarize finished agents"
                    description: "A small model writes a one-line recap in the sidebar and notification. Sends the end of the terminal output to your provider."
                    Toggle { checked: settings.aiSummaries; onToggled: (v) => settings.aiSummaries = v }
                }
                SettingRow {
                    label: "Profile"
                    description: "Credentials used by summaries and nebula ctl llm.ask"
                    Choice {
                        width: 220
                        model: ["(default profile)"].concat(profiles.list.map(p => p.name))
                        index: Math.max(0, profiles.list.map(p => p.name).indexOf(settings.aiProfile) + 1)
                        onPicked: (i) => settings.aiProfile = i === 0 ? "" : profiles.list[i - 1].name
                    }
                }
                SettingRow {
                    label: "Summary model"
                    description: "Small and cheap. Empty uses the provider default"
                    Field { width: 220; text: settings.aiModel; placeholder: "provider default"; onAccepted: (t) => settings.aiModel = t.trim() }
                }
            }

            // ═══ Integrations ═══
            Column {
                id: pInteg
                visible: root.page === 4
                width: parent.width
                spacing: 10
                PageTitle {
                    title: "Integrations"
                    subtitle: "Hooks make the sidebar state exact (working / blocked / done) with no screen scraping. MCP lets agents list, read and prompt sibling panes. Your files are merged and backed up as *.nebula-bak."
                }
                Repeater {
                    model: integrations.list
                    delegate: Card {
                        id: irow
                        required property var modelData
                        property string err: ""
                        width: pInteg.width
                        opacity: modelData.found ? 1 : 0.7
                        Column {
                            width: parent.width
                            spacing: 10
                            Row {
                                spacing: 8
                                Text { text: irow.modelData.id; color: theme.fg; font.bold: true; font.family: theme.fontFamily; font.pixelSize: root.fs + 1 }
                                Badge { text: irow.modelData.found ? "installed" : "not found"; tone: irow.modelData.found ? theme.green : theme.muted }
                                Badge { visible: irow.modelData.hookSupport; text: irow.modelData.hooks ? "hooks on" : "hooks off"; tone: irow.modelData.hooks ? theme.green : theme.muted }
                                Badge { text: irow.modelData.mcp ? "MCP on" : "MCP off"; tone: irow.modelData.mcp ? theme.green : theme.muted }
                            }
                            Sub { visible: text !== ""; width: parent.width; text: irow.err !== "" ? irow.err : irow.modelData.note; color: irow.err !== "" ? theme.red : theme.muted }
                            Row {
                                spacing: 6
                                Button {
                                    visible: irow.modelData.hookSupport
                                    label: irow.modelData.hooks ? "Remove hooks" : "Install hooks"
                                    primary: !irow.modelData.hooks
                                    onClicked: irow.err = irow.modelData.hooks ? integrations.remove(irow.modelData.id, "hooks") : integrations.install(irow.modelData.id, "hooks")
                                }
                                Button {
                                    label: irow.modelData.mcp ? "Remove MCP" : "Install MCP"
                                    primary: !irow.modelData.mcp
                                    onClicked: irow.err = irow.modelData.mcp ? integrations.remove(irow.modelData.id, "mcp") : integrations.install(irow.modelData.id, "mcp")
                                }
                            }
                        }
                    }
                }
                Sub { width: parent.width; text: "MCP gives an agent real terminal control over your other panes. Only install it in agents you trust." }
            }

            // ═══ Keybindings ═══
            Column {
                id: pKeys
                visible: root.page === 5
                width: parent.width
                PageTitle { title: "Keybindings"; subtitle: "All Ctrl+Shift+… combos are unused by terminal programs, so nothing is stolen from your shell or TUIs." }
                SettingRow {
                    label: "Custom bindings"
                    description: "Edit keys.conf, then reload. Lines look like: Ctrl+Shift+D = split-right"
                    Row {
                        spacing: 8
                        Button { label: "Edit keys.conf"; onClicked: app.openKeysConfig() }
                        Button { label: "Reload"; onClicked: app.reloadBindings() }
                    }
                }
                Item { width: 1; height: 8 }
                Repeater {
                    model: app.bindings
                    delegate: Item {
                        required property var modelData
                        width: pKeys.width
                        height: root.fs + 18
                        Text { anchors.verticalCenter: parent.verticalCenter; text: modelData.desc; color: theme.fg; font.family: theme.fontFamily; font.pixelSize: root.fs - 1 }
                        Kbd { anchors { right: parent.right; verticalCenter: parent.verticalCenter } keys: modelData.keys }
                        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: theme.border; opacity: 0.4 }
                    }
                }
            }

            // ═══ Session & About ═══
            Column {
                id: pSession
                visible: root.page === 6
                width: parent.width
                PageTitle { title: "Session & About"; subtitle: "nebula " + Qt.application.version }
                Card {
                    width: parent.width
                    Row {
                        spacing: 12
                        Text { text: "↻"; color: theme.green; font.pixelSize: root.fs * 2 }
                        Sub {
                            width: parent.parent.width - 50
                            text: "Closing the window keeps everything running. Shells live in a background host and are reattached, with scrollback, next time you open nebula. After a reboot the layout is restored with fresh shells, and agents get their resume command typed at the prompt."
                        }
                    }
                }
                Group { title: "Paths" }
                SettingRow { label: "Config"; description: settings.configPath }
                SettingRow { label: "Socket"; description: settings.socketPath }
                Group { title: "Danger zone" }
                SettingRow {
                    label: "Reset all settings"
                    description: "Restores defaults. Profiles and the session are untouched."
                    Button { label: "Reset"; danger: true; onClicked: settings.resetAll() }
                }
                SettingRow {
                    label: "Kill session"
                    description: "Terminates every pane and quits"
                    Button { label: "Kill all & quit"; danger: true; onClicked: app.killSession() }
                }
            }
        }
    }
}
