import QtQuick

Rectangle {
    id: root
    default property alias content: inner.data
    property int padding: 16
    property bool accent: false
    implicitHeight: inner.childrenRect.height + padding * 2
    radius: 8
    color: theme.panel
    border.width: 1
    border.color: accent ? theme.accent : theme.border
    Item {
        id: inner
        x: root.padding; y: root.padding
        width: root.width - root.padding * 2
        height: childrenRect.height
    }
}
