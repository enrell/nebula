#!/usr/bin/env bash
# Regenerates docs/screenshots/*.png headlessly, in a throw-away HOME with fake agents (no accounts, no personal data).
#   tests/screenshots.sh [path/to/nebula]
set -euo pipefail
BIN=$(realpath "${1:-./build/nebula}")
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(realpath "$HERE/../docs/screenshots")
export HOME=$(mktemp -d)
unset WAYLAND_DISPLAY DISPLAY
export QT_FORCE_STDERR_LOGGING=1  # Qt logs to journald when stderr is not a tty
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QPA_PLATFORMTHEME= GDK_BACKEND= NEBULA_SECRETS=file
export NEBULA_SOCKET=${XDG_RUNTIME_DIR:-/tmp}/nebula-shots.sock NEBULA_STATE_DIR=$HOME/state FAKEACP_LOG=$HOME/acp.log
GUI=
ctl() { "$BIN" ctl "$@"; }
kill_host() { for pid in $(pgrep -f "$BIN --host" 2>/dev/null); do tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -qx "NEBULA_SOCKET=$NEBULA_SOCKET" && kill "$pid" 2>/dev/null || true; done; }
stop() { ctl action.run action=kill-session >/dev/null 2>&1 || true; kill $GUI 2>/dev/null || true; wait $GUI 2>/dev/null || true; kill_host; rm -f "$NEBULA_SOCKET" "$NEBULA_SOCKET.host"; }
start() { "$BIN" >/dev/null 2>&1 & GUI=$!; for _ in $(seq 60); do ctl ping >/dev/null 2>&1 && break; sleep 0.1; done; ctl window.resize width=1200 height=760 >/dev/null; }
trap 'stop; rm -rf "$HOME"' EXIT

mkdir -p "$HOME/fakebin" "$HOME/.config/nebula" "$HOME/proj"
printf '#!/bin/sh\nexec python3 "%s/fake_native.py" claude "$@"\n' "$HERE" > "$HOME/fakebin/claude"; chmod +x "$HOME/fakebin/claude"
# a stand-in "agent" for the sidebar: detection goes by process name, so it must be called claude (outside PATH)
mkdir -p "$HOME/agentbin"
printf '#!/bin/sh\necho "  claude code v2 · Sonnet"; echo; echo "> refactor the session store"; echo "  reading src/session.cpp ..."; sleep 600\n' > "$HOME/agentbin/claude"
chmod +x "$HOME/agentbin/claude"
export PATH="$HOME/fakebin:$PATH"
cd "$HOME/proj" && git init -q . && git -c user.email=a@b -c user.name=demo commit -q --allow-empty -m "init" && printf 'fn main() {}\n' > main.rs && git add . && git -c user.email=a@b -c user.name=demo commit -q -m "add main"

# 1) main window: two panes + an agent
echo '{"onboarded":true,"operatorAgent":"native:claude"}' > "$HOME/.config/nebula/settings.json"
start
ctl space.rename space=0 name=demo >/dev/null
P=$(ctl pane.list | python3 -c 'import json,sys;print(json.load(sys.stdin)[0]["id"])')
ctl pane.send_text pane="$P" text="PS1='\$ '; cd ~/proj; clear; git log --oneline --decorate; ls" enter=true >/dev/null
R=$(ctl pane.split pane="$P" direction=right | python3 -c 'import json,sys;print(json.load(sys.stdin)["pane"])')
ctl pane.send_text pane="$R" text="PS1='\$ '; cd ~/proj; clear; $HOME/agentbin/claude" enter=true >/dev/null
sleep 2; ctl pane.focus pane="$P" >/dev/null; sleep 0.5
ctl window.screenshot path="$OUT/main.png" >/dev/null

# 2) the operator chat
ctl operator.ask prompt="please launch a shell" approve=all >/dev/null
ctl action.run action=operator >/dev/null; sleep 4
ctl window.screenshot path="$OUT/operator.png" >/dev/null
ctl action.run action=operator >/dev/null; ctl action.run action=toggle-help >/dev/null 2>&1 || true
stop

# 3) settings + 4) wizard (fresh state each)
rm -rf "$NEBULA_STATE_DIR"
start
ctl action.run action=open-settings >/dev/null; sleep 1
ctl window.screenshot path="$OUT/settings.png" >/dev/null
stop
rm -rf "$NEBULA_STATE_DIR"; rm -f "$HOME/.config/nebula/settings.json"
start; sleep 1.5
ctl window.screenshot path="$OUT/wizard.png" >/dev/null
echo "screenshots written to $OUT"
