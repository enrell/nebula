#include "hostclient.h"
#include "paths.h"
#include "platform.h"
#include "terminalsession.h"
#include <QCoreApplication>
#include <QDir>
#include <QSet>
#include <QDeadlineTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>

HostClient *HostClient::instance() {
    static HostClient *c = new HostClient;
    return c;
}

bool HostClient::connectToHost() {
    if (connected()) return true;
    const QString path = Paths::hostSocket();
    auto tryConnect = [&](int ms) {
        m_sock.abort();
        m_sock.connectToServer(path);
        return m_sock.waitForConnected(ms);
    };
    if (!tryConnect(200)) {
        QProcess::startDetached(Paths::selfExe(), {"--host"});
        QDeadlineTimer deadline(4000);
        bool ok = false;
        while (!deadline.hasExpired() && !(ok = tryConnect(100))) QThread::msleep(50);
        if (!ok) return false;
    }
    // protocol version check
    send(HostProto::Ping, 0);
    if (!m_sock.waitForReadyRead(1500)) return false;
    m_reader.feed(m_sock.readAll());
    HostProto::Frame f;
    if (!m_reader.next(f) || f.type != HostProto::Pong) return false;
    if (QJsonDocument::fromJson(f.payload).object()["version"].toInt() != HostProto::Version) {
        std::fprintf(stderr, "nebula: the running pty host speaks a different protocol version; run `nebula --stop-host` first\n");
        return false;
    }
    connect(&m_sock, &QLocalSocket::readyRead, this, &HostClient::onData);
    connect(&m_sock, &QLocalSocket::disconnected, this, [this] {
        const auto sessions = m_sessions.values();
        for (TerminalSession *s : sessions) s->hostGone();
    });
    return true;
}

QList<HostClient::PaneInfo> HostClient::list() {
    QList<PaneInfo> out;
    if (!connected()) return out;
    m_gotList = false;
    send(HostProto::List, 0);
    QDeadlineTimer deadline(2000);
    while (!m_gotList && !deadline.hasExpired()) {
        if (!m_sock.bytesAvailable() && !m_sock.waitForReadyRead(200)) continue;
        onData();
    }
    for (const QJsonValue &v : QJsonDocument::fromJson(m_listReply).array()) {
        const QJsonObject o = v.toObject();
        out.append({o["id"].toInt(), o["pid"].toInteger(), o["exited"].toBool()});
    }
    return out;
}

void HostClient::spawn(int id, const QString &cwd, const QString &shell, int rows, int cols, const QStringList &extraEnv) {
    QJsonArray env;
    // variable names are case-insensitive on Windows
    const auto key = [](const QString &entry) { return Platform::isWindows ? entry.section('=', 0, 0).toUpper() : entry.section('=', 0, 0); };
    QSet<QString> override;
    for (const QString &e : extraEnv) override.insert(key(e));
    for (const QString &v : QProcessEnvironment::systemEnvironment().toStringList())
        if (!v.startsWith('=') && !override.contains(key(v))) env.append(v);
    env.append("NEBULA_SOCKET=" + Paths::socket());
    env.append("NEBULA_BIN=" + Paths::selfExe());
    // pre-rename aliases so hooks/scripts installed by older versions keep working
    env.append("AINEBULA_SOCKET=" + Paths::socket());
    env.append("AINEBULA_BIN=" + Paths::selfExe());
    for (const QString &e : extraEnv) env.append(e);
    send(HostProto::Spawn, id,
         QJsonDocument(QJsonObject{{"cwd", cwd.isEmpty() ? QDir::homePath() : cwd}, {"shell", Platform::shellCommandLine(shell)},
                                   {"rows", rows}, {"cols", cols}, {"env", env}}).toJson(QJsonDocument::Compact));
}

void HostClient::send(quint8 type, int pane, const QByteArray &payload) {
    if (connected()) m_sock.write(HostProto::encode(type, quint32(pane), payload));
}

void HostClient::onData() {
    m_reader.feed(m_sock.readAll());
    HostProto::Frame f;
    while (m_reader.next(f)) dispatch(f);
}

void HostClient::dispatch(const HostProto::Frame &f) {
    using namespace HostProto;
    if (f.type == ListReply) { m_listReply = f.payload; m_gotList = true; return; }
    TerminalSession *s = m_sessions.value(int(f.pane));
    if (!s) return;
    switch (f.type) {
    case Output: s->hostOutput(f.payload); break;
    case Size: s->hostSize(qFromLittleEndian<quint16>(f.payload.constData()), qFromLittleEndian<quint16>(f.payload.constData() + 2)); break;
    case ReplayDone: s->hostReplayDone(); break;
    case Exit: s->hostExit(f.payload.size() >= 4 ? qFromLittleEndian<qint32>(f.payload.constData()) : -1); break;
    case InfoReply: {
        const QJsonObject o = QJsonDocument::fromJson(f.payload).object();
        s->hostInfo(o["pid"].toInteger(), o["fg"].toInteger());
        break;
    }
    default: break;
    }
}
