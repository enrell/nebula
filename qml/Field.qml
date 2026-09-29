import QtQuick

Rectangle {
    id: root
    property string text
    property string placeholder
    property bool secret: false
    readonly property string value: input.text
    signal accepted(string text)
    signal submitted
    function focusInput() { input.forceActiveFocus() }
    function setText(t) { input.text = t }
    function clear() { input.text = "" }
    implicitWidth: 240
    implicitHeight: 28
    radius: 5
    color: "transparent"
    border.width: 1
    border.color: input.activeFocus ? theme.accent : theme.border

    readonly property int fs: Math.round(theme.fontSize * 1.33)

    TextInput {
        id: input
        anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
        verticalAlignment: TextInput.AlignVCenter
        text: root.text
        echoMode: root.secret ? TextInput.Password : TextInput.Normal
        color: theme.fg
        selectionColor: theme.accent
        selectedTextColor: theme.bg
        font.family: theme.fontFamily
        font.pixelSize: root.fs
        clip: true
        onEditingFinished: root.accepted(text)
        Keys.onReturnPressed: { root.accepted(text); root.submitted() }
        Keys.onEnterPressed: { root.accepted(text); root.submitted() }
        Keys.onEscapePressed: (e) => { text = root.text; e.accepted = false }
        Text {
            visible: !input.text && !input.activeFocus
            anchors.verticalCenter: parent.verticalCenter
            text: root.placeholder
            color: theme.muted
            font: input.font
        }
    }
}
