#include "cli.h"
#include "paths.h"
#include <QCoreApplication>
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
  window.screenshot [path=/tmp/nebula.png] | window.resize width=N height=N
  operator.ask prompt=STR [approve=all|none]   (the built-in ACP operator; none = reject every permission request)
  settings.get | settings.set key=NAME value=V
  ping | status | action.run action=<name>
  space.list | space.create [cwd=] [name=] | space.rename [space=N] name=STR (empty = auto) | space.focus space=N | space.close [space=N]
  tab.create [space=N] | tab.focus tab=N [space=N] | tab.close [tab=N] [space=N]
  pane.list | agent.list | pane.get [pane=ID] | pane.focus pane=ID | pane.close [pane=ID]
  pane.split [pane=ID] direction=right|down
  pane.send_text [pane=ID] text=STR [enter=true] [paste=true]
  pane.send_keys [pane=ID] keys='["Ctrl+C","Enter"]'
  pane.read [pane=ID] [lines=N] [scrollback=true]
  pane.report_state [pane=ID] state=working|blocked|done|idle [ttl=SECONDS]

Panes default to the focused pane; inside a pane $NEBULA_PANE is set and used when pane= is omitted for report_state.
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
    if (args.isEmpty() || args[0] == "-h" || args[0] == "--help") { std::fputs(kHelp, stdout); return args.isEmpty() ? 2 : 0; }
    const bool watch = args[0] == "--watch";
    const QString method = watch ? "events.subscribe" : args[0];

    QJsonObject params;
    for (int i = 1; i < args.size(); ++i) {
        const int eq = args[i].indexOf('=');
        if (eq <= 0) { std::fprintf(stderr, "nebula: expected key=value, got '%s'\n", qPrintable(args[i])); return 2; }
        params[args[i].left(eq)] = parseValue(args[i].mid(eq + 1));
    }
    if (!params.contains("pane") && method == "pane.report_state") {
        bool ok = false;
        const int id = qEnvironmentVariable("NEBULA_PANE").toInt(&ok);
        if (ok) params["pane"] = id;
    }

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
