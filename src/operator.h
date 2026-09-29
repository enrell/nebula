#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class AcpClient;
class ApiServer;

// The built-in agent: an ACP subprocess (claude-agent-acp / codex-acp / `gemini --acp`) that operates
// nebula itself through the "nebula" MCP server we inject into its session. It authenticates with
// whatever login the underlying CLI already has - subscriptions work, no API keys needed.
class Operator : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList transcript READ transcript NOTIFY transcriptChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantMap pendingConfirm READ pendingConfirm NOTIFY confirmChanged)
    Q_PROPERTY(QString agentName READ agentName NOTIFY agentNameChanged)
    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString connection READ connection NOTIFY connectionChanged)   // off | connecting | ready | error
    Q_PROPERTY(QString lastError READ lastError NOTIFY connectionChanged)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)          // [{value, name, description}] offered by the agent
    Q_PROPERTY(QString currentModel READ currentModel NOTIFY modelsChanged)
public:
    enum Approval { Ask, ApproveAll, DenyAll };

    explicit Operator(ApiServer *api, QObject *parent = nullptr);
    QVariantList transcript() const { return m_transcript; }
    bool busy() const { return m_busy; }
    QVariantMap pendingConfirm() const { return m_confirm; }
    QString agentName() const;
    QString connection() const;
    QString lastError() const { return m_lastError; }
    QVariantList models() const { return m_models; }
    QString currentModel() const { return m_currentModel; }
    QString status() const;   // human-readable current step while busy ("signing in…"), empty when idle

    bool available() const;                                 // an ACP agent command resolves
    Q_INVOKABLE QVariantList agentPresets() const;          // [{label, command, available}]
    Q_INVOKABLE void send(const QString &text, const QString &context = QString());
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void confirm(bool allow);
    Q_INVOKABLE void connectNow();                          // start the agent and open a session without a prompt
    Q_INVOKABLE void setModel(const QString &value);        // switch model (live if connected, saved either way)

    // Non-interactive entry point (API / scripts). Emits finished(id, text, error).
    int ask(const QString &prompt, Approval mode);

signals:
    void transcriptChanged();
    void busyChanged();
    void confirmChanged();
    void agentNameChanged();
    void availabilityChanged();
    void statusChanged();
    void connectionChanged();
    void modelsChanged();
    void finished(int id, const QString &text, const QString &error);

private:
    enum class Stage { Idle, Spawn, Initialize, Auth, NewSession, Configure, Prompt };

    void readyForPrompt();
    void setStage(Stage s);
    void armWatchdog();
    void start(const QString &text, Approval mode, int id);
    void beginAgent();
    void configureSession(const QJsonObject &sessionResult);
    void sendPrompt();
    void finish(const QString &text, const QString &error);
    void push(const QString &kind, const QString &text, const QString &detail = QString());
    void appendChunk(const QString &kind, const QString &chunk);
    void onAgentResult(int reqId, bool ok, const QJsonValue &value, const QString &error);
    void onAgentNotification(const QString &method, const QJsonObject &params);
    void onAgentRequest(qint64 id, const QString &method, const QJsonObject &params);
    void answerPermission(qint64 id, const QJsonArray &options, bool allow);
    QString agentCommand() const;
    QString instructions() const;
    QString snapshot() const;
    QString paneCwd() const;

    ApiServer *m_api;
    AcpClient *m_acp = nullptr;
    QString m_acpKind;                 // "acp" | "claude" | "codex": which client class m_acp is
    Stage m_stage = Stage::Idle;
    bool m_legacyModels = false;       // agent reports `models` + session/set_model instead of a model config option
    bool m_warm = false;               // connecting without a user prompt (connectNow)
    QString m_lastError;
    QVariantList m_models;
    QString m_currentModel, m_wantModel, m_pendingModel;
    int m_modelReqId = 0;
    QTimer *m_watchdog = nullptr;      // fires when a setup step (spawn/initialize/auth/session) gets no answer
    int m_setupId = 0;                 // in-flight initialize/session/new request id
    int m_promptId = 0;                // in-flight session/prompt request id
    qint64 m_permId = -1;              // in-flight session/request_permission request id
    QJsonArray m_permOptions;
    QJsonArray m_authMethods;
    bool m_authTried = false;
    QString m_session;
    bool m_primed = false;             // instructions already sent on this session
    QString m_pendingText;             // user text waiting for the session to be ready
    QString m_lastText;                // accumulated agent text this turn
    int m_lastAssistant = -1, m_lastThought = -1;   // transcript indices being streamed into
    QHash<QString, int> m_toolLines;   // toolCallId -> transcript index
    QVariantList m_transcript;
    QVariantMap m_confirm;
    Approval m_mode = Ask;
    int m_reqId = 0, m_nextId = 1;
    bool m_busy = false;
};
