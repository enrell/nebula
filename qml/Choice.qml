import QtQuick

Rectangle {
    id: root
    property var model: []
    property int index: 0
    signal picked(int index)
    implicitWidth: 220
    height: 28
    radius: 5
    color: "transparent"
    border.width: 1
    border.color: theme.border
    readonly property int fs: Math.round(theme.fontSize * 1.33)

    function step(d) {
        if (model.length === 0) return
        picked((index + d + model.length) % model.length)
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - 44
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        text: root.model.length ? root.model[Math.min(root.index, root.model.length - 1)] : ""
        color: theme.fg; font.family: theme.fontFamily; font.pixelSize: root.fs - 1
    }
    Text { x: 8; anchors.verticalCenter: parent.verticalCenter; text: "‹"; color: theme.muted; font.pixelSize: root.fs + 2 }
    Text { anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter } text: "›"; color: theme.muted; font.pixelSize: root.fs + 2 }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: (m) => root.step(m.x < root.width / 2 ? -1 : 1)
        onWheel: (w) => root.step(w.angleDelta.y > 0 ? -1 : 1)
    }
}
