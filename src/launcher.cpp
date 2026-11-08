#include "launcher.h"
#include "isolation.h"
#include "platform.h"
#include "paths.h"
#include "profiles.h"
#include "settings.h"
#include "silo.h"
#include "terminalsession.h"
#include "workspace.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
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

// one argument for the shell the panes run (POSIX shells and PowerShell take 'single quotes', cmd.exe "double quotes")
static QString shq(const QString &s) { return Platform::quote(s); }

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

    // worktree, or silo: the agent works inside a microVM on a private clone of that worktree, and its changes reach
    // the worktree's branch only through silo apply. The setting decides for launches that just ask for a worktree.
    const QString requested = o["isolation"].toString().trimmed();
    const QString setting = Settings::instance() ? Settings::instance()->isolation() : QString("worktree");
    const bool mayUseSilo = requested == "silo" || requested == "auto" || (requested.isEmpty() && o["worktree"].toBool() && setting != "worktree");
    const Isolation::Choice choice = Isolation::choose(requested.toStdString(), o["worktree"].toBool(), setting.toStdString(),
                                                       mayUseSilo ? Silo::probe() : Isolation::Probe{});
    if (!choice.error.empty()) return err(QString::fromStdString(choice.error));

    QString spaceName;
    if (choice.kind != Isolation::Kind::None) {
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
    QString session;
    if (choice.kind == Isolation::Kind::Silo) {
        // `silo run ... -- <the agent's command>` in the pane: the agent's TUI, its permission prompts and nebula's
        // keys all pass through silo's pty. Profile secrets are forwarded by name, so no value is typed into the shell.
        session = QString::fromStdString(Isolation::sessionName(spaceName.toStdString()));
        std::vector<std::string> env;
        if (TerminalSession *s = m_ws->findPane(pane))
            for (const QString &e : Profiles::instance()->envFor(s->profile())) env.push_back(e.toStdString());
        std::vector<std::string> domains;
        if (o.contains("domains")) {
            const QVariant d = o["domains"];
            const QStringList list = d.typeId() == QMetaType::QString ? d.toString().split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts)
                                                                      : d.toStringList();
            for (const QString &h : list) domains.push_back(h.trimmed().toStdString());
        } else {
            domains = Isolation::defaultDomains(pre.id.toStdString());
        }
        QStringList silo{shq(Silo::program())};
        for (const std::string &a : Isolation::runArguments({session.toStdString(), domains, Isolation::forwardedNames(env)}))
            silo << shq(QString::fromStdString(a));
        cmd = silo.join(' ') + " " + (cmd.isEmpty() ? QString("/bin/sh") : cmd);
        m_sessions[pane] = session;
    }
    if (!cmd.isEmpty()) {
        QPointer<TerminalSession> s = m_ws->findPane(pane);
        QTimer::singleShot(300, this, [s, cmd] {
            if (!s) return;
            s->sendText(cmd);
            s->injectKey(Qt::Key_Return, 0, "\r");
        });
    }
    QVariantMap r{{"ok", true}, {"pane", pane}, {"cwd", cwd}, {"isolation", QString::fromLatin1(Isolation::name(choice.kind))}};
    if (!session.isEmpty()) r["session"] = session;
    if (!choice.note.empty()) r["note"] = QString::fromStdString(choice.note);
    return r;
}

QVariantMap Launcher::isolationInfo() const {
    const Isolation::Probe p = Silo::probe();
    return {{"setting", Settings::instance() ? Settings::instance()->isolation() : QString("worktree")},
            {"silo", QVariantMap{{"available", Isolation::available(p)}, {"program", Silo::program()},
                                 {"reason", QString::fromStdString(Isolation::unavailableReason(p))}}}};
}

// The silo session nebula launched in a pane; after a restart of the window, the newest open session whose project is
// the pane's directory (every silo launch runs from its own worktree, so that directory names one session).
QString Launcher::paneSession(int pane) const {
    if (m_sessions.contains(pane)) return m_sessions.value(pane);
    TerminalSession *s = m_ws->findPane(pane);
    const QString cwd = s ? QFileInfo(s->cwd()).canonicalFilePath() : QString();
    if (cwd.isEmpty()) return {};
    const Silo::Result r = Silo::list();
    for (const QJsonValue &v : r.value.toArray()) {   // newest first
        const Isolation::Phase ph = Isolation::phase(v["status"].toString().toStdString());
        if (ph != Isolation::Phase::Closed && QFileInfo(v["projectRoot"].toString()).canonicalFilePath() == cwd) return v["name"].toString();
    }
    return {};
}

QVariantMap Launcher::siloSession(const QString &action, int pane, const QString &name, const QVariantMap &opts) {
    auto err = [](const QString &m, const QString &code = QString()) {
        QVariantMap e{{"ok", false}, {"error", m}};
        if (!code.isEmpty()) e["code"] = code;
        return e;
    };
    const QString session = name.isEmpty() ? paneSession(pane) : name;
    if (session.isEmpty()) return err(QString("pane %1 has no silo session").arg(pane));
    Silo::Result r;
    if (action == "status") r = Silo::status(session);
    else if (action == "diff") r = Silo::diff(session, opts["stat_only"].toBool());
    else if (action == "apply") r = Silo::apply(session);
    else if (action == "discard") r = Silo::discard(session, opts["force"].toBool());
    else return err("unknown session action: " + action);
    if (!r.ok) {
        const QString hint = QString::fromStdString(Isolation::hint(r.code.toStdString()));
        QVariantMap e = err(QString("silo %1 %2: %3%4").arg(action, session, r.message, hint.isEmpty() ? QString() : " (" + hint + ")"), r.code);
        if (!hint.isEmpty()) e["hint"] = hint;
        return e;
    }
    // silo's JSON as it is, plus the session's name and phase: diff wraps the record ({session, hostSafe, patch, ...}),
    // the other commands print the record itself
    QVariantMap out = r.value.toObject().toVariantMap();
    const QJsonObject record = action == "diff" ? r.value["session"].toObject() : r.value.toObject();
    out["ok"] = true;
    out["name"] = record["name"].toString(session);
    out["phase"] = QString::fromLatin1(Isolation::name(Isolation::phase(record["status"].toString().toStdString())));
    return out;
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
