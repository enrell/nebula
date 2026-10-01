#include "launcher.h"
#include "paths.h"
#include "profiles.h"
#include "terminalsession.h"
#include "workspace.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

Launcher::Launcher(Workspace *ws, QObject *parent) : QObject(parent), m_ws(ws) {
    connect(ws, &Workspace::agentEvent, this, [this](const QJsonObject &ev) {
        const int pane = ev["pane"].toInt();
        if (!m_pending.contains(pane) || ev["state"].toString() != "idle") return;
        const QString prompt = m_pending.take(pane);
        QTimer::singleShot(1500, this, [this, pane, prompt] {
            if (auto *s = m_ws->findPane(pane)) {
                s->paste(prompt);
                QTimer::singleShot(150, s, [s] { s->injectKey(Qt::Key_Return, 0, "\r"); });
            }
        });
    });
}

QList<Launcher::Preset> Launcher::allPresets() const {
    QList<Preset> p = {
        {"claude", "Claude Code", "claude", "--model", "arg"},
        {"codex", "Codex", "codex", "--model", "arg"},
        {"opencode", "opencode", "opencode", "--model", "type"},
        {"gemini", "Gemini CLI", "gemini", "--model", "type"},
        {"agy", "Antigravity (agy)", "agy", "--model", "flag:-i"},
        {"aider", "aider", "aider", "--model", "type"},
        {"shell", "Plain shell", "", "", "type"},
    };
    QFile f(Paths::configDir() + "/agents.conf");
    if (f.open(QIODevice::ReadOnly)) {
        for (const QByteArray &raw : f.readAll().split('\n')) {
            const QString line = QString::fromUtf8(raw).trimmed();
            const int eq = line.indexOf('=');
            if (line.isEmpty() || line.startsWith('#') || eq <= 0) continue;
            const QString name = line.left(eq).trimmed();
            p.append({name, name, line.mid(eq + 1).trimmed(), "--model", "type"});
        }
    }
    return p;
}

QVariantList Launcher::presets() const {
    QVariantList l;
    for (const Preset &p : allPresets()) {
        const QString exe = p.command.section(' ', 0, 0);
        l << QVariantMap{{"id", p.id}, {"label", p.label}, {"command", p.command},
                         {"available", exe.isEmpty() || !QStandardPaths::findExecutable(exe).isEmpty()}};
    }
    return l;
}

QString Launcher::currentCwd() const {
    auto *p = m_ws->focusedPane();
    return p && !p->cwd().isEmpty() ? p->cwd() : QDir::homePath();
}

static QString shq(const QString &s) {
    QString r = s;
    r.replace("'", "'\\''");
    return "'" + r + "'";
}

static bool git(const QString &dir, const QStringList &args, QString *out = nullptr) {
    QProcess p;
    p.setWorkingDirectory(dir);
    p.start("git", args);
    if (!p.waitForFinished(20000)) return false;
    if (out) *out = QString::fromUtf8(p.readAllStandardOutput()).trimmed() + QString::fromUtf8(p.readAllStandardError()).trimmed();
    return p.exitCode() == 0;
}

QVariantMap Launcher::launch(const QVariantMap &o) {
    auto err = [](const QString &m) { return QVariantMap{{"ok", false}, {"error", m}}; };
    const QString agent = o["agent"].toString().isEmpty() ? "shell" : o["agent"].toString();
    Preset pre{agent, agent, agent, "--model", "type"};
    for (const Preset &p : allPresets())
        if (p.id == agent) pre = p;

    QString cwd = o["cwd"].toString().trimmed();
    if (cwd.isEmpty()) cwd = currentCwd();
    if (cwd.startsWith("~")) cwd = QDir::homePath() + cwd.mid(1);
    if (!QFileInfo(cwd).isDir()) return err("directory does not exist: " + cwd);

    const QString profile = o["profile"].toString();
    if (!profile.isEmpty() && profile != "none" && !Profiles::instance()->exists(profile)) return err("unknown profile: " + profile);

    QString spaceName;
    if (o["worktree"].toBool()) {
        QString root;
        if (!git(cwd, {"rev-parse", "--show-toplevel"}, &root)) return err("not inside a git repository: " + cwd);
        // launched from inside a worktree (e.g. the pane of an agent started this way): branch off the main checkout,
        // so parallel agents get sibling worktrees instead of repo-a-b chains built on each other's branches
        QString common;
        if (git(cwd, {"rev-parse", "--path-format=absolute", "--git-common-dir"}, &common) && QFileInfo(common).fileName() == ".git")
            root = QFileInfo(common).absolutePath();
        QString branch = o["branch"].toString().trimmed();
        if (branch.isEmpty()) branch = agent + "-" + QString::number(QDateTime::currentSecsSinceEpoch() % 100000);
        branch.replace(QRegularExpression("[^A-Za-z0-9._/-]"), "-");
        const QString path = root + "-" + QString(branch).replace('/', '-');
        QString out;
        if (!git(root, {"worktree", "add", "-b", branch, path}, &out) && !git(root, {"worktree", "add", path, branch}, &out)) return err("git worktree failed: " + out);
        cwd = path;
        spaceName = branch;
    }

    Space *space = m_ws->currentSpace();
    Tab *tab = space ? space->currentTab() : nullptr;
    QString where = o["where"].toString();
    if (where.isEmpty()) where = "split-right";
    int pane = -1;
    if (where == "space" || !tab) {
        m_ws->newSpace(cwd, spaceName.isEmpty() ? (agent == "shell" ? QString() : agent) : spaceName, profile);
        pane = m_ws->currentSpace()->currentTab()->focusedId();
    } else if (where == "tab") {
        pane = space->newTabWith(cwd, profile);
    } else {
        pane = tab->splitWith(where != "split-down", cwd, profile);
    }
    if (pane < 0) return err("could not create a pane");

    QString cmd = pre.command;
    if (!cmd.isEmpty()) {
        const QString model = o["model"].toString().trimmed();
        if (!model.isEmpty() && !pre.modelFlag.isEmpty()) cmd += " " + pre.modelFlag + " " + shq(model);
        if (!o["args"].toString().trimmed().isEmpty()) cmd += " " + o["args"].toString().trimmed();
    }
    const QString prompt = o["prompt"].toString();
    if (!prompt.isEmpty()) {
        if (pre.command.isEmpty() || pre.promptMode == "type") queuePrompt(pane, prompt);
        else if (pre.promptMode.startsWith("flag:")) cmd += " " + pre.promptMode.mid(5) + " " + shq(prompt);   // e.g. agy -i 'prompt'
        else cmd += " " + shq(prompt);
    }
    if (!cmd.isEmpty()) {
        QPointer<TerminalSession> s = m_ws->findPane(pane);
        QTimer::singleShot(300, this, [s, cmd] {
            if (!s) return;
            s->sendText(cmd);
            s->injectKey(Qt::Key_Return, 0, "\r");
        });
    }
    return {{"ok", true}, {"pane", pane}, {"cwd", cwd}};
}

void Launcher::queuePrompt(int pane, const QString &prompt) {
    m_pending[pane] = prompt;
    QTimer::singleShot(45000, this, [this, pane] { m_pending.remove(pane); });
}

int Launcher::broadcast(const QVariantList &panes, const QString &text, bool enter) {
    int n = 0;
    for (const QVariant &v : panes) {
        auto *s = m_ws->findPane(v.toInt());
        if (!s) continue;
        s->paste(text);
        if (enter) QTimer::singleShot(150, s, [s] { s->injectKey(Qt::Key_Return, 0, "\r"); });
        ++n;
    }
    return n;
}
