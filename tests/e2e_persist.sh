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
