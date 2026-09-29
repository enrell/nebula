#pragma once
#include "acp.h"
#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <functional>

// Drivers for agent CLIs that speak their own protocol instead of ACP. Each one translates that protocol into the
// AcpClient surface the Operator already uses (initialize / session/new / session/prompt / session/update
// notifications / session/request_permission), so no adapter package (npm) is needed.

// `claude -p --input-format stream-json --output-format stream-json`: one long-lived process per session.
class ClaudeNativeClient : public AcpClient {
    Q_OBJECT
public:
    using AcpClient::AcpClient;
    void start(const QString &program, const QStringList &args) override;
    void stop() override;
    bool running() const override { return m_alive; }
    int call(const QString &method, const QJsonObject &params = {}) override;
    void notify(const QString &method, const QJsonObject &params = {}) override;
    void respond(qint64 id, const QJsonObject &result) override;
    void respondError(qint64 id, int code, const QString &message) override;

protected:
    void dispatch(const QJsonObject &msg) override;

private:
    void later(int id, bool ok, const QJsonValue &value, const QString &error = {});
    void spawn(int id, const QJsonObject &params);
    void control(const QString &requestId, const QJsonObject &request);
    void toolStarted(const QJsonObject &block);

    bool m_alive = false, m_hooked = false;
    QString m_session;
    int m_newId = 0, m_promptId = 0;
    int m_ctlSeq = 0, m_permSeq = 1000;
    QHash<QString, int> m_ctlPending;                       // control request_id -> operator call id
    struct Perm { QString requestId; QJsonObject input; };
    QHash<qint64, Perm> m_perms;
    QSet<QString> m_streamedMsgs, m_seenTools;
    QString m_curMsg;
};

// `codex app-server`: JSON-RPC (no "jsonrpc" field) with threads and turns.
class CodexNativeClient : public AcpClient {
    Q_OBJECT
public:
    using AcpClient::AcpClient;
    void start(const QString &program, const QStringList &args) override;
    bool running() const override { return AcpClient::running(); }
    int call(const QString &method, const QJsonObject &params = {}) override;
    void notify(const QString &method, const QJsonObject &params = {}) override;
    void respond(qint64 id, const QJsonObject &result) override;
    void respondError(qint64 id, int code, const QString &message) override;

protected:
    void dispatch(const QJsonObject &msg) override;

private:
    using Cb = std::function<void(bool ok, const QJsonValue &value, const QString &error)>;
    void rpc(const QString &method, const QJsonObject &params, Cb cb);
    void later(int id, bool ok, const QJsonValue &value, const QString &error = {});
    void update(const QJsonObject &u) { emit notification("session/update", {{"sessionId", m_thread}, {"update", u}}); }
    void itemStarted(const QJsonObject &item);
    void askPermission(const QJsonValue &rawId, const QString &kind, const QString &title, const QJsonObject &raw, const QJsonObject &params);

    int m_cid = 1;                       // codex-side request ids
    QHash<int, Cb> m_pend;
    QString m_thread, m_turn, m_model, m_defaultModel, m_lastError;
    QJsonArray m_models;
    int m_promptId = 0;
    bool m_hadMessage = false;
    int m_permSeq = 1000;
    struct Perm { QJsonValue rawId; QString kind; QJsonObject params; };
    QHash<qint64, Perm> m_perms;
};
