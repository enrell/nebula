#include "nativeagents.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTimer>
#include <QUuid>

namespace {

QJsonArray allowRejectOptions() {
    return QJsonArray{QJsonObject{{"optionId", "allow"}, {"name", "Allow"}, {"kind", "allow_once"}},
                      QJsonObject{{"optionId", "reject"}, {"name", "Deny"}, {"kind", "reject_once"}}};
}

bool allowed(const QJsonObject &acpResult) {
    const QJsonObject o = acpResult["outcome"].toObject();
    return o["outcome"].toString() == "selected" && o["optionId"].toString() == "allow";
}

QJsonObject modelOption(const QString &current, const QJsonArray &options) {
    return QJsonObject{{"id", "model"}, {"name", "Model"}, {"category", "model"}, {"type", "select"},
                       {"currentValue", current}, {"options", options}};
}

QString agentTextOf(const QJsonArray &prompt) {
    QStringList parts;
    for (const QJsonValue &v : prompt)
        if (v.toObject()["type"].toString() == "text") parts << v.toObject()["text"].toString();
    return parts.join("\n\n");
}

// "mcp__nebula__list_panes" -> "nebula.list_panes"
QString prettyTool(const QString &name) {
    if (!name.startsWith("mcp__")) return name;
    QString n = name.mid(5);
    n.replace("__", ".");
    return n;
}

} // namespace

// ─────────────────────────────── Claude Code (stream-json) ───────────────────────────────

void ClaudeNativeClient::start(const QString &, const QStringList &) {
    // The process is spawned at session/new, when the working directory and MCP servers are known
    // (claude takes them as launch arguments). Report "started" so the operator can begin the handshake.
    m_alive = true;
    if (!m_hooked) { m_hooked = true; connect(this, &AcpClient::died, this, [this] { m_alive = false; }); }
    QTimer::singleShot(0, this, [this] { if (m_alive) emit started(); });
}

void ClaudeNativeClient::stop() {
    AcpClient::stop();
    m_alive = false;
    m_ctlPending.clear();
    m_perms.clear();
    m_promptId = m_newId = 0;
}

void ClaudeNativeClient::later(int id, bool ok, const QJsonValue &value, const QString &error) {
    QTimer::singleShot(0, this, [this, id, ok, value, error] { emit result(id, ok, value, error); });
}

void ClaudeNativeClient::control(const QString &requestId, const QJsonObject &request) {
    write({{"type", "control_request"}, {"request_id", requestId}, {"request", request}});
}

int ClaudeNativeClient::call(const QString &method, const QJsonObject &params) {
    const int id = m_nextId++;
    if (method == "initialize") {
        later(id, true, QJsonObject{{"authMethods", QJsonArray{}}});
    } else if (method == "session/new") {
        spawn(id, params);
    } else if (method == "session/set_config_option") {
        if (params["configId"].toString() != "model") { later(id, false, {}, "unsupported option"); return id; }
        const QString rid = QString("m%1").arg(++m_ctlSeq);
        m_ctlPending[rid] = id;
        control(rid, {{"subtype", "set_model"}, {"model", params["value"].toString()}});
    } else if (method == "session/prompt") {
        m_promptId = id;
        m_streamedMsgs.clear();
        write({{"type", "user"},
               {"message", QJsonObject{{"role", "user"},
                                       {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", agentTextOf(params["prompt"].toArray())}}}}}}});
    } else {
        later(id, false, {}, "unsupported method " + method);
    }
    return id;
}

void ClaudeNativeClient::spawn(int id, const QJsonObject &params) {
    QJsonObject servers;
    for (const QJsonValue &v : params["mcpServers"].toArray()) {
        const QJsonObject s = v.toObject();
        QJsonObject env;
        for (const QJsonValue &e : s["env"].toArray()) env[e.toObject()["name"].toString()] = e.toObject()["value"].toString();
        servers[s["name"].toString()] = QJsonObject{{"command", s["command"].toString()}, {"args", s["args"].toArray()}, {"env", env}};
    }
    m_session = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_seenTools.clear();
    setWorkingDirectory(params["cwd"].toString());
    const QStringList args = {"-p", "--input-format", "stream-json", "--output-format", "stream-json", "--verbose",
                              "--include-partial-messages", "--no-session-persistence", "--strict-mcp-config",
                              // without this claude denies anything not pre-approved by itself, instead of asking us
                              "--permission-mode", "default", "--permission-prompt-tool", "stdio",
                              "--mcp-config", QString::fromUtf8(QJsonDocument(QJsonObject{{"mcpServers", servers}}).toJson(QJsonDocument::Compact))};
    AcpClient::start("claude", args);   // (begins with stop(), which clears our state: set it afterwards)
    m_newId = id;
    m_alive = true;
    control("init", {{"subtype", "initialize"}});
}

void ClaudeNativeClient::notify(const QString &method, const QJsonObject &) {
    if (method == "session/cancel") control(QString("i%1").arg(++m_ctlSeq), {{"subtype", "interrupt"}});
}

void ClaudeNativeClient::respond(qint64 id, const QJsonObject &result) {
    if (!m_perms.contains(id)) return;
    const Perm p = m_perms.take(id);
    const QJsonObject body = allowed(result) ? QJsonObject{{"behavior", "allow"}, {"updatedInput", p.input}}
                                             : QJsonObject{{"behavior", "deny"}, {"message", "Denied by the user in nebula"}};
    write({{"type", "control_response"}, {"response", QJsonObject{{"subtype", "success"}, {"request_id", p.requestId}, {"response", body}}}});
}

void ClaudeNativeClient::respondError(qint64 id, int, const QString &message) {
    if (!m_perms.contains(id)) return;
    const Perm p = m_perms.take(id);
    write({{"type", "control_response"}, {"response", QJsonObject{{"subtype", "error"}, {"request_id", p.requestId}, {"error", message}}}});
}

void ClaudeNativeClient::toolStarted(const QJsonObject &block) {
    const QString tid = block["id"].toString();
    if (m_seenTools.contains(tid)) return;
    m_seenTools.insert(tid);
    const QString name = block["name"].toString();
    if (name == "ToolSearch") return;   // claude's own tool lookup: pure noise in the chat
    const QJsonObject input = block["input"].toObject();
    const QString title = prettyTool(name);
    emit notification("session/update", {{"sessionId", m_session},
                                         {"update", QJsonObject{{"sessionUpdate", "tool_call"}, {"toolCallId", tid}, {"title", title}, {"rawInput", input}}}});
}

void ClaudeNativeClient::dispatch(const QJsonObject &msg) {
    const QString type = msg["type"].toString();
    const auto chunk = [this](const QString &kind, const QString &text) {
        if (text.isEmpty()) return;
        emit notification("session/update", {{"sessionId", m_session},
                                             {"update", QJsonObject{{"sessionUpdate", kind}, {"content", QJsonObject{{"type", "text"}, {"text", text}}}}}});
    };

    if (type == "control_response") {
        const QJsonObject r = msg["response"].toObject();
        const QString rid = r["request_id"].toString();
        const bool ok = r["subtype"].toString() == "success";
        const QString err = r["error"].toString();
        if (rid == "init") {
            if (!ok) { emit result(m_newId, false, {}, err.isEmpty() ? "claude did not initialize" : err); return; }
            QJsonArray options;
            for (const QJsonValue &v : r["response"].toObject()["models"].toArray()) {
                const QJsonObject m = v.toObject();
                options << QJsonObject{{"value", m["value"].toString()},
                                       {"name", m["displayName"].toString().isEmpty() ? m["value"].toString() : m["displayName"].toString()},
                                       {"description", m["description"].toString()}};
            }
            emit result(m_newId, true, QJsonObject{{"sessionId", m_session}, {"configOptions", QJsonArray{modelOption("default", options)}}}, {});
        } else if (m_ctlPending.contains(rid)) {
            emit result(m_ctlPending.take(rid), ok, QJsonObject{}, err);
        }
    } else if (type == "stream_event") {
        const QJsonObject ev = msg["event"].toObject();
        const QString et = ev["type"].toString();
        if (et == "message_start") {
            m_curMsg = ev["message"].toObject()["id"].toString();
        } else if (et == "content_block_delta") {
            const QJsonObject d = ev["delta"].toObject();
            if (d["type"].toString() == "text_delta") { m_streamedMsgs.insert(m_curMsg); chunk("agent_message_chunk", d["text"].toString()); }
            else if (d["type"].toString() == "thinking_delta") chunk("agent_thought_chunk", d["thinking"].toString());
        }
    } else if (type == "assistant") {
        const QJsonObject m = msg["message"].toObject();
        for (const QJsonValue &b : m["content"].toArray()) {
            const QJsonObject block = b.toObject();
            if (block["type"].toString() == "tool_use") toolStarted(block);
            else if (block["type"].toString() == "text" && !m_streamedMsgs.contains(m["id"].toString())) chunk("agent_message_chunk", block["text"].toString());
        }
    } else if (type == "user") {
        const QJsonValue content = msg["message"].toObject()["content"];
        if (!content.isArray()) return;
        for (const QJsonValue &b : content.toArray()) {
            const QJsonObject block = b.toObject();
            if (block["type"].toString() != "tool_result") continue;
            emit notification("session/update", {{"sessionId", m_session},
                                                 {"update", QJsonObject{{"sessionUpdate", "tool_call_update"}, {"toolCallId", block["tool_use_id"].toString()},
                                                                        {"status", block["is_error"].toBool() ? "failed" : "completed"}}}});
        }
    } else if (type == "control_request") {
        const QJsonObject req = msg["request"].toObject();
        const QString rid = msg["request_id"].toString();
        if (req["subtype"].toString() != "can_use_tool") {
            write({{"type", "control_response"}, {"response", QJsonObject{{"subtype", "error"}, {"request_id", rid}, {"error", "unsupported request"}}}});
            return;
        }
        const qint64 pid = ++m_permSeq;
        m_perms[pid] = {rid, req["input"].toObject()};
        emit request(pid, "session/request_permission",
                     {{"options", allowRejectOptions()},
                      {"toolCall", QJsonObject{{"title", prettyTool(req["tool_name"].toString())}, {"rawInput", req["input"].toObject()}}}});
    } else if (type == "result") {
        if (!m_promptId) return;
        const int id = m_promptId;
        m_promptId = 0;
        const bool err = msg["is_error"].toBool();
        const QString text = msg["result"].toString();
        emit result(id, !err, QJsonObject{{"stopReason", "end_turn"}}, err ? (text.isEmpty() ? QString("claude reported an error") : text) : QString());
    }
}

// ─────────────────────────────── Codex (app-server) ───────────────────────────────

void CodexNativeClient::start(const QString &, const QStringList &) { AcpClient::start("codex", {"app-server"}); }

void CodexNativeClient::later(int id, bool ok, const QJsonValue &value, const QString &error) {
    QTimer::singleShot(0, this, [this, id, ok, value, error] { emit result(id, ok, value, error); });
}

void CodexNativeClient::rpc(const QString &method, const QJsonObject &params, Cb cb) {
    const int cid = m_cid++;
    m_pend[cid] = std::move(cb);
    write({{"jsonrpc", "2.0"}, {"id", cid}, {"method", method}, {"params", params}});
}

int CodexNativeClient::call(const QString &method, const QJsonObject &params) {
    const int id = m_nextId++;
    if (method == "initialize") {
        rpc("initialize", {{"clientInfo", QJsonObject{{"name", "nebula"}, {"title", "nebula"}, {"version", QCoreApplication::applicationVersion()}}}, {"capabilities", QJsonObject{}}},
            [this, id](bool ok, const QJsonValue &, const QString &err) {
                if (!ok) { emit result(id, false, {}, err); return; }
                write({{"jsonrpc", "2.0"}, {"method", "initialized"}});
                rpc("model/list", {}, [this, id](bool, const QJsonValue &v, const QString &) {
                    m_models = v.toObject()["data"].toArray();
                    for (const QJsonValue &m : std::as_const(m_models))
                        if (m.toObject()["isDefault"].toBool()) m_defaultModel = m.toObject()["id"].toString();
                    if (m_defaultModel.isEmpty() && !m_models.isEmpty()) m_defaultModel = m_models.first().toObject()["id"].toString();
                    emit result(id, true, QJsonObject{{"authMethods", QJsonArray{}}}, {});
                });
            });
    } else if (method == "session/new") {
        QJsonObject servers;
        for (const QJsonValue &v : params["mcpServers"].toArray()) {
            const QJsonObject s = v.toObject();
            QJsonObject env;
            for (const QJsonValue &e : s["env"].toArray()) env[e.toObject()["name"].toString()] = e.toObject()["value"].toString();
            servers[s["name"].toString()] = QJsonObject{{"command", s["command"].toString()}, {"args", s["args"].toArray()}, {"env", env}};
        }
        // Always name a model: a stale one in ~/.codex/config.toml would otherwise fail every turn.
        QJsonObject p{{"cwd", params["cwd"].toString()}, {"approvalPolicy", "on-request"}, {"ephemeral", true},
                      {"config", QJsonObject{{"mcp_servers", servers}}}};
        if (!m_defaultModel.isEmpty()) p["model"] = m_defaultModel;
        rpc("thread/start", p, [this, id](bool ok, const QJsonValue &v, const QString &err) {
            if (!ok) { emit result(id, false, {}, err); return; }
            const QJsonObject r = v.toObject();
            m_thread = r["thread"].toObject()["id"].toString();
            m_model = r["model"].toString();
            QJsonArray options;
            for (const QJsonValue &mv : std::as_const(m_models)) {
                const QJsonObject m = mv.toObject();
                if (m["hidden"].toBool()) continue;
                options << QJsonObject{{"value", m["id"].toString()}, {"name", m["displayName"].toString()}, {"description", m["description"].toString()}};
            }
            emit result(id, true, QJsonObject{{"sessionId", m_thread}, {"configOptions", QJsonArray{modelOption(m_model, options)}}}, {});
        });
    } else if (method == "session/set_config_option") {
        if (params["configId"].toString() == "model") { m_model = params["value"].toString(); later(id, true, QJsonObject{}); }
        else later(id, false, {}, "unsupported option");
    } else if (method == "session/prompt") {
        m_promptId = id;
        m_hadMessage = false;
        m_lastError.clear();
        QJsonObject p{{"threadId", m_thread}, {"input", QJsonArray{QJsonObject{{"type", "text"}, {"text", agentTextOf(params["prompt"].toArray())}}}}};
        if (!m_model.isEmpty()) p["model"] = m_model;
        rpc("turn/start", p, [this, id](bool ok, const QJsonValue &v, const QString &err) {
            if (!ok) { m_promptId = 0; emit result(id, false, {}, err); return; }
            m_turn = v.toObject()["turn"].toObject()["id"].toString();
        });
    } else {
        later(id, false, {}, "unsupported method " + method);
    }
    return id;
}

void CodexNativeClient::notify(const QString &method, const QJsonObject &) {
    if (method == "session/cancel" && !m_thread.isEmpty() && !m_turn.isEmpty())
        rpc("turn/interrupt", {{"threadId", m_thread}, {"turnId", m_turn}}, [](bool, const QJsonValue &, const QString &) {});
}

void CodexNativeClient::respond(qint64 id, const QJsonObject &result) {
    if (!m_perms.contains(id)) return;
    const Perm p = m_perms.take(id);
    const bool ok = allowed(result);
    QJsonObject body;
    if (p.kind == "elicit") body = {{"action", ok ? "accept" : "decline"}};
    else if (p.kind == "perm") body = {{"permissions", ok ? p.params["permissions"].toObject() : QJsonObject{}}, {"scope", "turn"}};
    else body = {{"decision", ok ? "accept" : "decline"}};
    write({{"jsonrpc", "2.0"}, {"id", p.rawId}, {"result", body}});
}

void CodexNativeClient::respondError(qint64 id, int code, const QString &message) {
    if (!m_perms.contains(id)) return;
    const Perm p = m_perms.take(id);
    write({{"jsonrpc", "2.0"}, {"id", p.rawId}, {"error", QJsonObject{{"code", code}, {"message", message}}}});
}

void CodexNativeClient::askPermission(const QJsonValue &rawId, const QString &kind, const QString &title, const QJsonObject &raw, const QJsonObject &params) {
    const qint64 pid = ++m_permSeq;
    m_perms[pid] = {rawId, kind, params};
    emit request(pid, "session/request_permission", {{"options", allowRejectOptions()}, {"toolCall", QJsonObject{{"title", title}, {"rawInput", raw}}}});
}

void CodexNativeClient::itemStarted(const QJsonObject &item) {
    const QString t = item["type"].toString();
    QString title;
    QJsonObject input;
    if (t == "commandExecution") {
        const QJsonArray acts = item["commandActions"].toArray();
        title = acts.isEmpty() ? item["command"].toString() : acts.first().toObject()["command"].toString();
        if (title.isEmpty()) title = item["command"].toString();
        input = {{"command", title}};
    } else if (t == "mcpToolCall") {
        title = item["server"].toString() + "." + item["tool"].toString();
        input = item["arguments"].toObject();
    } else if (t == "fileChange") {
        title = "edit files";
    } else if (t == "webSearch") {
        title = "web search: " + item["query"].toString();
    } else if (t == "dynamicToolCall") {
        title = item["tool"].toString();
        input = item["arguments"].toObject();
    } else {
        return;
    }
    update({{"sessionUpdate", "tool_call"}, {"toolCallId", item["id"].toString()}, {"title", title}, {"rawInput", input}});
}

void CodexNativeClient::dispatch(const QJsonObject &msg) {
    const bool hasId = msg.contains("id"), hasMethod = msg.contains("method");

    if (hasId && !hasMethod) {   // response to one of our requests
        Cb cb = m_pend.take(msg["id"].toInt());
        if (cb) cb(!msg.contains("error"), msg["result"], msg["error"].toObject()["message"].toString());
        return;
    }

    const QString method = msg["method"].toString();
    const QJsonObject p = msg["params"].toObject();

    if (hasId) {   // server -> client request
        const QJsonValue rid = msg["id"];
        if (method == "item/commandExecution/requestApproval")
            askPermission(rid, "cmd", p["command"].toString().isEmpty() ? "run a command" : p["command"].toString(),
                          {{"command", p["command"]}, {"cwd", p["cwd"]}, {"reason", p["reason"]}}, p);
        else if (method == "item/fileChange/requestApproval")
            askPermission(rid, "file", "edit files", {{"reason", p["reason"]}}, p);
        else if (method == "item/permissions/requestApproval")
            askPermission(rid, "perm", "extra permissions", {{"reason", p["reason"]}, {"permissions", p["permissions"]}}, p);
        else if (method == "mcpServer/elicitation/request")
            askPermission(rid, "elicit", p["message"].toString().isEmpty() ? "MCP tool call" : p["message"].toString(), {}, p);
        else
            write({{"jsonrpc", "2.0"}, {"id", rid}, {"error", QJsonObject{{"code", -32601}, {"message", "not supported"}}}});
        return;
    }

    if (!p["threadId"].toString().isEmpty() && p["threadId"].toString() != m_thread) return;

    if (method == "item/agentMessage/delta") {
        update({{"sessionUpdate", "agent_message_chunk"}, {"content", QJsonObject{{"type", "text"}, {"text", p["delta"].toString()}}}});
    } else if (method == "item/reasoning/summaryTextDelta" || method == "item/reasoning/textDelta") {
        update({{"sessionUpdate", "agent_thought_chunk"}, {"content", QJsonObject{{"type", "text"}, {"text", p["delta"].toString()}}}});
    } else if (method == "item/started") {
        const QJsonObject item = p["item"].toObject();
        if (item["type"].toString() == "agentMessage") {   // codex sends commentary and the final answer as separate messages
            if (m_hadMessage) update({{"sessionUpdate", "agent_message_chunk"}, {"content", QJsonObject{{"type", "text"}, {"text", "\n\n"}}}});
            m_hadMessage = true;
        }
        itemStarted(item);
    } else if (method == "item/completed") {
        const QJsonObject item = p["item"].toObject();
        const QString t = item["type"].toString();
        if (t == "commandExecution" || t == "mcpToolCall" || t == "fileChange" || t == "webSearch" || t == "dynamicToolCall") {
            const QString st = item["status"].toString();
            update({{"sessionUpdate", "tool_call_update"}, {"toolCallId", item["id"].toString()},
                    {"status", (st == "failed" || st == "declined") ? "failed" : "completed"}});
        }
    } else if (method == "error") {
        m_lastError = p["error"].toObject()["message"].toString();
    } else if (method == "turn/completed") {
        const QJsonObject turn = p["turn"].toObject();
        m_turn.clear();
        if (!m_promptId) return;
        const int id = m_promptId;
        m_promptId = 0;
        const QString status = turn["status"].toString();
        if (status == "failed") {
            QString msgText = turn["error"].toObject()["message"].toString();
            if (msgText.isEmpty()) msgText = m_lastError;
            // codex wraps provider errors as a JSON string: {"error":{"message":"..."}}
            const QJsonObject inner = QJsonDocument::fromJson(msgText.toUtf8()).object();
            if (!inner["error"].toObject()["message"].toString().isEmpty()) msgText = inner["error"].toObject()["message"].toString();
            emit result(id, false, {}, msgText.isEmpty() ? "codex turn failed" : msgText);
        } else {
            emit result(id, true, QJsonObject{{"stopReason", status == "interrupted" ? "cancelled" : "end_turn"}}, {});
        }
    }
}
