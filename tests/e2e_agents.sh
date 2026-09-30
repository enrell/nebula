#!/usr/bin/env bash
# Profiles/secrets, launcher, LLM plumbing, integrations installer and the MCP bridge - all headless, with a throw-away HOME.
set -euo pipefail
BIN=$(realpath "${1:-./build/nebula}")
HERE=$(cd "$(dirname "$0")" && pwd)
export HOME=$(mktemp -d)
unset WAYLAND_DISPLAY DISPLAY
export QT_FORCE_STDERR_LOGGING=1  # Qt logs to journald when stderr is not a tty
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QPA_PLATFORMTHEME= GDK_BACKEND= NEBULA_SECRETS=file
export NEBULA_SOCKET=${XDG_RUNTIME_DIR:-/tmp}/nebula-e2e-a.sock
export NEBULA_STATE_DIR=$HOME/state
PORT=$((20000 + RANDOM % 20000))
export FAKEACP_LOG=$HOME/acp.log    # read by the fake ACP agent process (child of the GUI)
LOG=$HOME/llm.log
GUI=; FAKE=
ctl() { "$BIN" ctl "$@"; }
fail() { echo "FAIL: $*" >&2; cat "$HOME"/fake_*.err "$HOME/acp.log" >&2 2>/dev/null; exit 1; }
# kill only the pty daemon bound to THIS test's socket, never the user's real one
kill_host() { for pid in $(pgrep -f "$BIN --host" 2>/dev/null); do tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -qx "NEBULA_SOCKET=$NEBULA_SOCKET" && kill "$pid" 2>/dev/null || true; done; }
cleanup() { ctl action.run action=kill-session >/dev/null 2>&1 || true; kill $GUI $FAKE 2>/dev/null || true; kill_host; rm -rf "$HOME" "$NEBULA_SOCKET" "$NEBULA_SOCKET.host"; }
trap cleanup EXIT

# fake `claude` / `codex` CLIs so the native operator drivers can be exercised without network or accounts
mkdir -p "$HOME/fakebin"
for k in claude codex; do printf '#!/bin/sh\nexec python3 "%s/fake_native.py" %s "$@" 2>>"%s/fake_%s.err"\n' "$HERE" "$k" "$HOME" "$k" > "$HOME/fakebin/$k"; chmod +x "$HOME/fakebin/$k"; done
export PATH="$HOME/fakebin:$PATH"

python3 "$HERE/fake_llm.py" "$LOG" "$PORT" & FAKE=$!
"$BIN" & GUI=$!
for _ in $(seq 60); do ctl ping >/dev/null 2>&1 && break; sleep 0.1; done
ctl ping >/dev/null || fail "gui did not start"
wait_read() { for _ in $(seq 40); do ctl pane.read pane="$1" scrollback=true | grep -q "$2" && return 0; sleep 0.1; done; return 1; }

# ---- profiles & secrets
ctl profile.set name=work provider=anthropic key=sk-work "env={\"NEBULA_BASE_URL\":\"http://127.0.0.1:$PORT\"}"
ctl profile.set name=oai provider=openai key=sk-oai "env={\"NEBULA_BASE_URL\":\"http://127.0.0.1:$PORT/v1\"}"
ctl profile.set name=gem provider=gemini key=sk-gem "env={\"NEBULA_BASE_URL\":\"http://127.0.0.1:$PORT\"}"
ctl profile.set name=badp provider=anthropic key=bad "env={\"NEBULA_BASE_URL\":\"http://127.0.0.1:$PORT\"}"
ctl profile.list | grep -q sk- && fail "profile.list leaked a secret"
grep -q sk-work "$HOME/.config/nebula/profiles.json" && fail "secret written to profiles.json"
[ "$(stat -c %a "$HOME/.config/nebula/secrets.json")" = 600 ] || fail "secrets file not 0600"
echo "profiles: ok"

# ---- LLM providers
ctl profile.test name=work; ctl profile.test name=oai; ctl profile.test name=gem
{ ctl profile.test name=badp 2>&1 || true; } | grep -q "invalid api key" || fail "bad key should report the provider error"
[ "$(ctl llm.ask prompt=hi profile=work)" = "echo fake-reply" ] || fail "anthropic reply"
grep -q '"x-api-key": "sk-work"' "$LOG" || fail "anthropic auth header"
grep -q '/v1/chat/completions' "$LOG" && grep -q 'Bearer sk-oai' "$LOG" || fail "openai request"
grep -q 'x-goog-api-key' "$LOG" || fail "gemini request"
echo "llm: ok"

# ---- launcher: profile env injection, cwd, worktree
P=$(ctl pane.list | python3 -c 'import json,sys;print(json.load(sys.stdin)[0]["id"])')
R=$(ctl agent.launch agent=shell profile=work where=split-right)
N=$(echo "$R" | python3 -c 'import json,sys;print(json.load(sys.stdin)["pane"])')
ctl pane.send_text pane="$N" text='echo "key=$ANTHROPIC_API_KEY prof=$NEBULA_PROFILE"' enter=true
wait_read "$N" "^key=sk-work prof=work" || fail "profile env not injected into the pane"
ctl pane.send_text pane="$P" text='echo "key=[$ANTHROPIC_API_KEY]"' enter=true
wait_read "$P" "^key=\[\]" || fail "profile leaked into a pane that did not ask for it"

REPO=$HOME/repo; mkdir -p "$REPO"; git -C "$REPO" init -q; git -C "$REPO" -c user.email=a@b -c user.name=t commit -q --allow-empty -m init
W=$(ctl agent.launch agent=shell cwd="$REPO" worktree=true branch=feat/x where=tab)
echo "$W" | grep -q "repo-feat-x" || fail "worktree path: $W"
[ -d "$REPO-feat-x" ] || fail "worktree not created"
# from inside that worktree, the next one is its sibling, branched off the main checkout
W=$(ctl agent.launch agent=shell cwd="$REPO-feat-x" worktree=true branch=feat/y where=tab)
echo "$W" | grep -q '"/[^"]*/repo-feat-y"' || fail "worktree from a worktree should be a sibling: $W"
{ ctl agent.launch agent=shell cwd=/nonexistent 2>&1 || true; } | grep -q "does not exist" || fail "bad cwd should error"
{ ctl agent.launch agent=shell profile=nope 2>&1 || true; } | grep -q "unknown profile" || fail "unknown profile should error"
ctl agent.presets | grep -q claude || fail "presets"
echo "launcher: ok"

# ---- integrations (throw-away HOME)
mkdir -p "$HOME/.claude" "$HOME/.codex" "$HOME/.gemini" "$HOME/.config/opencode"
echo '{"theme":"dark","hooks":{"Stop":[{"hooks":[{"type":"command","command":"echo mine"}]}]}}' > "$HOME/.claude/settings.json"
echo '{"model":"x"}' > "$HOME/.gemini/settings.json"
printf 'model = "gpt"\n\n[tui]\nfoo = 1\n' > "$HOME/.codex/config.toml"
for a in claude opencode codex; do ctl integration.install agent=$a kind=hooks; done
ctl integration.install agent=claude kind=hooks   # idempotent
python3 - "$HOME" <<'PY'
import json, sys
h = sys.argv[1]
s = json.load(open(f"{h}/.claude/settings.json"))
assert s["theme"] == "dark", "settings clobbered"
stop = s["hooks"]["Stop"]
assert any("echo mine" in x["hooks"][0]["command"] for x in stop), "user hook lost"
assert sum("pane.report_state" in x["hooks"][0]["command"] for x in stop) == 1, "duplicate hook"
assert set(s["hooks"]) == {"UserPromptSubmit", "PreToolUse", "Notification", "Stop", "SessionEnd"}
PY
grep -q '^notify = \[' "$HOME/.codex/config.toml" || fail "codex notify missing"
[ "$(head -1 "$HOME/.codex/config.toml")" != "" ] && sed -n '/^notify/,$p' "$HOME/.codex/config.toml" | grep -q '^\[tui\]' || fail "codex notify must precede tables"
[ -f "$HOME/.config/opencode/plugins/nebula.js" ] || fail "opencode plugin"
# the installed hook command must be a no-op success outside nebula and report inside a pane
CMD=$(python3 -c "import json,os;print([x for x in json.load(open(os.environ['HOME']+'/.claude/settings.json'))['hooks']['Stop'] if 'report_state' in x['hooks'][0]['command']][0]['hooks'][0]['command'])")
env -u NEBULA_PANE bash -c "$CMD" || fail "hook must succeed outside nebula"
ctl pane.send_text pane="$P" text="$(python3 -c "import json,os;print([x for x in json.load(open(os.environ['HOME']+'/.claude/settings.json'))['hooks']['Stop'] if 'report_state' in x['hooks'][0]['command']][0]['hooks'][0]['command'])")" enter=true
for a in opencode gemini codex; do ctl integration.install agent=$a kind=mcp; done
python3 - "$HOME" <<'PY'
import json, sys
h = sys.argv[1]
assert "nebula" in json.load(open(f"{h}/.config/opencode/opencode.json"))["mcp"]
g = json.load(open(f"{h}/.gemini/settings.json")); assert g["model"] == "x" and "nebula" in g["mcpServers"]
PY
grep -q '\[mcp_servers.nebula\]' "$HOME/.codex/config.toml" || fail "codex mcp"
ctl integration.list | grep -q '"mcp": true' || fail "integration.list"
for a in claude opencode codex; do ctl integration.remove agent=$a kind=hooks; done
for a in opencode gemini codex; do ctl integration.remove agent=$a kind=mcp; done
python3 - "$HOME" <<'PY'
import json, sys
h = sys.argv[1]
s = json.load(open(f"{h}/.claude/settings.json"))
assert s["hooks"]["Stop"][0]["hooks"][0]["command"] == "echo mine" and len(s["hooks"]) == 1, s
assert "mcpServers" not in json.load(open(f"{h}/.gemini/settings.json"))
PY
! grep -q 'nebula\|report_state' "$HOME/.codex/config.toml" || fail "codex config not restored"
echo "integrations: ok"

# ---- exact state via report_state + AI summary on "done"
mkdir -p "$HOME/bin"; printf '#!/bin/sh\nsleep 60\n' > "$HOME/bin/claude"; chmod +x "$HOME/bin/claude"
ctl pane.focus pane="$N"                       # P becomes an unfocused pane
ctl settings.set key=aiProfile value=work; ctl settings.set key=aiSummaries value=true
ctl pane.send_text pane="$P" text="$HOME/bin/claude" enter=true
state() { ctl pane.get pane="$P" | python3 -c 'import json,sys;print(json.load(sys.stdin)["state"])'; }
for _ in $(seq 40); do [ -n "$(state)" ] && break; sleep 0.2; done
[ -n "$(state)" ] || fail "agent not detected"
ctl pane.focus pane="$P"; sleep 1.5; ctl pane.focus pane="$N"; sleep 1     # settle: clear the startup activity
ctl pane.report_state pane="$P" state=working; sleep 1
[ "$(state)" = working ] || fail "reported working, got '$(state)'"
ctl pane.report_state pane="$P" state=blocked; sleep 1
[ "$(state)" = blocked ] || fail "reported blocked, got '$(state)'"
ctl pane.report_state pane="$P" state=working; sleep 1
ctl pane.report_state pane="$P" state=idle
for _ in $(seq 40); do [ "$(state)" = done ] && break; sleep 0.2; done
[ "$(state)" = done ] || fail "unfocused agent should be 'done', got '$(state)'"
for _ in $(seq 40); do ctl pane.get pane="$P" | grep -q '"summary": "echo fake-reply"' && break; sleep 0.2; done
ctl pane.get pane="$P" | grep -q '"summary": "echo fake-reply"' || fail "AI summary missing"
ctl pane.focus pane="$P"; sleep 1
[ "$(state)" = idle ] || fail "focusing a done agent should clear it, got '$(state)'"
echo "state+summary: ok"

# ---- operator: ACP agent driving the real nebula MCP bridge
count() { ctl pane.list | python3 -c 'import json,sys;print(len(json.load(sys.stdin)))'; }
ctl settings.set key=operatorAgent value="python3 $HERE/fake_acp.py"
before=$(count)
[ "$(ctl operator.ask prompt="please launch a shell" approve=all)" = "launched a shell" ] || fail "operator final answer"
[ "$(count)" = $((before + 1)) ] || fail "operator did not launch a pane through the MCP bridge"
python3 - "$HOME/acp.log" <<'PY'
import json, sys
lines = [json.loads(l) for l in open(sys.argv[1])]
mcps = [l["mcpServers"] for l in lines if "mcpServers" in l]
assert mcps and any(s["name"] == "nebula" and s["command"].endswith("nebula") and s["args"] == ["mcp"] for s in mcps[0]), mcps
prompt = [l["prompt"] for l in lines if "prompt" in l][0]
assert "operating nebula" in prompt and "[workspace snapshot]" in prompt
sco = [l["setConfigOption"] for l in lines if "setConfigOption" in l]
assert sco and sco[0]["configId"] == "model" and sco[0]["value"] == "good-1", sco  # stale model healed
PY
X=$(ctl pane.split pane="$N" direction=down | python3 -c 'import json,sys;print(json.load(sys.stdin)["pane"])')
ctl operator.ask prompt="close pane $X" approve=none >/dev/null
ctl pane.get pane="$X" >/dev/null || fail "denied close_pane still closed the pane"
[ "$(ctl operator.ask prompt="close pane $X" approve=all)" = "closed pane $X" ] || fail "approved close_pane answer"
{ ctl pane.get pane="$X" 2>&1 || true; } | grep -q "no such pane" || fail "approved close_pane did not close the pane"
echo "operator: ok"

# ---- operator: native claude (stream-json) and codex (app-server) drivers
for K in claude codex; do
  : > "$HOME/acp.log"
  ctl settings.set key=operatorAgent value="native:$K" >/dev/null
  ctl settings.set key=operatorModel value="$([ $K = claude ] && echo fast || echo good-2)" >/dev/null
  before=$(count)
  [ "$(ctl operator.ask prompt="please launch a shell" approve=all)" = "launched a shell" ] || fail "$K: operator final answer"
  [ "$(count)" = $((before + 1)) ] || fail "$K: operator did not launch a pane through the MCP bridge"
  python3 - "$HOME/acp.log" "$K" <<'PY'
import json, sys
lines = [json.loads(l) for l in open(sys.argv[1])]
k = sys.argv[2]
assert any("prompt" in l and "operating nebula" in l["prompt"] for l in lines), lines
if k == "claude":
    argv = [l["argv"] for l in lines if "argv" in l][0]
    assert "--strict-mcp-config" in argv and "--input-format" in argv and "stream-json" in argv, argv
    assert argv[argv.index("--permission-prompt-tool") + 1] == "stdio", argv
    assert [l["setModel"] for l in lines if "setModel" in l] == ["fast"], lines   # configured model applied via set_model
else:
    ts = [l["threadStart"] for l in lines if "threadStart" in l][0]
    assert ts["model"] == "good-1" and ts["servers"] == ["nebula"], ts            # always names a model, injects the nebula MCP server
    assert [l["model"] for l in lines if "prompt" in l][0] == "good-2", lines      # configured model used for the turn
PY
  # split the first pane, not $N: the MCP bridge check below reads $N and a shrunken pane loses its text
  X=$(ctl pane.split pane="$(ctl pane.list | python3 -c 'import json,sys;print(json.load(sys.stdin)[0]["id"])')" direction=down | python3 -c 'import json,sys;print(json.load(sys.stdin)["pane"])')
  ctl operator.ask prompt="close pane $X" approve=none >/dev/null
  ctl pane.get pane="$X" >/dev/null || fail "$K: denied close_pane still closed the pane"
  [ "$(ctl operator.ask prompt="close pane $X" approve=all)" = "closed pane $X" ] || fail "$K: approved close_pane answer"
  { ctl pane.get pane="$X" 2>&1 || true; } | grep -q "no such pane" || fail "$K: approved close_pane did not close the pane"
  echo "operator native $K: ok"
done
sleep 1.2; grep -q "closed pane" "$NEBULA_STATE_DIR/operator-history.json" || fail "operator history was not persisted"
echo "operator history: ok"


# ---- MCP bridge
OUT=$( (printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"t","version":"1"}}}' \
  '{"jsonrpc":"2.0","method":"notifications/initialized"}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
  "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"read_pane\",\"arguments\":{\"pane\":$N,\"lines\":300}}}" \
  '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"list_profiles","arguments":{}}}' \
  '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"nope","arguments":{}}}' ) | "$BIN" mcp)
echo "$OUT" | python3 -c '
import json, sys
r = {m["id"]: m for m in map(json.loads, sys.stdin)}
assert r[1]["result"]["protocolVersion"] == "2025-03-26" and "tools" in r[1]["result"]["capabilities"]
names = {t["name"] for t in r[2]["result"]["tools"]}
assert {"launch_agent", "read_pane", "send_text", "wait_for_state", "list_panes", "whoami"} <= names, names
assert "key=sk-work" in r[3]["result"]["content"][0]["text"], r[3]["result"]["content"][0]["text"][-600:]
assert "sk-" not in r[4]["result"]["content"][0]["text"], "list_profiles leaked a secret"
assert r[5]["error"]["code"] == -32602
' || fail "mcp bridge"
echo "mcp: ok"
