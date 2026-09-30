#!/usr/bin/env python3
"""Scripted stand-ins for tests/media.sh, so the README GIFs need no accounts, network or personal data.

  media_standin.py claude|codex [prompt]   a coding agent in a pane: prints like one, reports its state to nebula
  media_standin.py claude -p ...           the operator (Claude Code's stream-json protocol), driving the real
                                           `nebula mcp` server it is given, like tests/fake_native.py
"""
import json, os, subprocess, sys, time

KIND = sys.argv[1]
ARGS = sys.argv[2:]
DIM, BOLD, RESET = "\033[2m", "\033[1m", "\033[0m"
MAGENTA, GREEN, RED, YELLOW, CYAN = "\033[35m", "\033[32m", "\033[31m", "\033[33m", "\033[36m"


def state(s):
    subprocess.run(["nebula", "ctl", "pane.report_state", f"state={s}", "ttl=900"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def say(text="", pause=0.35):
    print(text, flush=True)
    time.sleep(pause)


def agent():
    prompt = " ".join(a for a in ARGS if not a.startswith("-")) or "fix the flaky auth test"
    name = {"claude": f"{MAGENTA}✻{RESET} {BOLD}claude{RESET}", "codex": f"{CYAN}>_{RESET} {BOLD}codex{RESET}"}[KIND]
    branch = os.path.basename(os.getcwd())
    say(f"{name}  {DIM}· {branch}{RESET}\n", 0.4)
    say(f"> {prompt}\n", 0.8)
    state("working")
    if KIND == "claude":
        say("● Read(tests/test_auth.py)", 0.5)
        say(f"  {DIM}⎿ 212 lines{RESET}", 0.6)
        say("● Bash(pytest tests/test_auth.py -k expiry --count 20)", 1.4)
        say(f"  {DIM}⎿{RESET} {RED}3 failed{RESET}, 17 passed {DIM}(token expiry race){RESET}", 0.9)
        say("● Update(src/auth/session.py)", 0.4)
        say(f"  {RED}-    if time.time() > self.expires_at:{RESET}", 0.2)
        say(f"  {GREEN}+    if time.monotonic() > self.expires_at:{RESET}", 1.0)
        say("● Bash(pytest tests/test_auth.py -k expiry --count 50)", 1.6)
        say(f"  {DIM}⎿{RESET} {GREEN}50 passed{RESET}", 0.6)
        state("done")
        say(f"\n{GREEN}✓{RESET} The expiry check compared wall-clock time; it now uses a monotonic clock.", 0.2)
    else:
        say("• Explored tests/test_auth.py, src/auth/session.py", 0.9)
        say("• Proposed: freeze time in the fixture instead of sleeping", 0.9)
        say(f"\n{YELLOW}Allow command?{RESET} pytest tests/test_auth.py -x  {DIM}[y/n]{RESET}", 0.1)
        state("blocked")
        sys.stdin.readline()
        state("working")
        say(f"{DIM}  approved{RESET}", 0.4)
        say("• Ran pytest tests/test_auth.py -x", 1.4)
        say(f"  {GREEN}14 passed{RESET} in 2.31s", 0.6)
        state("done")
        say(f"\n{GREEN}✓{RESET} Fixture now uses freezegun; no sleeps left in the auth tests.", 0.2)
    time.sleep(3600)


# ---- the operator: Claude Code stream-json on stdin/stdout, tools through the real nebula MCP server

def send(o):
    sys.stdout.write(json.dumps(o) + "\n")
    sys.stdout.flush()


def operator():
    cfg = json.loads(ARGS[ARGS.index("--mcp-config") + 1])["mcpServers"]["nebula"]
    env = dict(os.environ, **cfg.get("env", {}))
    mcp = subprocess.Popen([cfg["command"]] + cfg.get("args", []), stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, env=env)

    def rpc(method, params, rid):
        mcp.stdin.write(json.dumps({"jsonrpc": "2.0", "id": rid, "method": method, "params": params}) + "\n")
        mcp.stdin.flush()
        while True:
            r = json.loads(mcp.stdout.readline())
            if r.get("id") == rid:
                return r

    rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "standin", "version": "0"}}, 0)
    mcp.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
    mcp.stdin.flush()
    msg = [0]

    def stream(text):
        msg[0] += 1
        mid = f"m{msg[0]}"
        send({"type": "stream_event", "event": {"type": "message_start", "message": {"id": mid}}})
        for word in text.split(" "):
            send({"type": "stream_event", "event": {"type": "content_block_delta", "index": 0, "delta": {"type": "text_delta", "text": word + " "}}})
            time.sleep(0.05)
        send({"type": "assistant", "message": {"id": mid, "content": [{"type": "text", "text": text}]}})

    def tool(name, args):
        msg[0] += 1
        tid = f"t{msg[0]}"
        send({"type": "assistant", "message": {"id": f"m{msg[0]}", "content": [{"type": "tool_use", "id": tid, "name": "mcp__nebula__" + name, "input": args}]}})
        r = rpc("tools/call", {"name": name, "arguments": args}, msg[0] + 100)
        text = r.get("result", {}).get("content", [{}])[0].get("text", "")
        send({"type": "user", "message": {"role": "user", "content": [{"type": "tool_result", "tool_use_id": tid, "content": text}]}})
        time.sleep(0.6)
        return text

    while True:
        line = sys.stdin.readline()
        if not line:
            return
        m = json.loads(line)
        if m["type"] == "control_request":
            body = {"models": [{"value": "default", "displayName": "Default", "description": "recommended"}]} if m["request"]["subtype"] == "initialize" else {}
            send({"type": "control_response", "response": {"subtype": "success", "request_id": m["request_id"], "response": body}})
        elif m["type"] == "user":
            stream("On it: one agent each, in separate git worktrees so their edits can't collide.")
            task, repo = "fix the flaky auth test", os.getcwd()   # both worktrees branch off the project, not off each other
            tool("launch_agent", {"agent": "claude", "prompt": task, "cwd": repo, "worktree": True, "branch": "fix/auth-claude", "where": "split-right"})
            tool("launch_agent", {"agent": "codex", "prompt": task, "cwd": repo, "worktree": True, "branch": "fix/auth-codex", "where": "split-down"})
            stream("Both are running: claude on fix/auth-claude, codex on fix/auth-codex. "
                   "Their status is in the sidebar; I'll flag whichever one needs you.")
            send({"type": "result", "subtype": "success", "is_error": False, "result": "launched"})


if KIND == "claude" and "--input-format" in ARGS:
    operator()
else:
    agent()
