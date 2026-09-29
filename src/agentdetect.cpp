#include "agentdetect.h"
#include <QFileInfo>
#include <QRegularExpression>

namespace AgentDetect {

QString identify(const QStringList &args, const QString &comm) {
    static const QStringList known = {"claude", "opencode", "codex", "gemini", "aider", "amp", "pi", "droid", "goose",
                                      "crush", "agy", "hermes", "qwen", "kimi", "cursor-agent", "copilot"};
    QStringList names{comm.toLower()};
    for (int i = 0; i < args.size() && i < 4; ++i) names << QFileInfo(args[i]).fileName().toLower();
    for (const QString &n : std::as_const(names))
        for (const QString &k : known)
            if (n == k || n == k + ".js") return k;
    const QString joined = args.join(' ').toLower();
    if (joined.contains("claude-code")) return "claude";
    if (joined.contains("opencode")) return "opencode";
    if (joined.contains("gemini-cli")) return "gemini";
    if (joined.contains("/codex")) return "codex";
    return {};
}

namespace {

struct Profile {
    QStringList working;
    QStringList blocked;
};

const Profile &profile(const QString &agent) {
    static const QHash<QString, Profile> profiles = {
        {"claude", {{"esc to interrupt", "ctrl+c to interrupt"},
                    {"do you want to proceed", "do you want to make this edit", "do you want to create", "do you want to run",
                     "❯ 1. yes", "1. yes", "esc to cancel · tab to amend", "would you like to proceed", "allow claude to"}}},
        {"opencode", {{"esc interrupt", "esc to interrupt"},
                      {"permission required", "allow once", "allow always", "△ permission"}}},
        {"codex", {{"esc to interrupt", "working ("},
                   {"would you like to run", "yes, proceed", "press enter to confirm", "allow command", "approve this"}}},
        {"gemini", {{"esc to cancel", "(esc to cancel"},
                    {"allow once", "waiting for user confirmation", "apply this change?", "allow execution"}}},
        {"aider", {{}, {}}},
    };
    static const Profile generic;
    auto it = profiles.constFind(agent);
    return it == profiles.constEnd() ? generic : *it;
}

bool anyOf(const QString &text, const QStringList &needles) {
    for (const QString &n : needles)
        if (text.contains(n)) return true;
    return false;
}

bool startsWithBraille(const QString &t) {
    if (t.isEmpty()) return false;
    const ushort c = t.at(0).unicode();
    return c >= 0x2800 && c <= 0x28ff;
}

} // namespace

QString classify(const Signals &s) {
    const QString t = s.tail.toLower();
    const Profile &p = profile(s.agent);

    static const QRegularExpression yesNo(R"(\[y/n\]|\(y/n\)|\[yes/no\]|\(yes/no\)|\(y\)es/\(n\)o|\by/n\b\s*[?:]?\s*$)",
                                          QRegularExpression::MultilineOption);
    if (anyOf(t, p.blocked) || yesNo.match(t).hasMatch()) return "blocked";

    if (anyOf(t, p.working)) return "working";
    if (s.agent == "claude" && startsWithBraille(s.title)) return "working";

    // Unfocused pane that keeps printing is doing something. When focused, output is mostly the user's own typing.
    if (!s.active && s.recentOutput) return "working";
    return "idle";
}

} // namespace AgentDetect
