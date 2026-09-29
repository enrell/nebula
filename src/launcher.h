#pragma once
#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Workspace;

// Starts agent CLIs in new panes (optionally in a fresh git worktree, with a credential profile) and fans prompts out.
class Launcher : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
public:
    explicit Launcher(Workspace *ws, QObject *parent = nullptr);
    QVariantList presets() const;

    // opts: agent, profile, model, cwd, worktree(bool), branch, prompt, args, where(split-right|split-down|tab|space)
    // returns {ok, pane, error}
    Q_INVOKABLE QVariantMap launch(const QVariantMap &opts);
    Q_INVOKABLE int broadcast(const QVariantList &panes, const QString &text, bool enter = true);
    Q_INVOKABLE QString currentCwd() const;

private:
    struct Preset { QString id, label, command, modelFlag, promptMode; };
    QList<Preset> allPresets() const;
    void queuePrompt(int pane, const QString &prompt);

    Workspace *m_ws;
    QHash<int, QString> m_pending;
};
