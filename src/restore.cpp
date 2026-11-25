#include "restore.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace Restore {

QString posixQuote(const QString &arg) {
    static const QRegularExpression safe(R"(^[A-Za-z0-9_@%+=:,./-]+$)");
    if (!arg.isEmpty() && safe.match(arg).hasMatch()) return arg;
    QString q = arg;
    q.replace('\'', "'\\''");
    return '\'' + q + '\'';
}

namespace {

QString base(const QString &program) {
    QString b = QFileInfo(program).fileName();
    if (b.endsWith(".exe", Qt::CaseInsensitive)) b.chop(4);
    return b;
}

// argv without the interpreter of a script-based CLI: [node, /path/cli.js, --flag] -> [--flag]
QStringList toolArgs(const QStringList &args) {
    static const QSet<QString> interpreters = {"node", "bun", "deno", "python", "python3"};
    if (args.size() >= 2 && interpreters.contains(base(args[0]))) return args.mid(2);
    return args.mid(1);
}

// Flags that take a value as the next word, per agent; any other bare word is a positional (an initial prompt) and
// is not repeated, or the resumed agent would receive that prompt again.
const QSet<QString> &valued(const QString &agent) {
    static const QSet<QString> claude = {"--model", "--permission-mode", "--allowedTools", "--allowed-tools", "--disallowedTools",
        "--disallowed-tools", "--add-dir", "--mcp-config", "--append-system-prompt", "--system-prompt", "--settings",
        "--agents", "--agent", "--fallback-model", "--setting-sources", "--plugin-dir", "--betas", "--debug-file"};
    static const QSet<QString> codex = {"-m", "--model", "-c", "--config", "-p", "--profile", "-s", "--sandbox",
        "-a", "--ask-for-approval", "-C", "--cd", "--add-dir", "-i", "--image", "--enable", "--disable"};
    static const QSet<QString> opencode = {"-m", "--model", "--agent", "--port", "--hostname", "--prompt", "--log-level"};
    static const QSet<QString> none;
    if (agent == "claude") return claude;
    if (agent == "codex") return codex;
    if (agent == "opencode") return opencode;
    return none;
}

// Keeps the flags (and their values) of `args`, dropping positionals and the flags listed in `drop` (with the
// value of those that take one).
QStringList keepFlags(const QString &agent, const QStringList &args, const QSet<QString> &drop, const QSet<QString> &dropValued) {
    QStringList out;
    const QSet<QString> &v = valued(agent);
    for (int i = 0; i < args.size(); ++i) {
        const QString &a = args[i];
        const QString name = a.section('=', 0, 0);
        if (dropValued.contains(name)) { if (!a.contains('=') && i + 1 < args.size()) ++i; continue; }
        if (drop.contains(a)) continue;
        if (!a.startsWith('-')) continue;   // a positional: the initial prompt or a subcommand
        out << a;
        if (!a.contains('=') && v.contains(a) && i + 1 < args.size()) out << args[++i];
    }
    return out;
}

QString join(const QStringList &words, const Quote &quote) {
    QStringList q;
    for (const QString &w : words) q << quote(w);
    return q.join(' ');
}

} // namespace

QString sessionFromArgs(const QString &agent, const QStringList &args) {
    const QStringList a = toolArgs(args);
    auto valueOf = [&](const QSet<QString> &names) -> QString {
        for (int i = 0; i < a.size(); ++i) {
            const QString name = a[i].section('=', 0, 0);
            if (!names.contains(name)) continue;
            if (a[i].contains('=')) return a[i].section('=', 1);
            if (i + 1 < a.size() && !a[i + 1].startsWith('-')) return a[i + 1];
        }
        return {};
    };
    if (agent == "claude") return valueOf({"--resume", "-r", "--session-id"});
    if (agent == "opencode") return valueOf({"--session", "-s"});
    if (agent == "codex") {
        const int r = int(a.indexOf("resume"));
        if (r >= 0 && r + 1 < a.size() && !a[r + 1].startsWith('-')) return a[r + 1];
    }
    return {};
}

QString sessionFromPayload(const QByteArray &json) {
    const QJsonObject o = QJsonDocument::fromJson(json.trimmed()).object();
    for (const char *k : {"session_id", "sessionID", "sessionId", "thread-id", "thread_id", "threadId", "conversation_id"}) {
        const QString v = o.value(QLatin1String(k)).toString();
        static const QRegularExpression ok(R"(^[A-Za-z0-9_.:-]{1,128}$)");
        if (!v.isEmpty() && ok.match(v).hasMatch()) return v;
    }
    return {};
}

bool interactive(const QStringList &args) {
    if (args.isEmpty()) return false;
    const QString p = base(args[0]);
    static const QSet<QString> always = {
        "vim", "nvim", "vi", "hx", "helix", "nano", "micro", "emacs", "kak", "less", "more", "most", "man",
        "htop", "btop", "top", "atop", "nvtop", "btm", "glances", "iotop", "bandwhich",
        "yazi", "ranger", "lf", "nnn", "mc", "broot", "superfile", "spf",
        "lazygit", "tig", "gitui", "lazydocker", "ctop", "k9s",
        "psql", "pgcli", "mysql", "mariadb", "mycli", "sqlite3", "litecli", "usql", "redis-cli", "mongosh",
        "ssh", "mosh", "neomutt", "mutt", "aerc", "weechat", "irssi", "cmus", "ncmpcpp", "newsboat",
        "watch", "journalctl", "ncdu", "gdu", "bluetuith", "impala", "wiremix", "pulsemixer", "nmtui"};
    // a REPL is interactive; the same program running a script is a job
    static const QSet<QString> repls = {"python", "python3", "ipython", "node", "bun", "deno", "lua", "ghci", "irb", "iex", "julia", "R"};
    if (p == "tail") return args.contains("-f") || args.contains("-F") || args.contains("--follow");
    if (p == "journalctl") return true;
    if (always.contains(p)) return true;
    if (repls.contains(p)) {
        for (int i = 1; i < args.size(); ++i)
            if (!args[i].startsWith('-')) return false;
        return true;
    }
    return false;
}

Plan plan(const QString &agent, const QStringList &args, const QString &session, const Quote &quote) {
    if (args.isEmpty()) return {};
    const QStringList a = toolArgs(args);
    const QString sid = session.isEmpty() ? sessionFromArgs(agent, args) : session;
    if (agent == "claude") {
        QStringList cmd{"claude"};
        cmd << keepFlags(agent, a, {"-c", "--continue", "-p", "--print"}, {"--resume", "-r", "--session-id"});
        cmd << (sid.isEmpty() ? QStringList{"--continue"} : QStringList{"--resume", sid});
        return {join(cmd, quote), true};
    }
    if (agent == "opencode") {
        QStringList cmd{"opencode"};
        cmd << keepFlags(agent, a, {"-c", "--continue"}, {"--session", "-s"});
        cmd << (sid.isEmpty() ? QStringList{"--continue"} : QStringList{"--session", sid});
        return {join(cmd, quote), true};
    }
    if (agent == "codex") {
        QStringList cmd{"codex", "resume", sid.isEmpty() ? "--last" : sid};
        cmd << keepFlags(agent, a, {"--last"}, {});
        return {join(cmd, quote), true};
    }
    if (agent == "aider") return {join(QStringList{"aider"} << keepFlags(agent, a, {"--restore-chat-history"}, {}) << "--restore-chat-history", quote), true};
    if (!agent.isEmpty()) return {join(QStringList{agent} << keepFlags(agent, a, {}, {}), quote), true};
    return {join(args, quote), interactive(args)};
}

} // namespace Restore
