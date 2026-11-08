#pragma once
#include "isolation.h"
#include <QJsonValue>
#include <QString>
#include <QStringList>

// The silo CLI, driven through its --json output: the probe that decides whether
// silo can isolate an agent here, and the session commands behind diff/apply/discard. Calls block with a timeout,
// like the git calls in launcher.cpp. See docs/isolation.md.
namespace Silo {

QString program();          // $NEBULA_SILO, else `silo` on PATH; empty when there is none
Isolation::Probe probe();   // Linux, the CLI, and `silo doctor --json`

struct Result {
    bool ok = false;
    QJsonValue value;   // what silo printed as JSON (on failure too, when it is not an error envelope)
    QString code;       // on failure: silo's error code (dirty_host, running, ...), "error" when it gave none
    QString message;    // on failure: what went wrong
};

Result run(const QStringList &args, int timeoutMs = 30000);
Result status(const QString &session);
Result diff(const QString &session, bool statOnly);
Result apply(const QString &session);
Result discard(const QString &session, bool force);
Result list();

} // namespace Silo
