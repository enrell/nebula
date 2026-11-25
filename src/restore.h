#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>

// What a pane comes back as after its shell died (a reboot, Kill all & quit): the program it was running, and for
// agents the exact conversation it was in. Pure functions, so they are unit-tested (tests/test_restore.cpp).
namespace Restore {

struct Plan {
    QString command;       // typed into the new shell; empty: nothing to restore
    bool autorun = false;  // run it (an agent or a known interactive program) instead of leaving it at the prompt
};

using Quote = std::function<QString(const QString &)>;
QString posixQuote(const QString &arg);   // 'single quotes' only where needed

// `agent` is AgentDetect's name for the foreground program ("" when it is not an agent), `args` its argv and
// `session` the agent's conversation id when known (from its hooks or its own arguments).
Plan plan(const QString &agent, const QStringList &args, const QString &session, const Quote &quote = posixQuote);

// The conversation id an agent was started with (claude --resume X, codex resume X, opencode --session X).
QString sessionFromArgs(const QString &agent, const QStringList &args);

// The conversation id in the JSON an agent hands its hooks: Claude Code's session_id, Codex's thread-id,
// opencode's sessionID. Empty when there is none or the payload is not JSON.
QString sessionFromPayload(const QByteArray &json);

// Programs that are safe and useful to start again unattended: editors, pagers, monitors, file managers, git and
// container TUIs, database shells, REPLs without a script, ssh. Anything else (a build, a script) is only typed.
bool interactive(const QStringList &args);

} // namespace Restore
