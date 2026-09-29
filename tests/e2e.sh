#!/usr/bin/env bash
# Starts an isolated instance and exercises the automation API.
set -euo pipefail
BIN=$(realpath "${1:-./build/nebula}")
export NEBULA_SOCKET=${XDG_RUNTIME_DIR:-/tmp}/nebula-e2e.sock
export NEBULA_STATE_DIR=$(mktemp -d)
# always headless: never open a window on the user's desktop
unset WAYLAND_DISPLAY DISPLAY
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QPA_PLATFORMTHEME= GDK_BACKEND=
# kill only the pty daemon bound to THIS test's socket, never the user's real one
kill_host() { for pid in $(pgrep -f "$BIN --host" 2>/dev/null); do tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -qx "NEBULA_SOCKET=$NEBULA_SOCKET" && kill "$pid" 2>/dev/null || true; done; }
"$BIN" & PID=$!
trap 'kill $PID 2>/dev/null || true; kill_host; rm -rf "$NEBULA_STATE_DIR" "$NEBULA_SOCKET" "$NEBULA_SOCKET.host"' EXIT
for _ in $(seq 50); do "$BIN" ctl ping >/dev/null 2>&1 && break; sleep 0.1; done

fail() { echo "FAIL: $*" >&2; exit 1; }
ctl() { "$BIN" ctl "$@"; }

[ "$(ctl ping)" = pong ] || fail ping
P=$(ctl pane.list | python3 -c 'import json,sys;print(json.load(sys.stdin)[0]["id"])')
ctl pane.send_text pane="$P" text="printf 'e2e-%s\\n' ok" enter=true
for _ in $(seq 30); do ctl pane.read pane="$P" | grep -q '^e2e-ok' && break; sleep 0.1; done
ctl pane.read pane="$P" | grep -q '^e2e-ok' || fail "pane.read did not see command output"

N=$(ctl pane.split pane="$P" direction=right | python3 -c 'import json,sys;print(json.load(sys.stdin)["pane"])')
[ "$(ctl pane.list | python3 -c 'import json,sys;print(len(json.load(sys.stdin)))')" = 2 ] || fail "split"
ctl pane.send_keys pane="$N" keys='["e","x","i","t","Enter"]'
for _ in $(seq 30); do [ "$(ctl pane.list | python3 -c 'import json,sys;print(len(json.load(sys.stdin)))')" = 1 ] && break; sleep 0.1; done
[ "$(ctl pane.list | python3 -c 'import json,sys;print(len(json.load(sys.stdin)))')" = 1 ] || fail "pane did not close after exit"

ctl action.run action=new-tab
[ "$(ctl space.list | python3 -c 'import json,sys;print(len(json.load(sys.stdin)[0]["tabs"]))')" = 2 ] || fail "new-tab"
ctl pane.report_state pane="$P" state=working 2>/dev/null && true
ctl ping >/dev/null
echo "e2e: ok"
