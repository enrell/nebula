#include "host.h"
#include "paths.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

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
        if (p && !p->exited) write(*p, f.payload);
        break;
    case Resize:
        if (p && !p->exited && f.payload.size() >= 4) {
            p->rows = qFromLittleEndian<quint16>(f.payload.constData());
            p->cols = qFromLittleEndian<quint16>(f.payload.constData() + 2);
            struct winsize ws = {p->rows, p->cols, 0, 0};
            ioctl(p->fd, TIOCSWINSZ, &ws);
        }
        break;
    case Kill: kill(id); break;
    case Info:
        if (p) {
            const pid_t fg = p->exited ? -1 : tcgetpgrp(p->fd);
            send(sock, InfoReply, f.pane, QJsonDocument(QJsonObject{{"pid", p->pid}, {"fg", qint64(fg)}, {"exited", p->exited}}).toJson(QJsonDocument::Compact));
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
    QByteArray sh = o["shell"].toString().toLocal8Bit();
    if (sh.isEmpty()) sh = "/bin/sh";
    const QByteArray dir = QFile::encodeName(o["cwd"].toString());
    std::vector<QByteArray> env;
    for (const QJsonValue &v : o["env"].toArray()) {
        const QByteArray e = v.toString().toLocal8Bit();
        if (e.startsWith("TERM=") || e.startsWith("COLORTERM=") || e.startsWith("QT_") || e.startsWith("TERM_PROGRAM")) continue;
        env.push_back(e);
    }
    env.push_back("TERM=xterm-256color");
    env.push_back("COLORTERM=truecolor");
    env.push_back("NEBULA=1");
    env.push_back("NEBULA_PANE=" + QByteArray::number(id));
    env.push_back("AINEBULA_PANE=" + QByteArray::number(id));   // pre-rename compat for older hooks
    std::vector<char *> envp;
    for (auto &e : env) envp.push_back(e.data());
    envp.push_back(nullptr);
    char *argv[] = {sh.data(), nullptr};

    auto *p = new Pane;
    p->id = id;
    p->rows = quint16(o["rows"].toInt(24));
    p->cols = quint16(o["cols"].toInt(80));
    struct winsize ws = {p->rows, p->cols, 0, 0};
    int fd = -1;
    const pid_t pid = forkpty(&fd, nullptr, nullptr, &ws);
    if (pid == 0) {
        if (!dir.isEmpty() && chdir(dir.constData()) != 0) (void)!chdir("/");
        execve(sh.constData(), argv, envp.data());
        _exit(127);
    }
    if (pid < 0) { delete p; return; }
    p->fd = fd;
    p->pid = pid;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    p->rn = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(p->rn, &QSocketNotifier::activated, this, [this, p] { onReadable(*p); });
    p->wn = new QSocketNotifier(fd, QSocketNotifier::Write, this);
    p->wn->setEnabled(false);
    connect(p->wn, &QSocketNotifier::activated, this, [p] {
        const ssize_t n = ::write(p->fd, p->pending.constData(), size_t(p->pending.size()));
        if (n > 0) p->pending.remove(0, n);
        if (p->pending.isEmpty()) p->wn->setEnabled(false);
    });
    m_panes.insert(id, p);
}

void HostServer::write(Pane &p, const QByteArray &data) {
    const char *s = data.constData();
    size_t len = size_t(data.size());
    if (p.pending.isEmpty()) {
        while (len) {
            const ssize_t n = ::write(p.fd, s, len);
            if (n > 0) { s += n; len -= size_t(n); continue; }
            if (n < 0 && errno == EINTR) continue;
            break;
        }
    }
    if (len) { p.pending.append(s, qsizetype(len)); p.wn->setEnabled(true); }
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

void HostServer::onReadable(Pane &p) {
    using namespace HostProto;
    char buf[65536];
    for (;;) {
        const ssize_t n = read(p.fd, buf, sizeof buf);
        if (n > 0) {
            const QByteArray data(buf, int(n));
            p.ring += data;
            if (p.ring.size() > kRingMax) {
                p.ring.remove(0, p.ring.size() - kRingMax);
                const int nl = p.ring.indexOf('\n');
                if (nl >= 0 && nl < 4096) p.ring.remove(0, nl + 1);
                p.truncated = true;
            }
            scanModes(p, data);
            for (QLocalSocket *c : std::as_const(p.clients)) send(c, Output, quint32(p.id), data);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
        break;
    }
    p.rn->setEnabled(false);
    p.wn->setEnabled(false);
    int st = 0;
    waitpid(pid_t(p.pid), &st, 0);
    p.exited = true;
    p.status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
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
    delete p->rn;
    delete p->wn;
    if (!p->exited) {
        ::kill(pid_t(p->pid), SIGHUP);
        close(p->fd);
        const pid_t pid = pid_t(p->pid);
        QTimer::singleShot(1000, this, [pid] { waitpid(pid, nullptr, WNOHANG); });
    } else {
        close(p->fd);
    }
    delete p;
}

int runHost() {
    signal(SIGPIPE, SIG_IGN);
    int argc = 1;
    char name[] = "nebula-host";
    char *argv[] = {name, nullptr};
    QCoreApplication app(argc, argv);
    HostServer server;
    if (!server.listen(Paths::hostSocket())) return 0;
    return app.exec();
}
