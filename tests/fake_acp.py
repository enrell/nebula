#!/usr/bin/env python3
"""Fake ACP agent for e2e tests. Speaks ndjson JSON-RPC (ACP) on stdio and drives the real
`nebula mcp` server it was configured with in session/new, so the whole path is real except the model."""
import json, os, subprocess, sys

LOG = os.environ.get("FAKEACP_LOG", "/tmp/fake_acp.log")
session = None
mcp = None
req_seq = 100


def log(**kw):
    with open(LOG, "a") as f:
        f.write(json.dumps(kw) + "\n")


def recv():
    line = sys.stdin.readline()
    if not line:
        sys.exit(0)
    return json.loads(line)


def send(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def mcp_call(name, args):
    mcp.stdin.write(json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {"name": name, "arguments": args}}) + "\n")
    mcp.stdin.flush()
    while True:
        r = json.loads(mcp.stdout.readline())
        if r.get("id") == 1:
            return r


def start_mcp(cfg):
    env = dict(os.environ)
    for e in cfg.get("env", []):
        env[e["name"]] = e["value"]
    p = subprocess.Popen([cfg["command"]] + cfg["args"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, env=env)
    p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": 0, "method": "initialize",
                              "params": {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "fake", "version": "0"}}}) + "\n")
    p.stdin.flush()
    json.loads(p.stdout.readline())
    p.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
    p.stdin.flush()
    return p


def chunk(text):
    send({"jsonrpc": "2.0", "method": "session/update",
          "params": {"sessionId": session, "update": {"sessionUpdate": "agent_message_chunk", "content": {"type": "text", "text": text}}}})


def tool_call(title, raw):
    send({"jsonrpc": "2.0", "method": "session/update",
          "params": {"sessionId": session, "update": {"sessionUpdate": "tool_call", "toolCallId": "t1", "title": title, "rawInput": raw, "status": "completed"}}})


def ask_permission(title, raw):
    global req_seq
    req_seq += 1
    pid = req_seq
    send({"jsonrpc": "2.0", "id": pid, "method": "session/request_permission",
          "params": {"sessionId": session, "toolCall": {"toolCallId": "t1", "title": title, "rawInput": raw},
                     "options": [{"optionId": "a", "kind": "allow_once", "name": "Allow"}, {"optionId": "r", "kind": "reject_once", "name": "Reject"}]}})
    while True:
        r = recv()
        if r.get("id") == pid:
            out = r.get("result", {}).get("outcome", {})
            return out.get("outcome") == "selected" and out.get("optionId") == "a"


def on_prompt(p, msgid):
    global session, mcp
    texts = [b.get("text", "") for b in p.get("prompt", [])]
    prompt = texts[-1] if texts else ""
    log(prompt=prompt)
    if "launch a shell" in prompt:
        tool_call("launch_agent", {"agent": "shell"})
        mcp_call("launch_agent", {"agent": "shell"})
        chunk("launched a shell")
    elif "close pane" in prompt:
        pane = int(prompt.split("close pane ")[1].split()[0])
        tool_call("close_pane", {"pane": pane})
        if ask_permission("close_pane", {"pane": pane}):
            mcp_call("close_pane", {"pane": pane})
            chunk("closed pane %d" % pane)
        else:
            chunk("not allowed")
    else:
        chunk("operator-done")
    send({"jsonrpc": "2.0", "id": msgid, "result": {"stopReason": "end_turn"}})


while True:
    m = recv()
    meth = m.get("method")
    p = m.get("params") or {}
    if meth == "initialize":
        send({"jsonrpc": "2.0", "id": m["id"],
              "result": {"protocolVersion": 1, "agentCapabilities": {}, "authMethods": [], "agentInfo": {"name": "fake-acp", "version": "0.0"}}})
    elif meth == "session/new":
        session = "s1"
        log(mcpServers=p.get("mcpServers"))
        cfg = next((s for s in p.get("mcpServers", []) if s.get("name") == "nebula"), None)
        if cfg:
            mcp = start_mcp(cfg)
        # advertise a stale configured model: "stale-9x" is injected with no description (like a real
        # adapter does for an unknown configured model), "good-1" is the only real choice
        result = {"sessionId": session, "configOptions": [
            {"id": "model", "name": "Model", "category": "model", "type": "select", "currentValue": "stale-9x",
             "options": [{"value": "stale-9x", "name": "Stale 9x", "description": None},
                         {"value": "good-1", "name": "Good 1", "description": "the default model"}]}]}
        send({"jsonrpc": "2.0", "id": m["id"], "result": result})
    elif meth == "session/set_config_option":
        log(setConfigOption=p)
        send({"jsonrpc": "2.0", "id": m["id"], "result": {}})
    elif meth == "session/prompt":
        on_prompt(p, m["id"])
    # session/cancel and anything else: ignore
