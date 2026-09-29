import QtQuick

Dialog {
    id: root
    title: "send a prompt to several agents"
    contentWidth: 560

    property var selected: ({})
    property int sent: -1
    function toggle(pane) { const s = Object.assign({}, selected); if (s[pane]) delete s[pane]; else s[pane] = true; selected = s }
    function chosen() { return Object.keys(selected).map(k => parseInt(k)) }
    function go() {
        if (chosen().length === 0 || text.value === "") return
        sent = launcher.broadcast(chosen(), text.value, true)
        if (sent > 0) app.hideOverlay()
    }

    Column {
        width: parent.width
        spacing: 12

        Text {
            visible: app.agents.length === 0
            text: "No agents are running."
            color: theme.muted; font.family: theme.fontFamily; font.pixelSize: root.fs - 1
        }
        Repeater {
            model: app.agents
            delegate: Row {
                required property var modelData
                spacing: 12
                Toggle { checked: !!root.selected[modelData.pane]; onToggled: root.toggle(modelData.pane); anchors.verticalCenter: parent.verticalCenter }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.space + "  ·  " + modelData.agent + "  ·  " + modelData.state
                    color: theme.fg; font.family: theme.fontFamily; font.pixelSize: root.fs - 1
                }
            }
        }
        Field { id: text; width: parent.width; placeholder: "prompt sent to every selected agent"; onSubmitted: root.go() }
        Row {
            spacing: 8
            Button { label: "Send"; primary: true; onClicked: root.go() }
            Button { label: "All"; onClicked: { const s = {}; app.agents.forEach(a => s[a.pane] = true); root.selected = s } }
            Button { label: "Cancel"; onClicked: app.hideOverlay() }
        }
    }
    Component.onCompleted: text.focusInput()
}
