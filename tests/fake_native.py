#!/usr/bin/env python3
"""Fake `claude` (stream-json) and `codex app-server` for e2e tests: `fake_native.py claude|codex [args...]`.
Like fake_acp.py, it drives the real `nebula mcp` server it was configured with, so only the model is fake."""
import json, os, subprocess, sys

KIND = sys.argv[1]
ARGS = sys.argv[2:]
LOG = os.environ.get("FAKEACP_LOG", "/tmp/fake_native.log")
mcp = None
seq = [100]


def log(**kw):
    with open(LOG, "a") as f:
        f.write(json.dumps(dict(kw, kind=KIND)) + "\n")


def recv():
    line = sys.stdin.readline()
    if not line:
        sys.exit(0)
    return json.loads(line)


def send(o):
    sys.stdout.write(json.dumps(o) + "\n")
    sys.stdout.flush()


def start_mcp(cfg):
    env = dict(os.environ)
    env.update(cfg.get("env", {}))
    p = subprocess.Popen([cfg["command"]] + cfg.get("args", []), stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, env=env)
    p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "fake", "version": "0"}}}) + "\n")
    p.stdin.flush()
    json.loads(p.stdout.readline())
    p.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
    p.stdin.flush()
    return p


def mcp_call(name, args):
    mcp.stdin.write(json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {"name": name, "arguments": args}}) + "\n")
    mcp.stdin.flush()
    while True:
        r = json.loads(mcp.stdout.readline())
        if r.get("id") == 1:
            return r


def plan(prompt):
    """-> (tool, args, needs_permission, reply_ok, reply_denied)"""
    if "launch a shell" in prompt:
        return "launch_agent", {"agent": "shell"}, False, "launched a shell", None
    if "close pane" in prompt:
        pane = int(prompt.split("close pane ")[1].split()[0])
        return "close_pane", {"pane": pane}, True, "closed pane %d" % pane, "not allowed"
    return None, None, False, "operator-done", None


# ───────────────────────────── claude ─────────────────────────────
def claude():
    global mcp
    cfg = json.loads(ARGS[ARGS.index("--mcp-config") + 1])["mcpServers"]["nebula"]
    log(argv=ARGS)
    mcp = start_mcp(cfg)
    while True:
        m = recv()
        if m["type"] == "control_request":
            sub = m["request"]["subtype"]
            body = {}
            if sub == "initialize":
                body = {"models": [{"value": "default", "displayName": "Default", "description": "the default"},
                                   {"value": "fast", "displayName": "Fast", "description": "the fast one"}]}
            elif sub == "set_model":
                log(setModel=m["request"]["model"])
            send({"type": "control_response", "response": {"subtype": "success", "request_id": m["request_id"], "response": body}})
        elif m["type"] == "user":
            prompt = m["message"]["content"][0]["text"]
            log(prompt=prompt)
            tool, args, perm, ok, denied = plan(prompt)
            text = ok
            if tool:
                allowed = True
                if perm:
                    seq[0] += 1
                    send({"type": "control_request", "request_id": "perm%d" % seq[0],
                          "request": {"subtype": "can_use_tool", "tool_name": "mcp__nebula__" + tool, "input": args}})
                    r = recv()
                    allowed = r["response"]["response"].get("behavior") == "allow"
                send({"type": "assistant", "message": {"id": "m1", "content": [{"type": "tool_use", "id": "tu1", "name": "mcp__nebula__" + tool, "input": args}]}})
                if allowed:
                    mcp_call(tool, args)
                send({"type": "user", "message": {"role": "user", "content": [{"type": "tool_result", "tool_use_id": "tu1", "is_error": not allowed}]}})
                text = ok if allowed else denied
            send({"type": "stream_event", "event": {"type": "message_start", "message": {"id": "m2"}}})
            send({"type": "stream_event", "event": {"type": "content_block_delta", "index": 0, "delta": {"type": "text_delta", "text": text}}})
            send({"type": "assistant", "message": {"id": "m2", "content": [{"type": "text", "text": text}]}})
            send({"type": "result", "subtype": "success", "is_error": False, "result": text})


# ───────────────────────────── codex app-server ─────────────────────────────
def codex():
    global mcp
    thread = "th1"
    while True:
        m = recv()
        meth, p, mid = m.get("method"), m.get("params") or {}, m.get("id")
        if meth == "initialize":
            send({"id": mid, "result": {"userAgent": "fake-codex", "codexHome": "/tmp", "platformFamily": "unix", "platformOs": "linux"}})
        elif meth == "model/list":
            send({"id": mid, "result": {"data": [{"id": "good-1", "displayName": "Good 1", "description": "d", "isDefault": True, "hidden": False},
                                                {"id": "good-2", "displayName": "Good 2", "description": "d2", "isDefault": False, "hidden": False}]}})
        elif meth == "thread/start":
            servers = p["config"]["mcp_servers"]
            log(threadStart={"model": p.get("model"), "approvalPolicy": p.get("approvalPolicy"), "servers": list(servers)})
            mcp = start_mcp(servers["nebula"])
            send({"id": mid, "result": {"thread": {"id": thread}, "model": p.get("model", "good-1")}})
        elif meth == "turn/start":
            prompt = p["input"][0]["text"]
            log(prompt=prompt, model=p.get("model"))
            send({"id": mid, "result": {"turn": {"id": "tu1", "status": "inProgress"}}})
            tool, args, perm, ok, denied = plan(prompt)
            text = ok
            if tool:
                allowed = True
                if perm:
                    seq[0] += 1
                    send({"id": seq[0], "method": "item/commandExecution/requestApproval",
                          "params": {"threadId": thread, "turnId": "tu1", "itemId": "i1", "command": "nebula." + tool}})
                    r = recv()
                    allowed = r["result"]["decision"] == "accept"
                item = {"type": "mcpToolCall", "id": "i1", "server": "nebula", "tool": tool, "arguments": args, "status": "inProgress"}
                send({"method": "item/started", "params": {"threadId": thread, "turnId": "tu1", "item": item}})
                if allowed:
                    mcp_call(tool, args)
                send({"method": "item/completed", "params": {"threadId": thread, "turnId": "tu1", "item": dict(item, status="completed" if allowed else "declined")}})
                text = ok if allowed else denied
            send({"method": "item/started", "params": {"threadId": thread, "turnId": "tu1", "item": {"type": "agentMessage", "id": "a1", "text": ""}}})
            send({"method": "item/agentMessage/delta", "params": {"threadId": thread, "turnId": "tu1", "itemId": "a1", "delta": text}})
            send({"method": "turn/completed", "params": {"threadId": thread, "turn": {"id": "tu1", "status": "completed"}}})


{"claude": claude, "codex": codex}[KIND]()
