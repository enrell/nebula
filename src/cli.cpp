#include "cli.h"
#include "paths.h"
#include "platform.h"
#include "restore.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <cstdio>
#include <iostream>

static const char *kHelp = R"(usage: nebula ctl <method> [key=value ...]
       nebula ctl --watch          stream events (agent state changes) as JSON lines

Values are parsed as JSON when possible (pane=3 enter=true keys='["Ctrl+C"]'), otherwise sent as strings.
Use text=@- to read a value from stdin. Set NEBULA_SOCKET to talk to another instance.

methods:
  window.show | window.screenshot [path=PNG] | window.resize width=N height=N
  operator.ask prompt=STR [approve=all|none]   (the built-in ACP operator; none = reject every permission request)
  settings.get | settings.set key=NAME value=V
  keys.list | keys.add action=NAME keys=Ctrl+Alt+K | keys.remove action=NAME keys=KEYS | keys.reset [action=NAME]
  ping | status | action.run action=<name>
  space.list | space.create [cwd=] [name=] | space.rename [space=N] name=STR (empty = auto) | space.focus space=N | space.close [space=N]
  tab.create [space=N] | tab.focus tab=N [space=N] | tab.close [tab=N] [space=N]
  pane.list | agent.list | pane.get [pane=ID] | pane.focus pane=ID | pane.close [pane=ID]
  pane.split [pane=ID] direction=right|down
  pane.send_text [pane=ID] text=STR [enter=true] [paste=true]
  pane.send_keys [pane=ID] keys='["Ctrl+C","Enter"]'
  pane.read [pane=ID] [lines=N] [scrollback=true]
  pane.report_state [pane=ID] state=working|blocked|done|idle [ttl=SECONDS]
  view.show file=PATH | content=STR (content=@- reads stdin) [view=ID] [title=STR] [where=modal|right|down|tab]
  view.get view=ID | view.list | view.close view=ID | view.dock view=ID where=modal|right|down|tab | view.toggle
  view.components [name=NAME]
  file.open file=PATH   (in a tab of its own: .neb / Markdown as a view, anything else in the editor)
  editor.open file=PATH [line=N] [where=right|down|tab] | editor.list | editor.get [pane=ID] [text=true] | editor.save [pane=ID]
      (pane.send_text / pane.send_keys type into an editor pane; pane.close refuses unsaved changes without force=true)
  view.export view=ID [format=html|pdf] [path=FILE]   (a standalone HTML file, or a PDF)
  view.snapshot view=ID [block=N] [path=FILE.png]     (a PNG of the view, scrolled to block N; default view-ID.png)

Panes default to the focused pane; inside a pane $NEBULA_PANE is set and used when pane= is omitted for report_state
and view.show (a view opens next to the calling pane; relative paths start from the current directory).
)";

static QJsonValue parseValue(const QString &v) {
    if (v == "@-") {
        std::string all, line;
        while (std::getline(std::cin, line)) all += line + "\n";
        if (!all.empty() && all.back() == '\n') all.pop_back();
        return QString::fromStdString(all);
    }
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson("[" + v.toUtf8() + "]", &err);
    if (err.error == QJsonParseError::NoError && d.array().size() == 1) return d.array().first();
    return v;
}

int runCtl(const QStringList &args) {
    // `ctl --hook ...` is what agent hooks run: silent, always successful, and nothing at all outside a nebula pane,
    // so a hook can never break the agent that runs it. Only the report_state arguments are used (Codex appends a
    // JSON payload to its notify command).
    if (!args.isEmpty() && args[0] == "--hook") {
        if (qEnvironmentVariableIsEmpty("NEBULA_PANE") || args.size() < 2) return 0;
#ifdef Q_OS_WIN
        const char *nul = "NUL";
#else
        const char *nul = "/dev/null";
#endif
        std::freopen(nul, "w", stdout);
        std::freopen(nul, "w", stderr);
        QStringList rest;
        for (int i = 1; i < args.size(); ++i)
            if (i == 1 || args[i].startsWith("state=") || args[i].startsWith("ttl=") || args[i].startsWith("pane=")) rest << args[i];
            else if (args[i].startsWith('{')) rest << "payload=" + args[i];
        runCtl(rest);
        return 0;
    }
    if (args.isEmpty() || args[0] == "-h" || args[0] == "--help") { std::fputs(kHelp, stdout); return args.isEmpty() ? 2 : 0; }
    const bool watch = args[0] == "--watch";
    const QString method = watch ? "events.subscribe" : args[0];

    QJsonObject params;
    for (int i = 1; i < args.size(); ++i) {
        const int eq = args[i].indexOf('=');
        if (eq <= 0) { std::fprintf(stderr, "nebula: expected key=value, got '%s'\n", qPrintable(args[i])); return 2; }
        params[args[i].left(eq)] = parseValue(args[i].mid(eq + 1));
    }
    // agent hooks run `ctl pane.report_state` with the agent's JSON payload on stdin (Claude Code) or as an argument
    // (Codex notify): its conversation id is what restores the pane into the same conversation after a reboot
    if (method == "pane.report_state" && !params.contains("session")) {
        QString sid;
        if (params.contains("payload")) {
            const QJsonValue pl = params.take("payload");
            sid = Restore::sessionFromPayload(pl.isObject() ? QJsonDocument(pl.toObject()).toJson() : pl.toString().toUtf8());
        }
        if (sid.isEmpty()) sid = Restore::sessionFromPayload(Platform::readPipedStdin(150));
        if (!sid.isEmpty()) params["session"] = sid;
    }
    if (!params.contains("pane") && (method == "pane.report_state" || method == "view.show" || method == "editor.open")) {
        bool ok = false;
        const int id = qEnvironmentVariable("NEBULA_PANE").toInt(&ok);
        if (ok) params["pane"] = id;
    }
    if ((method == "view.show" || method == "view.export" || method == "view.snapshot" || method == "editor.open" || method == "file.open") && !params.contains("cwd")) params["cwd"] = QDir::currentPath();
    // the socket reply would otherwise carry the image as base64
    if (method == "view.snapshot" && !params.contains("path")) params["path"] = QString("view-%1.png").arg(params["view"].toInt());

    QLocalSocket sock;
    sock.connectToServer(Paths::socket());
    if (!sock.waitForConnected(1500)) {
        std::fprintf(stderr, "nebula: cannot connect to %s (is nebula running?)\n", qPrintable(Paths::socket()));
        return 1;
    }
    sock.write(QJsonDocument(QJsonObject{{"id", 1}, {"method", method}, {"params", params}}).toJson(QJsonDocument::Compact) + '\n');
    // async methods can legitimately take a while: the operator spins up an ACP agent + a model turn,
    // llm.ask/profile.test wait on a provider round-trip.
    int timeout = 30000;
    if (method == "operator.ask") timeout = 600000;
    else if (method == "llm.ask" || method == "profile.test") timeout = 120000;
    QByteArray buf;
    bool gotResponse = false;
    while (sock.state() == QLocalSocket::ConnectedState || sock.bytesAvailable()) {
        if (!sock.bytesAvailable() && !sock.waitForReadyRead(watch ? -1 : timeout)) break;
        buf += sock.readAll();
        int nl;
        while ((nl = buf.indexOf('\n')) >= 0) {
            const QJsonObject o = QJsonDocument::fromJson(buf.left(nl)).object();
            buf.remove(0, nl + 1);
            if (o.contains("event")) { std::printf("%s\n", QJsonDocument(o).toJson(QJsonDocument::Compact).constData()); std::fflush(stdout); continue; }
            gotResponse = true;
            if (!o["ok"].toBool()) { std::fprintf(stderr, "nebula: %s\n", qPrintable(o["error"].toString())); return 1; }
            if (watch) continue;
            const QJsonValue r = o["result"];
            if (r.isString()) std::printf("%s\n", qPrintable(r.toString()));
            else if (!r.isBool()) std::printf("%s", (r.isArray() ? QJsonDocument(r.toArray()) : QJsonDocument(r.toObject())).toJson(QJsonDocument::Indented).constData());
            return 0;
        }
    }
    return gotResponse ? 0 : 1;
}

// .neb and Markdown files open as rendered views; any other file opens in the editor
bool isDocument(const QString &path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == "neb" || ext == "md" || ext == "markdown";
}

QStringList documentArguments(const QStringList &args, bool *ok) {
    QStringList files;
    *ok = true;
    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args[i];
        if (a.startsWith('-')) continue;
        const QFileInfo fi(a);
        // a word that is not a file (the value of -platform xcb) is not ours; a file name that does not exist yet
        // is, when it has an extension or a path (nebula notes.md creates it)
        if (!fi.exists() && !(a.contains('/') || !fi.suffix().isEmpty())) continue;
        if (fi.isDir()) { std::fprintf(stderr, "nebula: %s: is a directory\n", qPrintable(a)); *ok = false; continue; }
        if (!fi.exists() && isDocument(a)) { std::fprintf(stderr, "nebula: %s: no such file\n", qPrintable(a)); *ok = false; continue; }
        files << fi.absoluteFilePath();
    }
    return files;
}

int openDocuments(const QStringList &files) {
    QLocalSocket sock;
    sock.connectToServer(Paths::socket());
    if (!sock.waitForConnected(1500)) {
        std::fprintf(stderr, "nebula: cannot connect to %s\n", qPrintable(Paths::socket()));
        return 1;
    }
    int rc = 0, id = 0;
    QByteArray buf;
    for (const QString &f : files) {
        const QJsonObject params{{"file", f}, {"cwd", QFileInfo(f).absolutePath()}, {"raise", true}};
        const QString method = "file.open";
        sock.write(QJsonDocument(QJsonObject{{"id", ++id}, {"method", method}, {"params", params}}).toJson(QJsonDocument::Compact) + '\n');
        QJsonObject reply;
        while (reply.isEmpty()) {
            int nl = buf.indexOf('\n');
            if (nl < 0) {
                // the first view of a session starts the web engine, so the reply can take a few seconds
                if (!sock.waitForReadyRead(30000)) { std::fprintf(stderr, "nebula: no reply for %s\n", qPrintable(f)); return 1; }
                buf += sock.readAll();
                continue;
            }
            reply = QJsonDocument::fromJson(buf.left(nl)).object();
            buf.remove(0, nl + 1);
            if (reply.contains("event")) reply = {};
        }
        if (!reply["ok"].toBool()) { std::fprintf(stderr, "nebula: %s: %s\n", qPrintable(f), qPrintable(reply["error"].toString())); rc = 1; }
        else if (const QJsonObject report = reply["result"].toObject(); report.contains("ok") && !report["ok"].toBool())
            std::fprintf(stderr, "nebula: %s: the document has errors (shown in the view)\n", qPrintable(f));
    }
    return rc;
}

int showRunning() {
    QLocalSocket sock;
    sock.connectToServer(Paths::socket());
    if (!sock.waitForConnected(1500)) return 1;
    sock.write(QJsonDocument(QJsonObject{{"id", 1}, {"method", "window.show"}, {"params", QJsonObject()}}).toJson(QJsonDocument::Compact) + '\n');
    if (!sock.waitForReadyRead(3000)) return 1;
    return QJsonDocument::fromJson(sock.readAll().split('\n').first()).object()["ok"].toBool() ? 0 : 1;
}
