#pragma once
#include <QByteArray>
#include <QtEndian>

// Wire protocol between the GUI and the pty host daemon (`nebula --host`).
// Frame: [type u8][pane u32 LE][len u32 LE][payload]
namespace HostProto {

constexpr int Version = 1;

enum Type : quint8 {
    // client -> host
    Spawn = 1,   // JSON {cwd, shell, rows, cols, env[]}
    Attach,      // start receiving output (replay first)
    Input,       // raw bytes for the pty
    Resize,      // u16 rows, u16 cols
    Kill,        // terminate the pane and forget it
    Info,        // request pid / foreground pgrp
    List,        // list panes
    Ping,
    Shutdown,    // kill every pane and exit
    // host -> client
    Output = 101,
    Size,        // u16 rows, u16 cols (sent on attach, before replay)
    Exit,        // i32 status (-1 = unknown pane)
    InfoReply,   // JSON {pid, fg}
    ListReply,   // JSON [{id, pid, exited}]
    Pong,        // JSON {version}
    ReplayDone,
};

struct Frame {
    quint8 type = 0;
    quint32 pane = 0;
    QByteArray payload;
};

inline QByteArray encode(quint8 type, quint32 pane, const QByteArray &payload = {}) {
    QByteArray out;
    out.reserve(9 + payload.size());
    out.append(char(type));
    char b[4];
    qToLittleEndian<quint32>(pane, b);
    out.append(b, 4);
    qToLittleEndian<quint32>(quint32(payload.size()), b);
    out.append(b, 4);
    out.append(payload);
    return out;
}

inline QByteArray size(quint16 rows, quint16 cols) {
    char b[4];
    qToLittleEndian<quint16>(rows, b);
    qToLittleEndian<quint16>(cols, b + 2);
    return QByteArray(b, 4);
}

class Reader {
public:
    void feed(const QByteArray &d) { m_buf += d; }
    bool next(Frame &f) {
        if (m_buf.size() < 9) return false;
        const quint32 len = qFromLittleEndian<quint32>(m_buf.constData() + 5);
        if (quint32(m_buf.size()) < 9 + len) return false;
        f.type = quint8(m_buf[0]);
        f.pane = qFromLittleEndian<quint32>(m_buf.constData() + 1);
        f.payload = m_buf.mid(9, int(len));
        m_buf.remove(0, int(9 + len));
        return true;
    }
private:
    QByteArray m_buf;
};

} // namespace HostProto
