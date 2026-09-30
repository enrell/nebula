import QtQuick

Item {
    id: root
    property var node
    property var tab
    property real ratio: node && !node.leaf ? node.ratio : 0.5
    onNodeChanged: if (node && !node.leaf) ratio = node.ratio

    readonly property bool isLeaf: !!node && node.leaf === true
    readonly property bool horiz: !!node && !node.leaf && node.horizontal
    readonly property int gap: 6
    // A pane never shrinks below a usable size (about 4 rows / 20 columns), whatever the saved ratio or drag says.
    readonly property real minFirst: root.horiz ? 160 : 100
    readonly property real avail: (root.horiz ? root.width : root.height) - root.gap
    readonly property real eff: avail <= 2 * minFirst ? 0.5 : Math.max(minFirst / avail, Math.min(1 - minFirst / avail, root.ratio))

    Loader {
        anchors.fill: parent
        active: root.isLeaf
        sourceComponent: PaneFrame { tab: root.tab; paneId: root.node.pane }
    }

    Loader {
        id: first
        active: !!root.node && !root.isLeaf
        x: 0; y: 0
        width: root.horiz ? Math.round((root.width - root.gap) * root.eff) : root.width
        height: root.horiz ? root.height : Math.round((root.height - root.gap) * root.eff)
        Component.onCompleted: setSource("PaneTree.qml", { node: Qt.binding(() => root.node && root.node.a), tab: root.tab })
    }

    Loader {
        id: second
        active: !!root.node && !root.isLeaf
        x: root.horiz ? first.width + root.gap : 0
        y: root.horiz ? 0 : first.height + root.gap
        width: root.horiz ? root.width - first.width - root.gap : root.width
        height: root.horiz ? root.height : root.height - first.height - root.gap
        Component.onCompleted: setSource("PaneTree.qml", { node: Qt.binding(() => root.node && root.node.b), tab: root.tab })
    }

    MouseArea {
        visible: !root.isLeaf
        x: root.horiz ? first.width : 0
        y: root.horiz ? 0 : first.height
        width: root.horiz ? root.gap : root.width
        height: root.horiz ? root.height : root.gap
        cursorShape: root.horiz ? Qt.SplitHCursor : Qt.SplitVCursor
        preventStealing: true
        onPositionChanged: (m) => {
            const p = mapToItem(root, m.x, m.y)
            const r = root.horiz ? (p.x - root.gap / 2) / (root.width - root.gap) : (p.y - root.gap / 2) / (root.height - root.gap)
            root.ratio = root.avail <= 2 * root.minFirst ? 0.5 : Math.max(root.minFirst / root.avail, Math.min(1 - root.minFirst / root.avail, r))
        }
        onReleased: root.tab.setRatio(root.node.node, root.ratio)
        onDoubleClicked: { root.ratio = 0.5; root.tab.setRatio(root.node.node, 0.5) }
    }
}
