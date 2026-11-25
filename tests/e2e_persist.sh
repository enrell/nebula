#!/usr/bin/env bash
# Session persistence: shells must survive the GUI dying, layouts must survive the host dying.
set -euo pipefail
BIN=$(realpath "${1:-./build/nebula}")
export NEBULA_SOCKET=${XDG_RUNTIME_DIR:-/tmp}/nebula-e2e-p.sock
export NEBULA_STATE_DIR=$(mktemp -d)
# always headless: never open a window on the user's desktop
unset WAYLAND_DISPLAY DISPLAY
export QT_FORCE_STDERR_LOGGING=1  # Qt logs to journald when stderr is not a tty
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QPA_PLATFORMTHEME= GDK_BACKEND=
TAG=$RANDOM$RANDOM
GUI=
ctl() { "$BIN" ctl "$@"; }
fail() { echo "FAIL: $*" >&2; exit 1; }
start_gui() { "$BIN" & GUI=$!; for _ in $(seq 60); do ctl ping >/dev/null 2>&1 && return; sleep 0.1; done; fail "gui did not start"; }
# kill only the pty daemon bound to THIS test's socket, never the user's real one
kill_host() { for pid in $(pgrep -f "$BIN --host" 2>/dev/null); do tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -qx "NEBULA_SOCKET=$NEBULA_SOCKET" && kill "$pid" 2>/dev/null || true; done; }
cleanup() { ctl action.run action=kill-session >/dev/null 2>&1 || true; kill $GUI 2>/dev/null || true; kill_host
            pkill -f "sleep 9$TAG" 2>/dev/null || true; rm -rf "$NEBULA_STATE_DIR" "$NEBULA_SOCKET" "$NEBULA_SOCKET.host"; }
trap cleanup EXIT
ids() { ctl pane.list | python3 -c 'import json,sys;print(" ".join(str(p["id"]) for p in json.load(sys.stdin)))'; }
wait_read() { for _ in $(seq 40); do ctl pane.read pane="$1" scrollback=true | grep -q "$2" && return 0; sleep 0.1; done; return 1; }

start_gui
P=$(ids)
ctl pane.send_text pane="$P" text="sleep 9$TAG &" enter=true
ctl pane.send_text pane="$P" text="printf 'mark-%s\\n' $TAG" enter=true
wait_read "$P" "^mark-$TAG" || fail "marker not printed"
N=$(ctl pane.split pane="$P" direction=right | python3 -c 'import json,sys;print(json.load(sys.stdin)["pane"])')
ctl space.rename name="my-space"
sleep 1.2                      # let the debounced layout save happen
kill -9 $GUI; wait $GUI 2>/dev/null || true; rm -f "$NEBULA_SOCKET"

pgrep -f "sleep 9$TAG" >/dev/null || fail "shell/job died with the GUI"
start_gui
[ "$(ids)" = "$P $N" ] || fail "panes after reattach: '$(ids)' (want '$P $N')"
wait_read "$P" "^mark-$TAG" || fail "scrollback not replayed on reattach"
[ "$(ctl space.list | python3 -c 'import json,sys;print(json.load(sys.stdin)[0]["name"])')" = my-space ] || fail "space name lost"
pgrep -f "sleep 9$TAG" >/dev/null || fail "job died on reattach"
echo "reattach: ok"

sleep 1; kill -9 $GUI; wait $GUI 2>/dev/null || true; rm -f "$NEBULA_SOCKET"
kill_host; sleep 0.5; pkill -f "sleep 9$TAG" || true
start_gui
[ "$(ids)" = "$P $N" ] || fail "layout not resurrected: '$(ids)'"
pgrep -f "sleep 9$TAG" >/dev/null && fail "old job should be gone after host death"
echo "resurrect: ok"

# ---- Kill all & quit, and a reboot (host and GUI gone): each pane starts its program again, an agent reopens the
# exact conversation its hook reported, with its own flags and without its old prompt; other commands are typed.
ctl action.run action=discard-session >/dev/null 2>&1 || true
for _ in $(seq 50); do kill -0 $GUI 2>/dev/null || break; sleep 0.1; done
kill_host; rm -f "$NEBULA_SOCKET"
FAKE=$(mktemp -d)
mkdir -p "$FAKE/home/.config/nebula"; echo '{"onboarded":true}' > "$FAKE/home/.config/nebula/settings.json"
export HOME="$FAKE/home"   # settings.set below must never touch the real settings
export PATH="$FAKE:$PATH" FAKE_AGENT_LOG="$FAKE/agent.log"
# a stand-in for Claude Code: logs how it was started and, like its Stop hook, hands nebula its session id on stdin
cat > "$FAKE/claude" <<'PY'
#!/usr/bin/env python3
import json, os, subprocess, sys, time
open(os.environ["FAKE_AGENT_LOG"], "a").write(json.dumps(sys.argv[1:]) + "\n")
if "--resume" not in sys.argv:
    subprocess.run([os.environ["NEBULA_BIN"], "ctl", "pane.report_state", "state=idle"],
                   input=json.dumps({"session_id": "sess-" + os.environ["NEBULA_PANE"], "hook_event_name": "Stop"}).encode())
print("fake claude ready", flush=True)
time.sleep(100000)
PY
chmod +x "$FAKE/claude"
printf 'notes-%s\n' "$TAG" > "$FAKE/notes.txt"
start_gui
[ "$(ids | wc -w)" = 1 ] || fail "discard-session should start empty: '$(ids)'"
A=$(ids)
ctl pane.send_text pane="$A" text="claude --dangerously-skip-permissions 'fix the bug'" enter=true
B=$(ctl pane.split pane="$A" direction=right | python3 -c 'import json,sys;print(json.load(sys.stdin)["pane"])')
ctl pane.send_text pane="$B" text="less $FAKE/notes.txt" enter=true
C=$(ctl pane.split pane="$B" direction=down | python3 -c 'import json,sys;print(json.load(sys.stdin)["pane"])')
ctl pane.send_text pane="$C" text="sleep 7$TAG" enter=true
wait_read "$A" "fake claude ready" || fail "fake agent did not start: $(ctl pane.read pane="$A")"
wait_read "$B" "notes-$TAG" || fail "less did not start"
sleep 2                         # the host reports the foreground programs; the layout save is debounced
python3 - "$NEBULA_STATE_DIR/session.json" "$A" "$B" "$C" "$FAKE" "$TAG" <<'PY' || fail "saved session: $(cat "$NEBULA_STATE_DIR/session.json")"
import json, sys
path, a, b, c, fake, tag = sys.argv[1:]
panes = json.load(open(path))["spaces"][0]["tabs"][0]["panes"]
assert panes[a]["cmd"] == f"claude --dangerously-skip-permissions --resume sess-{a}" and panes[a]["run"] is True, panes[a]
assert panes[b]["cmd"] == f"less {fake}/notes.txt" and panes[b]["run"] is True, panes[b]
assert panes[c]["cmd"] == f"sleep 7{tag}" and panes[c]["run"] is False, panes[c]
PY

check_restored() {   # $1: what happened
  wait_read "$A" "fake claude ready" || fail "$1: agent not started again"
  [ "$(tail -1 "$FAKE_AGENT_LOG")" = "[\"--dangerously-skip-permissions\", \"--resume\", \"sess-$A\"]" ] || fail "$1: agent started as $(tail -1 "$FAKE_AGENT_LOG")"
  wait_read "$B" "notes-$TAG" || fail "$1: less not started again"
  wait_read "$C" "sleep 7$TAG" || fail "$1: command not typed"
  sleep 0.5; pgrep -f "^sleep 7$TAG" >/dev/null && fail "$1: a command that is not interactive must only be typed"
  [ "$(ids)" = "$A $B $C" ] || fail "$1: panes '$(ids)'"
}
ctl action.run action=kill-session >/dev/null 2>&1 || true
for _ in $(seq 50); do kill -0 $GUI 2>/dev/null || break; sleep 0.1; done
pgrep -f "notes.txt" >/dev/null && fail "kill-session left programs running"
rm -f "$NEBULA_SOCKET"; start_gui
check_restored "after Kill all & quit"
echo "kill-session restores: ok"

sleep 1.5; kill -9 $GUI; wait $GUI 2>/dev/null || true; kill_host; sleep 0.5; pkill -f "$FAKE/" || true; rm -f "$NEBULA_SOCKET"
start_gui
check_restored "after a reboot"
echo "reboot restores: ok"

ctl settings.set key=restorePrograms value=type >/dev/null
N=$(wc -l < "$FAKE_AGENT_LOG")
ctl action.run action=kill-session >/dev/null 2>&1 || true
for _ in $(seq 50); do kill -0 $GUI 2>/dev/null || break; sleep 0.1; done
rm -f "$NEBULA_SOCKET"; start_gui
wait_read "$A" "resume sess-$A" || fail "restorePrograms=type: command not typed: $(ctl pane.read pane="$A")"
sleep 1; [ "$(wc -l < "$FAKE_AGENT_LOG")" = "$N" ] || fail "restorePrograms=type must not start the agent"
ctl settings.set key=restorePrograms value=run >/dev/null
echo "restore setting: ok"

ctl action.run action=discard-session >/dev/null 2>&1 || true
for _ in $(seq 50); do kill -0 $GUI 2>/dev/null || break; sleep 0.1; done
rm -f "$NEBULA_SOCKET"; start_gui
[ "$(ids | wc -w)" = 1 ] || fail "discard-session: '$(ids)'"
echo "discard-session: ok"
pkill -f "$FAKE/" || true; rm -rf "$FAKE"
# ---- editors: the file comes back in its place in the layout, with text that was never saved
ED=$(mktemp -d)
printf 'first line\n' > "$ED/notes.txt"
E=$(ctl editor.open file="$ED/notes.txt" | python3 -c 'import json,sys;print(json.load(sys.stdin)["pane"])')
ctl pane.send_keys pane="$E" keys='["Ctrl+End"]'
ctl pane.send_text pane="$E" text="unsaved $TAG"
sleep 1.5                      # the session (and the unsaved text with it) is saved on a debounce
sleep 1; kill -9 $GUI; wait $GUI 2>/dev/null || true; kill_host; rm -f "$NEBULA_SOCKET"
start_gui
ctl editor.get pane="$E" text=true | python3 -c "
import json, sys
d = json.load(sys.stdin)
assert d['path'].endswith('notes.txt') and d['dirty'] and 'unsaved $TAG' in d['text'], d
assert 'restored' in d['message'], d" || fail "editor not restored with its unsaved text: $(ctl editor.list)"
[ "$(cat "$ED/notes.txt")" = "first line" ] || fail "restoring must not write the file"
ctl pane.close pane="$E" force=true
ls "$NEBULA_STATE_DIR/editors" 2>/dev/null | grep -q "^$E\." && fail "unsaved text of a closed editor left behind"
rm -rf "$ED"
echo "editor restore: ok"
