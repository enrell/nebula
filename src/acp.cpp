#include "acp.h"
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <cstdio>

static bool traceOn() { static const bool on = qEnvironmentVariableIsSet("NEBULA_ACP_TRACE"); return on; }   // wire log on stderr

AcpClient::AcpClient(QObject *parent) : QObject(parent) {}

AcpClient::~AcpClient() { stop(); }

void AcpClient::start(const QString &program, const QStringList &args) {
    stop();
    m_stderr.clear();
    m_died = false;
    m_proc = new QProcess(this);
    // Claude Code refuses to start inside another Claude Code session; nebula may itself have been launched from one.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove("CLAUDECODE");
    env.remove("CLAUDE_CODE_ENTRYPOINT");
    m_proc->setProcessEnvironment(env);
    if (!m_workDir.isEmpty()) m_proc->setWorkingDirectory(m_workDir);
    connect(m_proc, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += QString::fromUtf8(m_proc->readAllStandardError());
        const int cut = m_stderr.size() - 6000;
        if (cut > 0) m_stderr.remove(0, cut + (m_stderr.indexOf('\n', cut) + 1));
    });
    connect(m_proc, &QProcess::started, this, &AcpClient::started);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this] {
        m_buf += m_proc->readAllStandardOutput();
        int nl;
        while ((nl = m_buf.indexOf('\n')) >= 0) {
            const QByteArray line = m_buf.left(nl).trimmed();
            m_buf.remove(0, nl + 1);
            if (traceOn()) std::fprintf(stderr, "acp <- %.400s\n", line.constData());
            if (!line.isEmpty()) dispatch(QJsonDocument::fromJson(line).object());
        }
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if ((e == QProcess::FailedToStart || e == QProcess::Crashed) && !m_died) { m_died = true; emit died(m_proc->errorString()); }
    });
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (m_died) return;
        m_died = true;
        emit died(code ? QString("agent exited with code %1").arg(code) : QString("agent exited"));
    });
    m_proc->start(program, args);
}

void AcpClient::stop() {
    if (!m_proc) return;
    m_proc->disconnect(this);
    m_proc->kill();
    m_proc->waitForFinished(1500);
    m_proc->deleteLater();
    m_proc = nullptr;
}

bool AcpClient::running() const { return m_proc && m_proc->state() == QProcess::Running; }

void AcpClient::write(const QJsonObject &msg) {
    if (!m_proc || m_proc->state() == QProcess::NotRunning) return;
    const QByteArray line = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    if (traceOn()) std::fprintf(stderr, "acp -> %.400s\n", line.constData());
    m_proc->write(line + '\n');
}

int AcpClient::call(const QString &method, const QJsonObject &params) {
    const int id = m_nextId++;
    write({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}});
    return id;
}

void AcpClient::notify(const QString &method, const QJsonObject &params) {
    write({{"jsonrpc", "2.0"}, {"method", method}, {"params", params}});
}

void AcpClient::respond(qint64 id, const QJsonObject &result) {
    write({{"jsonrpc", "2.0"}, {"id", static_cast<double>(id)}, {"result", result}});
}

void AcpClient::respondError(qint64 id, int code, const QString &message) {
    write({{"jsonrpc", "2.0"}, {"id", static_cast<double>(id)}, {"error", QJsonObject{{"code", code}, {"message", message}}}});
}

void AcpClient::dispatch(const QJsonObject &msg) {
    const QString method = msg["method"].toString();
    if (msg.contains("id") && !method.isEmpty()) {
        emit request(msg["id"].toVariant().toLongLong(), method, msg["params"].toObject());
    } else if (msg.contains("id")) {
        const QJsonObject err = msg["error"].toObject();
        emit result(msg["id"].toVariant().toLongLong(), msg.contains("result"), msg["result"], err["message"].toString());
    } else if (!method.isEmpty()) {
        emit notification(method, msg["params"].toObject());
    }
}
