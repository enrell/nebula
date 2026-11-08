#include "silo.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace Silo {

QString program() {
    const QString env = qEnvironmentVariable("NEBULA_SILO");
    if (!env.isEmpty()) return env;
    return QStandardPaths::findExecutable("silo");
}

Result run(const QStringList &args, int timeoutMs) {
    Result r;
    r.code = "error";
    const QString exe = program();
    if (exe.isEmpty()) { r.message = "the silo CLI was not found"; return r; }
    QProcess p;
    p.start(exe, args);
    if (!p.waitForStarted(5000)) { r.message = "cannot start " + exe + ": " + p.errorString(); return r; }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        r.message = QString("silo %1 did not finish within %2 s").arg(args.value(0)).arg(timeoutMs / 1000);
        return r;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(p.readAllStandardOutput(), &perr);
    const bool parsed = perr.error == QJsonParseError::NoError;
    if (parsed) r.value = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0 && parsed) {
        r.ok = true;
        r.code.clear();
        return r;
    }
    const QJsonObject e = doc.object().value("error").toObject();
    if (!e.isEmpty()) {
        r.code = e["code"].toString("error");
        r.message = e["message"].toString();
        r.value = QJsonValue();
    }
    if (r.message.isEmpty()) {
        const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
        r.message = !err.isEmpty() ? err.section('\n', 0, 0).remove(QRegularExpression("^error: "))
                    : !parsed      ? QString("silo %1 printed no JSON (a silo without --json support?)").arg(args.value(0))
                                   : QString("silo %1 exited with code %2").arg(args.value(0)).arg(p.exitCode());
    }
    return r;
}

Isolation::Probe probe() {
    // asked again on every launch that may use silo: doctor only stats a few files, and a fixed setup is seen at once
    Isolation::Probe p;
#ifdef Q_OS_LINUX
    p.onLinux = true;
#endif
    p.cli = p.onLinux && !program().isEmpty();
    if (p.cli) {
        const Result r = run({"doctor", "--json"}, 15000);
        const QJsonObject report = r.value.toObject();
        p.ready = report["ok"].toBool();
        if (!p.ready) {
            QStringList failed;
            for (const QJsonValue &c : report["checks"].toArray())
                if (!c["ok"].toBool()) failed << c["name"].toString() + ": " + c["detail"].toString();
            p.problem = (failed.isEmpty() ? r.message : failed.join("; ")).toStdString();
        }
    }
    return p;
}

Result status(const QString &session) { return run({"status", "--json", session}); }
Result diff(const QString &session, bool statOnly) {
    QStringList a{"diff", "--json"};
    if (statOnly) a << "--stat-only";
    return run(a << session, 60000);
}
// apply imports the whole private tree with git (add -A, write-tree, fetch): give big repositories time
Result apply(const QString &session) { return run({"apply", "--json", session}, 120000); }
Result discard(const QString &session, bool force) {
    QStringList a{"discard", "--json"};
    if (force) a << "--force";
    return run(a << session, 60000);
}
Result list() { return run({"ls", "--json"}); }

} // namespace Silo
