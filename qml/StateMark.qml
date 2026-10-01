import QtQuick

// An agent's state as a mark that reads without color: working spins (the braille spinner terminal programs use,
// like the operator's), blocked pulses until you look, done is a check, idle a hollow ring. Only working and
// blocked move, so motion always means "something is happening" or "you are needed".
Item {
    id: root
    property string mark
    property int size: 8
    readonly property color tone: win.stateColor(mark)
    implicitWidth: size + 4
    implicitHeight: size + 4
    visible: mark === "working" || mark === "blocked" || mark === "done" || mark === "idle"

    // working: the braille frame drawn as real dots (a font's braille is too faint at this size); unlit dots stay faint
    Grid {
        visible: root.mark === "working"
        anchors.centerIn: parent
        columns: 2
        readonly property real d: Math.max(2, Math.round(root.size * 0.34))
        readonly property int bits: win.spinFrames[win.spin].charCodeAt(0) - 0x2800
        spacing: Math.max(1, Math.round(d * 0.55))
        // braille dot numbering: bits 0,1,2 go down the left column, 3,4,5 down the right
        Repeater {
            model: [0, 3, 1, 4, 2, 5]
            Rectangle {
                required property int modelData
                width: parent.d; height: parent.d; radius: parent.d / 2
                color: root.tone
                opacity: (parent.bits >> modelData) & 1 ? 1 : 0.18
            }
        }
    }

    Icon {
        visible: root.mark === "done"
        anchors.centerIn: parent
        name: "check"
        size: root.size + 4
        color: root.tone
    }

    // blocked: a solid dot with a ring that grows and fades, about once a second and a half (calm, never a flash)
    Rectangle {
        visible: root.mark === "blocked" || root.mark === "idle"
        anchors.centerIn: parent
        width: root.size; height: root.size; radius: width / 2
        color: root.mark === "blocked" ? root.tone : "transparent"
        border.width: root.mark === "idle" ? 1 : 0
        border.color: root.tone
    }
    Rectangle {
        id: ring
        visible: root.mark === "blocked"
        anchors.centerIn: parent
        width: root.size; height: width; radius: width / 2
        color: "transparent"
        border.width: 1.5
        border.color: root.tone
        ParallelAnimation {
            running: ring.visible
            loops: Animation.Infinite
            NumberAnimation { target: ring; property: "width"; from: root.size; to: root.size * 2.4; duration: 1500; easing.type: Easing.OutCubic }
            NumberAnimation { target: ring; property: "opacity"; from: 0.9; to: 0; duration: 1500; easing.type: Easing.OutCubic }
        }
    }
}
