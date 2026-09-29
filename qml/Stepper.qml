import QtQuick

Row {
    id: root
    property real value: 0
    property real from: 0
    property real to: 100
    property real step: 1
    property string suffix: ""
    property string autoText: ""
    signal moved(real value)
    spacing: 0

    readonly property int fs: Math.round(theme.fontSize * 1.33)

    function nudge(d) { root.moved(Math.max(from, Math.min(to, value + d * step))) }

    component Btn: Rectangle {
        property string label
        signal clicked
        width: 28; height: 28
        color: ma.containsMouse ? theme.panel : "transparent"
        border.width: 1; border.color: theme.border
        Text { anchors.centerIn: parent; text: parent.label; color: theme.fg; font.family: theme.fontFamily; font.pixelSize: root.fs }
        MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
    }

    Btn { label: "−"; onClicked: root.nudge(-1) }
    Rectangle {
        width: 84; height: 28
        color: "transparent"
        border.width: 1; border.color: theme.border
        Text {
            anchors.centerIn: parent
            text: root.autoText && root.value === 0 ? root.autoText : (Math.round(root.value * 10) / 10) + root.suffix
            color: theme.fg; font.family: theme.fontFamily; font.pixelSize: root.fs
        }
    }
    Btn { label: "+"; onClicked: root.nudge(1) }
}
