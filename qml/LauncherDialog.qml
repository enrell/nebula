import QtQuick

Dialog {
    id: root
    title: "launch agent"
    contentWidth: 600

    property string agent: "claude"
    property string error: ""
    readonly property var profileNames: ["(default)", "none"].concat(profiles.list.map(p => p.name))
    readonly property var wheres: [
        { label: "split right", value: "split-right" }, { label: "split down", value: "split-down" },
        { label: "new tab", value: "tab" }, { label: "new space", value: "space" }]
    property int profileIdx: 0
    property int whereIdx: 0

    function go() {
        const p = profileIdx === 0 ? "" : profileNames[profileIdx]
        const r = launcher.launch({
            agent: agent, profile: p, model: model.value, cwd: cwd.value, worktree: wt.checked,
            branch: branch.value, prompt: prompt.value, args: extra.value, where: wheres[whereIdx].value })
        if (r.ok) app.hideOverlay(); else error = r.error
    }

    Column {
        width: parent.width
        spacing: 12

        Flow {
            width: parent.width
            spacing: 6
            Repeater {
                model: launcher.presets
                delegate: Button {
                    required property var modelData
                    label: modelData.label
                    primary: root.agent === modelData.id
                    onClicked: root.agent = modelData.id
                }
            }
        }

        Grid {
            columns: 2
            columnSpacing: 14
            rowSpacing: 10
            width: parent.width
            readonly property int colW: (width - columnSpacing) / 2

            Column { spacing: 4; width: parent.colW
                Text { text: "profile"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2 }
                Choice { width: parent.width; model: root.profileNames; index: root.profileIdx; onPicked: (i) => root.profileIdx = i } }
            Column { spacing: 4; width: parent.colW
                Text { text: "open in"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2 }
                Choice { width: parent.width; model: root.wheres.map(w => w.label); index: root.whereIdx; onPicked: (i) => root.whereIdx = i } }
            Column { spacing: 4; width: parent.colW
                Text { text: "model (optional)"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2 }
                Field { id: model; width: parent.width; placeholder: "agent default" } }
            Column { spacing: 4; width: parent.colW
                Text { text: "extra arguments"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2 }
                Field { id: extra; width: parent.width; placeholder: "--flag ..." } }
        }

        Column { spacing: 4; width: parent.width
            Text { text: "directory"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2 }
            Field { id: cwd; width: parent.width; text: launcher.currentCwd() } }

        Row {
            spacing: 12
            Toggle { id: wt; anchors.verticalCenter: parent.verticalCenter }
            Text { anchors.verticalCenter: parent.verticalCenter; text: "new git worktree"; color: theme.fg; font.family: theme.fontFamily; font.pixelSize: root.fs - 1 }
            Field { id: branch; visible: wt.checked; width: 240; placeholder: "branch (auto)" }
        }

        Column { spacing: 4; width: parent.width
            Text { text: "initial prompt (optional)"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 2 }
            Field { id: prompt; width: parent.width; placeholder: "what should it work on?"; onSubmitted: root.go() } }

        Text { visible: root.error !== ""; width: parent.width; wrapMode: Text.WordWrap; text: root.error; color: theme.red; font.family: theme.fontFamily; font.pixelSize: root.fs - 1 }

        Row {
            spacing: 8
            Button { label: "Launch"; primary: true; onClicked: root.go() }
            Button { label: "Cancel"; onClicked: app.hideOverlay() }
        }
    }
    Component.onCompleted: prompt.focusInput()
}
