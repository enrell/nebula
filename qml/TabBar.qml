import QtQuick

Rectangle {
    id: root
    color: theme.bg
    readonly property int fs: Math.round(theme.fontSize * 1.33)

    Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: theme.border }

    Row {
        anchors.fill: parent
        anchors.bottomMargin: 1
        Repeater {
            model: win.space ? win.space.tabs : []
            delegate: Rectangle {
                required property var modelData
                required property int index
                readonly property bool current: index === win.space.currentIndex
                height: parent.height
                width: label.implicitWidth + 46
                property bool hovered: tma.containsMouse || closeBtn.hovered
                color: current ? theme.panel : (tma.containsMouse ? Qt.rgba(1, 1, 1, 0.04) : "transparent")
                Rectangle { visible: parent.current; anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 2; color: theme.accent }
                Text {
                    id: label
                    x: 14; anchors.verticalCenter: parent.verticalCenter
                    text: modelData.title
                    color: current ? theme.fg : theme.muted
                    font.family: theme.fontFamily; font.pixelSize: root.fs; font.bold: current
                }
                Rectangle { anchors { right: parent.right; top: parent.top; bottom: parent.bottom } width: 1; color: theme.border }
                MouseArea {
                    id: tma
                    hoverEnabled: true
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    onClicked: (m) => m.button === Qt.MiddleButton ? win.space.closeTab(index) : win.space.setCurrent(index)
                }
                IconButton {
                    id: closeBtn
                    visible: parent.hovered || parent.current
                    icon: "close"; size: 9
                    color: theme.muted
                    hoverColor: theme.fg
                    anchors { right: parent.right; rightMargin: 4; verticalCenter: parent.verticalCenter }
                    onClicked: win.space.closeTab(index)
                }
            }
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            icon: "plus"
            visible: win.space !== null
            onClicked: win.space.newTab()
        }
    }

    IconButton {
        visible: !app.sidebarVisible
        anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
        icon: "gear"; size: 14
        onClicked: app.setSettingsVisible(!app.settingsVisible)
    }
}
