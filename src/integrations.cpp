#include "integrations.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

const char *kMarker = "pane.report_state";
const QStringList kAgents = {"claude", "opencode", "codex", "gemini"};

QString home() { return QDir::homePath(); }
QString bin() { return QCoreApplication::applicationFilePath(); }

QString reportCmd(const QString &state) {
    return QString("sh -c '[ -n \"$NEBULA_PANE\" ] && \"${NEBULA_BIN:-nebula}\" ctl pane.report_state state=%1 ttl=3600 >/dev/null 2>&1; exit 0'").arg(state);
}

bool readText(const QString &path, QString *out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    *out = QString::fromUtf8(f.readAll());
    return true;
}

bool writeText(const QString &path, const QString &text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(text.toUtf8());
    return true;
}

void backupOnce(const QString &path) {
    const QString bak = path + ".nebula-bak";
    if (QFileInfo::exists(path) && !QFileInfo::exists(bak)) QFile::copy(path, bak);
}

// Load a JSON object from disk. Missing file -> empty object. Unparseable -> false (never clobber user files).
bool loadJson(const QString &path, QJsonObject *obj) {
    QString t;
    if (!readText(path, &t)) { *obj = {}; return true; }
    if (t.trimmed().isEmpty()) { *obj = {}; return true; }
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson(t.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !d.isObject()) return false;
    *obj = d.object();
    return true;
}

bool saveJson(const QString &path, const QJsonObject &obj) {
    backupOnce(path);
    return writeText(path, QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented)));
}

bool found(const QString &agent) { return !QStandardPaths::findExecutable(agent).isEmpty() || QFileInfo::exists(home() + "/." + agent) ; }

// ---- paths
QString claudeSettings() { return home() + "/.claude/settings.json"; }
QString opencodePlugin() { return home() + "/.config/opencode/plugins/nebula.js"; }
QString opencodeConfig() { return home() + "/.config/opencode/opencode.json"; }
QString codexConfig() { return home() + "/.codex/config.toml"; }
QString geminiSettings() { return home() + "/.gemini/settings.json"; }

const QList<QPair<QString, QString>> kClaudeEvents = {{"UserPromptSubmit", "working"}, {"PreToolUse", "working"}, {"Notification", "blocked"},
                                                       {"Stop", "idle"}, {"SessionEnd", ""}};

bool isOurs(const QJsonValue &entry) {
    for (const QJsonValue &h : entry.toObject()["hooks"].toArray())
        if (h.toObject()["command"].toString().contains(kMarker)) return true;
    return false;
}

QJsonArray withoutOurs(const QJsonArray &arr) {
    QJsonArray out;
    for (const QJsonValue &e : arr)
        if (!isOurs(e)) out << e;
    return out;
}

bool hooksInstalled(const QString &agent) {
    QString t;
    if (agent == "claude") {
        QJsonObject o;
        if (!loadJson(claudeSettings(), &o)) return false;
        for (const QJsonValue &e : o["hooks"].toObject()["Stop"].toArray())
            if (isOurs(e)) return true;
        return false;
    }
    if (agent == "opencode") return readText(opencodePlugin(), &t) && t.contains("managed by nebula");
    if (agent == "codex") return readText(codexConfig(), &t) && t.contains(QRegularExpression(QString("^notify\\s*=.*%1").arg(kMarker), QRegularExpression::MultilineOption));
    return false;
}

bool mcpInstalled(const QString &agent) {
    QString t;
    QJsonObject o;
    if (agent == "claude") {
        QString cfg = home() + "/.claude.json";
        return loadJson(cfg, &o) && o["mcpServers"].toObject().contains("nebula");
    }
    if (agent == "opencode") return loadJson(opencodeConfig(), &o) && o["mcp"].toObject().contains("nebula");
    if (agent == "gemini") return loadJson(geminiSettings(), &o) && o["mcpServers"].toObject().contains("nebula");
    if (agent == "codex") return readText(codexConfig(), &t) && t.contains("[mcp_servers.nebula]");
    return false;
}

QString jsonPathError(const QString &p) { return p + " is not plain JSON; edit it by hand (see README)"; }

} // namespace

Integrations::Integrations(QObject *parent) : QObject(parent) { s_instance = this; }

QVariantList Integrations::list() const {
    QVariantList l;
    for (const QString &a : kAgents) {
        const bool hookSupport = a != "gemini";
        QString note;
        if (a == "codex") note = "hooks: turn completion only (working/blocked stay heuristic)";
        if (a == "gemini") note = "no hook support upstream; state uses screen heuristics";
        l << QVariantMap{{"id", a}, {"found", found(a)}, {"hookSupport", hookSupport}, {"hooks", hooksInstalled(a)}, {"mcp", mcpInstalled(a)}, {"note", note}};
    }
    return l;
}

QString Integrations::install(const QString &agent, const QString &kind) {
    QString err;
    if (kind == "hooks") {
        if (agent == "claude") {
            QJsonObject o;
            if (!loadJson(claudeSettings(), &o)) return jsonPathError(claudeSettings());
            QJsonObject hooks = o["hooks"].toObject();
            for (const auto &ev : kClaudeEvents) {
                QJsonArray arr = withoutOurs(hooks[ev.first].toArray());
                arr << QJsonObject{{"hooks", QJsonArray{QJsonObject{{"type", "command"}, {"command", reportCmd(ev.second)}}}}};
                hooks[ev.first] = arr;
            }
            o["hooks"] = hooks;
            if (!saveJson(claudeSettings(), o)) err = "cannot write " + claudeSettings();
        } else if (agent == "opencode") {
            const QString js = R"(// managed by nebula - reports agent state to the nebula sidebar
import { spawn } from "node:child_process"

const report = (state) => {
  if (!process.env.NEBULA_PANE) return
  try {
    spawn(process.env.NEBULA_BIN || "nebula", ["ctl", "pane.report_state", "state=" + state, "ttl=3600"], { stdio: "ignore", detached: true }).unref()
  } catch {}
}

export const NebulaPlugin = async () => ({
  event: async ({ event }) => {
    switch (event.type) {
      case "session.idle": case "session.error": report("idle"); break
      case "permission.asked": report("blocked"); break
      case "permission.replied": report("working"); break
      case "session.status": if (event.properties?.status?.type === "busy") report("working"); break
    }
  },
  "tool.execute.before": async () => report("working"),
})
)";
            if (!writeText(opencodePlugin(), js)) err = "cannot write " + opencodePlugin();
        } else if (agent == "codex") {
            QString t;
            readText(codexConfig(), &t);
            static const QRegularExpression notify("^notify\\s*=", QRegularExpression::MultilineOption);
            if (notify.match(t).hasMatch() && !t.contains(kMarker)) return "codex already has a custom `notify` in config.toml; not overriding it";
            t.remove(QRegularExpression(QString("^notify\\s*=.*%1.*\\n").arg(kMarker), QRegularExpression::MultilineOption));
            const QString shCmd = "[ -n \\\"$NEBULA_PANE\\\" ] && \\\"${NEBULA_BIN:-nebula}\\\" ctl pane.report_state state=idle ttl=3600 >/dev/null 2>&1; exit 0";
            const QString entry = QString("notify = [\"sh\", \"-c\", \"%1\"]\n").arg(shCmd);
            const int firstTable = t.indexOf(QRegularExpression("^\\[", QRegularExpression::MultilineOption));
            if (firstTable < 0) t += (t.isEmpty() || t.endsWith('\n') ? "" : "\n") + entry;
            else t.insert(firstTable, entry + "\n");
            backupOnce(codexConfig());
            if (!writeText(codexConfig(), t)) err = "cannot write " + codexConfig();
        } else return "no hook support for " + agent;
    } else if (kind == "mcp") {
        if (agent == "claude") {
            QProcess p;
            p.start("claude", {"mcp", "add", "--scope", "user", "nebula", "--", bin(), "mcp"});
            if (!p.waitForFinished(20000) || p.exitCode() != 0) err = "`claude mcp add` failed: " + QString::fromUtf8(p.readAllStandardError()).trimmed();
        } else if (agent == "opencode" || agent == "gemini") {
            const QString path = agent == "opencode" ? opencodeConfig() : geminiSettings();
            QJsonObject o;
            if (!loadJson(path, &o)) return jsonPathError(path);
            if (agent == "opencode") {
                if (!o.contains("$schema")) o["$schema"] = "https://opencode.ai/config.json";
                QJsonObject m = o["mcp"].toObject();
                m["nebula"] = QJsonObject{{"type", "local"}, {"command", QJsonArray{bin(), "mcp"}}, {"enabled", true}};
                o["mcp"] = m;
            } else {
                QJsonObject m = o["mcpServers"].toObject();
                m["nebula"] = QJsonObject{{"command", bin()}, {"args", QJsonArray{"mcp"}}};
                o["mcpServers"] = m;
            }
            if (!saveJson(path, o)) err = "cannot write " + path;
        } else if (agent == "codex") {
            QString t;
            readText(codexConfig(), &t);
            if (!t.contains("[mcp_servers.nebula]")) {
                t += QString("%1# managed by nebula (mcp)\n[mcp_servers.nebula]\ncommand = \"%2\"\nargs = [\"mcp\"]\n").arg(t.isEmpty() || t.endsWith("\n\n") ? "" : "\n", bin());
                backupOnce(codexConfig());
                if (!writeText(codexConfig(), t)) err = "cannot write " + codexConfig();
            }
        }
    } else return "unknown kind";
    emit changed();
    return err;
}

QString Integrations::remove(const QString &agent, const QString &kind) {
    QString err;
    if (kind == "hooks") {
        if (agent == "claude") {
            QJsonObject o;
            if (!loadJson(claudeSettings(), &o)) return jsonPathError(claudeSettings());
            QJsonObject hooks = o["hooks"].toObject();
            for (const QString &k : hooks.keys()) {
                const QJsonArray arr = withoutOurs(hooks[k].toArray());
                if (arr.isEmpty()) hooks.remove(k); else hooks[k] = arr;
            }
            if (hooks.isEmpty()) o.remove("hooks"); else o["hooks"] = hooks;
            if (!saveJson(claudeSettings(), o)) err = "cannot write " + claudeSettings();
        } else if (agent == "opencode") {
            QFile::remove(opencodePlugin());
        } else if (agent == "codex") {
            QString t;
            if (readText(codexConfig(), &t)) {
                t.remove(QRegularExpression(QString("^notify\\s*=.*%1.*\\n\\n?").arg(kMarker), QRegularExpression::MultilineOption));
                writeText(codexConfig(), t);
            }
        }
    } else if (kind == "mcp") {
        if (agent == "claude") {
            QProcess p;
            p.start("claude", {"mcp", "remove", "--scope", "user", "nebula"});
            p.waitForFinished(20000);
        } else if (agent == "opencode" || agent == "gemini") {
            const QString path = agent == "opencode" ? opencodeConfig() : geminiSettings();
            QJsonObject o;
            if (!loadJson(path, &o)) return jsonPathError(path);
            const QString k = agent == "opencode" ? "mcp" : "mcpServers";
            QJsonObject m = o[k].toObject();
            m.remove("nebula");
            if (m.isEmpty()) o.remove(k); else o[k] = m;
            if (!saveJson(path, o)) err = "cannot write " + path;
        } else if (agent == "codex") {
            QString t;
            if (readText(codexConfig(), &t)) {
                t.remove(QRegularExpression("\\n?# managed by nebula \\(mcp\\)\\n\\[mcp_servers\\.nebula\\]\\n(?:[^\\[\\n][^\\n]*\\n?)*"));
                writeText(codexConfig(), t);
            }
        }
    } else return "unknown kind";
    emit changed();
    return err;
}
