#include "host.h"
#include "paths.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#ifndef Q_OS_WIN
#include <csignal>
#endif

static constexpr int kRingMax = 2 * 1024 * 1024;

HostServer::HostServer(QObject *parent) : QObject(parent) {
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, &HostServer::onConnection);
    m_idle.setInterval(3000);
    connect(&m_idle, &QTimer::timeout, this, &HostServer::checkIdle);
    m_idle.start();
}

bool HostServer::listen(const QString &path) {
    QLocalSocket probe;
    probe.connectToServer(path);
    if (probe.waitForConnected(200)) return false; // another host is alive
    QLocalServer::removeServer(path);
    return m_server.listen(path);
}

void HostServer::checkIdle() {
    if (m_panes.isEmpty() && m_clients.isEmpty()) QCoreApplication::quit();
}

void HostServer::send(QLocalSocket *s, quint8 type, quint32 pane, const QByteArray &payload) {
    s->write(HostProto::encode(type, pane, payload));
}

void HostServer::onConnection() {
    while (QLocalSocket *sock = m_server.nextPendingConnection()) {
        m_clients.insert(sock, {});
        connect(sock, &QLocalSocket::readyRead, this, [this, sock] { onData(sock); });
        connect(sock, &QLocalSocket::disconnected, this, [this, sock] {
            m_clients.remove(sock);
            for (Pane *p : std::as_const(m_panes)) p->clients.remove(sock);
            sock->deleteLater();
        });
    }
}

void HostServer::onData(QLocalSocket *sock) {
    auto it = m_clients.find(sock);
    if (it == m_clients.end()) return;
    it->feed(sock->readAll());
    HostProto::Frame f;
    while (true) {
        it = m_clients.find(sock);
        if (it == m_clients.end() || !it->next(f)) break;
        handle(sock, f);
    }
}

void HostServer::handle(QLocalSocket *sock, const HostProto::Frame &f) {
    using namespace HostProto;
    const int id = int(f.pane);
    Pane *p = m_panes.value(id);
    switch (f.type) {
    case Ping: send(sock, Pong, 0, QJsonDocument(QJsonObject{{"version", Version}}).toJson(QJsonDocument::Compact)); break;
    case List: {
        QJsonArray a;
        for (Pane *q : std::as_const(m_panes)) a << QJsonObject{{"id", q->id}, {"pid", q->pid}, {"exited", q->exited}};
        send(sock, ListReply, 0, QJsonDocument(a).toJson(QJsonDocument::Compact));
        break;
    }
    case Spawn:
        if (!p) spawn(id, f.payload);
        break;
    case Attach:
        if (p) attach(sock, *p);
        else send(sock, Exit, f.pane, QByteArray::fromRawData("\xff\xff\xff\xff", 4));
        break;
    case Input:
        if (p && p->pty && !p->exited) p->pty->write(f.payload);
        break;
    case Resize:
        if (p && !p->exited && f.payload.size() >= 4) {
            p->rows = qFromLittleEndian<quint16>(f.payload.constData());
            p->cols = qFromLittleEndian<quint16>(f.payload.constData() + 2);
            if (p->pty) p->pty->resize(p->rows, p->cols);
        }
        break;
    case Kill: kill(id); break;
    case Info:
        if (p) {
            const qint64 fg = p->exited || !p->pty ? -1 : p->pty->foregroundPid();
            send(sock, InfoReply, f.pane, QJsonDocument(QJsonObject{{"pid", p->pid}, {"fg", fg}, {"exited", p->exited}}).toJson(QJsonDocument::Compact));
        }
        break;
    case Shutdown: {
        const auto ids = m_panes.keys();
        for (int i : ids) kill(i);
        QCoreApplication::quit();
        break;
    }
    default: break;
    }
}

void HostServer::spawn(int id, const QByteArray &json) {
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    Pty::Spec spec;
    spec.shell = o["shell"].toString();
    if (spec.shell.isEmpty()) {
#ifdef Q_OS_WIN
        spec.shell = qEnvironmentVariable("COMSPEC", "cmd.exe");
#else
        spec.shell = "/bin/sh";
#endif
    }
    spec.cwd = o["cwd"].toString();
    spec.rows = o["rows"].toInt(24);
    spec.cols = o["cols"].toInt(80);
    for (const QJsonValue &v : o["env"].toArray()) {
        const QString e = v.toString();
        if (e.startsWith("TERM=") || e.startsWith("COLORTERM=") || e.startsWith("QT_") || e.startsWith("TERM_PROGRAM")) continue;
        spec.env << e;
    }
    spec.env << "TERM=xterm-256color"
             << "COLORTERM=truecolor"
             << "NEBULA=1"
             << "NEBULA_PANE=" + QString::number(id)
             << "AINEBULA_PANE=" + QString::number(id);   // pre-rename compat for older hooks

    auto *p = new Pane;
    p->id = id;
    p->rows = quint16(spec.rows);
    p->cols = quint16(spec.cols);
    QString error;
    p->pty = Pty::start(spec, &error, this);
    if (!p->pty) {
        // keep the pane, so the window shows why instead of waiting for a shell that never comes
        p->ring = ("nebula: cannot start " + spec.shell + (error.isEmpty() ? QString() : ": " + error) + "\r\n").toUtf8();
        p->exited = true;
        p->status = 127;
        m_panes.insert(id, p);
        return;
    }
    p->pid = p->pty->pid();
    connect(p->pty, &Pty::output, this, [this, p](const QByteArray &data) { onOutput(*p, data); });
    connect(p->pty, &Pty::finished, this, [this, p](int status) { onFinished(*p, status); });
    m_panes.insert(id, p);
}

void HostServer::attach(QLocalSocket *sock, Pane &p) {
    using namespace HostProto;
    p.clients.insert(sock);
    send(sock, Size, quint32(p.id), size(p.rows, p.cols));
    QByteArray replay;
    if (p.truncated) {
        replay += "\x1b[0m";
        for (int m : p.modes) replay += "\x1b[?" + QByteArray::number(m) + "h";
    }
    replay += p.ring;
    for (int off = 0; off < replay.size(); off += 65536) send(sock, Output, quint32(p.id), replay.mid(off, 65536));
    send(sock, ReplayDone, quint32(p.id));
    if (p.exited) {
        char b[4];
        qToLittleEndian<qint32>(p.status, b);
        send(sock, Exit, quint32(p.id), QByteArray(b, 4));
    }
}

void HostServer::onOutput(Pane &p, const QByteArray &data) {
    using namespace HostProto;
    p.ring += data;
    if (p.ring.size() > kRingMax) {
        p.ring.remove(0, p.ring.size() - kRingMax);
        const int nl = p.ring.indexOf('\n');
        if (nl >= 0 && nl < 4096) p.ring.remove(0, nl + 1);
        p.truncated = true;
    }
    scanModes(p, data);
    for (QLocalSocket *c : std::as_const(p.clients)) send(c, Output, quint32(p.id), data);
}

void HostServer::onFinished(Pane &p, int status) {
    using namespace HostProto;
    p.exited = true;
    p.status = status;
    char b[4];
    qToLittleEndian<qint32>(p.status, b);
    for (QLocalSocket *c : std::as_const(p.clients)) send(c, Exit, quint32(p.id), QByteArray(b, 4));
}

// Track DEC private modes (alt screen, mouse, bracketed paste...) so a truncated replay can restore them.
void HostServer::scanModes(Pane &p, const QByteArray &data) {
    static const QSet<int> tracked = {1, 7, 25, 47, 1000, 1002, 1003, 1004, 1005, 1006, 1015, 1047, 1049, 2004};
    const QByteArray s = p.carry + data;
    p.carry.clear();
    int i = 0;
    while ((i = s.indexOf('\x1b', i)) >= 0) {
        int j = i + 1;
        if (j >= s.size() || (s[j] == '[' && j + 1 >= s.size())) { p.carry = s.mid(i); break; }
        if (s[j] != '[' || s[j + 1] != '?') { i = j; continue; }
        j += 2;
        int start = j;
        while (j < s.size() && ((s[j] >= '0' && s[j] <= '9') || s[j] == ';')) ++j;
        if (j >= s.size()) { if (s.size() - i < 48) p.carry = s.mid(i); break; }
        if (s[j] == 'h' || s[j] == 'l') {
            for (const QByteArray &part : s.mid(start, j - start).split(';')) {
                bool ok = false;
                const int m = part.toInt(&ok);
                if (!ok || !tracked.contains(m)) continue;
                if (s[j] == 'h') p.modes.insert(m); else p.modes.remove(m);
            }
        }
        i = j;
    }
}

void HostServer::kill(int id) {
    Pane *p = m_panes.take(id);
    if (!p) return;
    delete p->pty;   // hangs up the program if it still runs
    delete p;
}

int runHost() {
#ifndef Q_OS_WIN
    signal(SIGPIPE, SIG_IGN);
#endif
    int argc = 1;
    char name[] = "nebula-host";
    char *argv[] = {name, nullptr};
    QCoreApplication app(argc, argv);
    HostServer server;
    if (!server.listen(Paths::hostSocket())) return 0;
    return app.exec();
}
