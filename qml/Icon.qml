import QtQuick

Canvas {
    id: c
    property string name
    property color color: theme.muted
    property int size: 14
    width: size
    height: size
    onColorChanged: requestPaint()
    onNameChanged: requestPaint()
    onSizeChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.strokeStyle = color
        ctx.fillStyle = color
        ctx.lineWidth = 1.3
        ctx.lineCap = "round"
        const w = width, h = height, m = Math.max(2, w * 0.2)
        ctx.beginPath()
        switch (name) {
        case "close":
            ctx.moveTo(m + 1, m + 1); ctx.lineTo(w - m - 1, h - m - 1)
            ctx.moveTo(w - m - 1, m + 1); ctx.lineTo(m + 1, h - m - 1)
            break
        case "check":
            ctx.lineWidth = Math.max(1.6, w * 0.17)
            ctx.lineJoin = "round"
            ctx.moveTo(w * 0.18, h * 0.55); ctx.lineTo(w * 0.42, h * 0.78); ctx.lineTo(w * 0.84, h * 0.24)
            break
        case "plus":
            ctx.moveTo(w / 2, m); ctx.lineTo(w / 2, h - m)
            ctx.moveTo(m, h / 2); ctx.lineTo(w - m, h / 2)
            break
        case "split-right":
            ctx.rect(m, m, w - 2 * m, h - 2 * m)
            ctx.moveTo(w / 2, m); ctx.lineTo(w / 2, h - m)
            break
        case "split-down":
            ctx.rect(m, m, w - 2 * m, h - 2 * m)
            ctx.moveTo(m, h / 2); ctx.lineTo(w - m, h / 2)
            break
        case "zoom":
            ctx.moveTo(m, m + 3); ctx.lineTo(m, m); ctx.lineTo(m + 3, m)
            ctx.moveTo(w - m - 3, m); ctx.lineTo(w - m, m); ctx.lineTo(w - m, m + 3)
            ctx.moveTo(w - m, h - m - 3); ctx.lineTo(w - m, h - m); ctx.lineTo(w - m - 3, h - m)
            ctx.moveTo(m + 3, h - m); ctx.lineTo(m, h - m); ctx.lineTo(m, h - m - 3)
            break
        case "gear": {
            const cx = w / 2, cy = h / 2, r = w * 0.27
            ctx.arc(cx, cy, r, 0, Math.PI * 2)
            ctx.moveTo(cx + w * 0.11, cy)
            ctx.arc(cx, cy, w * 0.11, 0, Math.PI * 2)
            for (let i = 0; i < 8; ++i) {
                const a = i * Math.PI / 4
                ctx.moveTo(cx + Math.cos(a) * r, cy + Math.sin(a) * r)
                ctx.lineTo(cx + Math.cos(a) * (w * 0.43), cy + Math.sin(a) * (w * 0.43))
            }
            break
        }
        case "keys":
            ctx.rect(m - 1, m + 1.5, w - 2 * m + 2, h - 2 * m - 3)
            ctx.moveTo(m + 2, h / 2); ctx.lineTo(w - m - 2, h / 2)
            ctx.moveTo(m + 3, h / 2 + 2.5); ctx.lineTo(w - m - 3, h / 2 + 2.5)
            break
        case "operator": {
            const cx = w / 2, cy = h / 2, r = w * 0.4, i = w * 0.11
            ctx.moveTo(cx, cy - r); ctx.lineTo(cx + i, cy - i); ctx.lineTo(cx + r, cy); ctx.lineTo(cx + i, cy + i)
            ctx.lineTo(cx, cy + r); ctx.lineTo(cx - i, cy + i); ctx.lineTo(cx - r, cy); ctx.lineTo(cx - i, cy - i)
            ctx.closePath()
            break
        }
        case "popout":
            ctx.moveTo(w / 2 - 1, m); ctx.lineTo(m, m); ctx.lineTo(m, h - m); ctx.lineTo(w - m, h - m); ctx.lineTo(w - m, h / 2 + 1)
            ctx.moveTo(w / 2 + 1, m); ctx.lineTo(w - m, m); ctx.lineTo(w - m, h / 2 - 1)
            ctx.moveTo(w - m, m); ctx.lineTo(w / 2, h / 2)
            break
        case "hide":
            ctx.moveTo(m, h - m - 1); ctx.lineTo(w - m, h - m - 1)
            break
        case "back":
            ctx.moveTo(w - m, h / 2); ctx.lineTo(m, h / 2)
            ctx.moveTo(m + 3.5, h / 2 - 3.5); ctx.lineTo(m, h / 2); ctx.lineTo(m + 3.5, h / 2 + 3.5)
            break
        }
        ctx.stroke()
    }
}
