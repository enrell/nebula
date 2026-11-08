#pragma once
#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Workspace;

// Starts agent CLIs in new panes (optionally in a fresh git worktree, or inside a silo microVM session started from one,
// with a credential profile), fans prompts out, and reviews silo sessions (docs/isolation.md).
class Launcher : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
public:
    explicit Launcher(Workspace *ws, QObject *parent = nullptr);
    QVariantList presets() const;

    // opts: agent, profile, model, cwd, worktree(bool), branch, prompt, args, where(split-right|split-down|tab|space),
    //       isolation(none|worktree|silo|auto; unset: worktree=true follows the isolation setting), domains (silo's allowlist)
    // returns {ok, pane, cwd, isolation, session (silo), note, error}
    Q_INVOKABLE QVariantMap launch(const QVariantMap &opts);
    // A silo session, named or the one of a pane: action status | diff | apply | discard; opts: stat_only, force.
    // returns silo's JSON plus {ok, name, phase}, or {ok: false, error, code, hint}
    Q_INVOKABLE QVariantMap siloSession(const QString &action, int pane, const QString &session, const QVariantMap &opts = {});
    Q_INVOKABLE QVariantMap isolationInfo() const;   // {setting, silo: {available, reason}}
    Q_INVOKABLE int broadcast(const QVariantList &panes, const QString &text, bool enter = true);
    Q_INVOKABLE QString currentCwd() const;

private:
    struct Preset { QString id, label, command, modelFlag, promptMode; };
    QList<Preset> allPresets() const;
    void queuePrompt(int pane, const QString &prompt);
    QString paneSession(int pane) const;

    Workspace *m_ws;
    QHash<int, QString> m_pending;
    QHash<int, QString> m_sessions;   // pane -> the silo session launched in it
};
