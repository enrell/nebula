#pragma once
#include "hostproto.h"
#include "ptyprocess.h"
#include <QHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QSet>
#include <QTimer>

// Long-lived daemon that owns the ptys/shells so they survive the GUI closing (like a tiny tmux server).
// Keeps a bounded raw-output ring per pane that is replayed into a fresh terminal emulator on attach.
class HostServer : public QObject {
    Q_OBJECT
public:
    explicit HostServer(QObject *parent = nullptr);
    bool listen(const QString &path);

private:
    struct Pane {
        int id = 0;
        Pty *pty = nullptr;            // null when the program could not be started (the pane then only shows why)
        qint64 pid = -1;
        quint16 rows = 24, cols = 80;
        QByteArray ring, carry;
        bool truncated = false, exited = false;
        int status = 0;
        QSet<int> modes;
        QSet<QLocalSocket *> clients;
    };

    void onConnection();
    void onData(QLocalSocket *sock);
    void handle(QLocalSocket *sock, const HostProto::Frame &f);
    void spawn(int id, const QByteArray &json);
    void attach(QLocalSocket *sock, Pane &p);
    void onOutput(Pane &p, const QByteArray &data);
    void onFinished(Pane &p, int status);
    void scanModes(Pane &p, const QByteArray &data);
    void kill(int id);
    void send(QLocalSocket *s, quint8 type, quint32 pane, const QByteArray &payload = {});
    void checkIdle();

    QLocalServer m_server;
    QHash<int, Pane *> m_panes;
    QHash<QLocalSocket *, HostProto::Reader> m_clients;
    QTimer m_idle;
};

int runHost();
