#pragma once
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>

class Workspace;
class Launcher;
class Operator;
class QLocalSocket;
class TerminalSession;

// Newline-delimited JSON over a unix socket.
//   request : {"id": 1, "method": "pane.list", "params": {...}}
//   response: {"id": 1, "ok": true, "result": ...} | {"id": 1, "ok": false, "error": "..."}
//   events  : {"event": "agent.state", ...}   (after "events.subscribe")
class ApiServer : public QObject {
    Q_OBJECT
public:
    ApiServer(Workspace *ws, Launcher *launcher, QObject *parent = nullptr);
    ~ApiServer() override;
    bool listen(const QString &path);
    void setOperator(Operator *op) { m_operator = op; }
    // In-process access to the automation API (throws on error); used by the built-in operator.
    QJsonValue invoke(const QString &method, const QJsonObject &params) { return call(method, params, nullptr); }
    QString path() const { return m_server.fullServerName(); }

private:
    struct Client { QByteArray buf; bool subscribed = false; };
    void onConnection();
    void onData(QLocalSocket *sock);
    QJsonObject handle(const QJsonObject &req, QLocalSocket *sock);
    QJsonValue call(const QString &method, const QJsonObject &p, QLocalSocket *sock);
    TerminalSession *pane(const QJsonObject &p) const;
    QJsonObject paneInfo(TerminalSession *s) const;
    void broadcast(const QJsonObject &ev);

    void startAsync(const QJsonObject &req, QLocalSocket *sock);
    void replyLater(QLocalSocket *sock, const QJsonValue &reqId, int llmId, bool wantText);

    Workspace *m_ws;
    Launcher *m_launcher;
    Operator *m_operator = nullptr;
    QLocalServer m_server;
    QHash<QLocalSocket *, Client> m_clients;
};
