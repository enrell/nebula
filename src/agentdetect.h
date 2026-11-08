#pragma once
#include <QString>
#include <QStringList>

namespace AgentDetect {

struct Signals {
    QString agent;       // detected agent id ("claude", "codex", ...)
    QString tail;        // bottom lines of the screen
    QString title;       // terminal title (OSC 0/2)
    bool recentOutput;   // the pane produced output within the last ~1.5s
    bool active;         // pane is focused and visible (user is watching / typing)
};

// Identify an agent from a process command line + comm name. Empty if none.
// Under a sandbox wrapper (`silo run ... -- claude ...`) it is the wrapped command's agent.
QString identify(const QStringList &args, const QString &comm);

// The sandbox the process runs its command in: "silo" for `silo run|shell ...` (a microVM), else empty.
QString wrapper(const QStringList &args, const QString &comm);

// Instantaneous state from a single screen snapshot: "working", "blocked" or "idle".
QString classify(const Signals &s);

} // namespace AgentDetect
