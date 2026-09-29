#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>

// Minimal Agent Client Protocol client: newline-delimited JSON-RPC over an agent subprocess' stdio.
// Drives claude-agent-acp / codex-acp / `gemini --acp` style agents: they authenticate with whatever
// login the underlying CLI already has (subscription or API key), so no provider keys are needed here.
class AcpClient : public QObject {
    Q_OBJECT
public:
    explicit AcpClient(QObject *parent = nullptr);
    ~AcpClient() override;

    // Runs the agent process. started() fires when it is up (or died() on failure).
    virtual void start(const QString &program, const QStringList &args);
    virtual void stop();
    virtual bool running() const;
    void setWorkingDirectory(const QString &dir) { m_workDir = dir; }
    // Last lines the agent wrote to stderr (for diagnostics when it dies or errors).
    QString stderrTail() const { return m_stderr; }
    // Last protocol lines in both directions (always recorded, bounded): what "copy log" hands to a bug report.
    QString wireLog() const { return m_wire.join('\n'); }

    // JSON-RPC request -> result() signal. Returns the request id.
    virtual int call(const QString &method, const QJsonObject &params = {});
    virtual void notify(const QString &method, const QJsonObject &params = {});
    // Replies to an agent->client request that arrived through request().
    virtual void respond(qint64 id, const QJsonObject &result);
    virtual void respondError(qint64 id, int code, const QString &message);

signals:
    void started();
    void result(int reqId, bool ok, const QJsonValue &value, const QString &error);
    void notification(const QString &method, const QJsonObject &params);
    void request(qint64 id, const QString &method, const QJsonObject &params);
    void died(const QString &error);

protected:
    // One decoded line from the agent's stdout. The base class treats it as JSON-RPC; native clients override it
    // to translate another wire protocol into the same signals.
    virtual void dispatch(const QJsonObject &msg);
    void write(const QJsonObject &msg);

    QProcess *m_proc = nullptr;
    QByteArray m_buf;
    QString m_stderr;
    QString m_workDir;
    void recordWire(const char *dir, const QByteArray &line);
    QStringList m_wire;
    bool m_died = false;   // died() is emitted once per process (a crash triggers both errorOccurred and finished)
    int m_nextId = 1;
};
