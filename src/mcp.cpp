#include "mcp.h"
#include "paths.h"
#include "tools.h"
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QThread>
#include <cstdio>
#include <functional>
#include <iostream>

namespace {

QJsonValue callApi(const QString &method, const QJsonObject &params, int timeoutMs = 15000) {
    QLocalSocket sock;
    sock.connectToServer(Paths::socket());
    if (!sock.waitForConnected(1500)) throw Tools::ToolError{"nebula is not running (cannot connect to " + Paths::socket() + ")"};
    sock.write(QJsonDocument(QJsonObject{{"id", 1}, {"method", method}, {"params", params}}).toJson(QJsonDocument::Compact) + '\n');
    QByteArray buf;
    QDeadlineTimer deadline(timeoutMs);
    while (!deadline.hasExpired()) {
        if (!sock.bytesAvailable() && !sock.waitForReadyRead(200)) continue;
        buf += sock.readAll();
        const int nl = buf.indexOf('\n');
        if (nl < 0) continue;
        const QJsonObject o = QJsonDocument::fromJson(buf.left(nl)).object();
        if (!o["ok"].toBool()) throw Tools::ToolError{o["error"].toString()};
        return o["result"];
    }
    throw Tools::ToolError{"timed out waiting for nebula"};
}

Tools::Api bridgeApi() {
    return [](const QString &method, const QJsonObject &params) { return callApi(method, params, 60000); };
}

void reply(const QJsonValue &id, const QJsonObject &result) {
    const QByteArray line = QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}).toJson(QJsonDocument::Compact);
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void replyError(const QJsonValue &id, int code, const QString &msg) {
    const QByteArray line = QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", msg}}}}).toJson(QJsonDocument::Compact);
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

} // namespace

int runMcp() {
    int argc = 1;
    char name[] = "nebula-mcp";
    char *argv[] = {name, nullptr};
    QCoreApplication app(argc, argv);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        const QJsonObject req = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        const QString method = req["method"].toString();
        const QJsonValue id = req["id"];
        if (id.isUndefined()) continue; // notifications need no answer

        if (method == "initialize") {
            reply(id, {{"protocolVersion", req["params"].toObject()["protocolVersion"].toString("2024-11-05")},
                       {"capabilities", QJsonObject{{"tools", QJsonObject()}}},
                       {"serverInfo", QJsonObject{{"name", "nebula"}, {"version", "0.1.0"}}},
                       {"instructions", "Control the nebula terminal workspace: inspect panes, read output, prompt other agents and launch new ones. "
                                        "Show results to the user as rich views (tables, stats, checklists, code, images) with view_show."}});
        } else if (method == "ping") {
            reply(id, {});
        } else if (method == "tools/list") {
            QJsonArray a;
            for (const Tools::Spec &t : Tools::all()) a << QJsonObject{{"name", t.name}, {"description", t.description}, {"inputSchema", t.schema}};
            reply(id, {{"tools", a}});
        } else if (method == "tools/call") {
            const QJsonObject p = req["params"].toObject();
            const QString tn = p["name"].toString();
            const Tools::Spec *tool = Tools::find(tn);
            if (!tool) { replyError(id, -32602, "unknown tool: " + tn); continue; }
            try {
                const QJsonObject args = p["arguments"].toObject();
                reply(id, {{"content", tool->content ? tool->content(args, bridgeApi())
                                                     : QJsonArray{QJsonObject{{"type", "text"}, {"text", tool->run(args, bridgeApi())}}}}});
            } catch (const Tools::ToolError &e) {
                reply(id, {{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", e.msg}}}}, {"isError", true}});
            }
        } else {
            replyError(id, -32601, "method not found: " + method);
        }
    }
    return 0;
}
