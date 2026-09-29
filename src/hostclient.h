#pragma once
#include "hostproto.h"
#include <QHash>
#include <QLocalSocket>
#include <QObject>

class TerminalSession;

// GUI-side connection to the pty host daemon; routes frames to TerminalSession objects by pane id.
class HostClient : public QObject {
    Q_OBJECT
public:
    struct PaneInfo { int id; qint64 pid; bool exited; };

    static HostClient *instance();
    bool connectToHost();                 // starts the daemon if needed
    QList<PaneInfo> list();               // blocking; used at startup
    bool connected() const { return m_sock.state() == QLocalSocket::ConnectedState; }

    void registerPane(int id, TerminalSession *s) { m_sessions.insert(id, s); }
    void unregisterPane(int id) { m_sessions.remove(id); }

    void spawn(int id, const QString &cwd, const QString &shell, int rows, int cols, const QStringList &extraEnv = {});
    void attach(int id) { send(HostProto::Attach, id); }
    void input(int id, const QByteArray &d) { send(HostProto::Input, id, d); }
    void resize(int id, int rows, int cols) { send(HostProto::Resize, id, HostProto::size(quint16(rows), quint16(cols))); }
    void kill(int id) { send(HostProto::Kill, id); }
    void requestInfo(int id) { send(HostProto::Info, id); }
    void shutdown() { send(HostProto::Shutdown, 0); m_sock.flush(); m_sock.waitForBytesWritten(500); }

private:
    HostClient() = default;
    void send(quint8 type, int pane, const QByteArray &payload = {});
    void onData();
    void dispatch(const HostProto::Frame &f);

    QLocalSocket m_sock;
    HostProto::Reader m_reader;
    QHash<int, TerminalSession *> m_sessions;
    QByteArray m_listReply;
    bool m_gotList = false;
};
