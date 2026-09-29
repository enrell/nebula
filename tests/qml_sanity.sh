#!/usr/bin/env bash
# Starts nebula headlessly, opens every overlay/page so all QML gets instantiated, and fails on any QML warning.
# Meant for a RELEASE build: the AOT-compiled QML reports problems (wrong property use, overridden members) that the
# interpreted debug build stays silent about.
#   tests/qml_sanity.sh path/to/nebula
set -euo pipefail
BIN=$(realpath "${1:-./build/nebula}")
export HOME=$(mktemp -d)
unset WAYLAND_DISPLAY DISPLAY
export QT_FORCE_STDERR_LOGGING=1  # Qt logs to journald when stderr is not a tty
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QPA_PLATFORMTHEME= GDK_BACKEND= NEBULA_SECRETS=file
export NEBULA_SOCKET=${XDG_RUNTIME_DIR:-/tmp}/nebula-qml-sanity.sock NEBULA_STATE_DIR=$HOME/state
LOG=$HOME/gui.log GUI=
ctl() { "$BIN" ctl "$@"; }
cleanup() { ctl action.run action=kill-session >/dev/null 2>&1 || true; kill $GUI 2>/dev/null || true; for pid in $(pgrep -f "$BIN --host" 2>/dev/null); do tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -qx "NEBULA_SOCKET=$NEBULA_SOCKET" && kill "$pid" 2>/dev/null || true; done; rm -rf "$HOME" "$NEBULA_SOCKET" "$NEBULA_SOCKET.host"; }
trap cleanup EXIT
"$BIN" >"$LOG" 2>&1 & GUI=$!
for _ in $(seq 60); do ctl ping >/dev/null 2>&1 && break; sleep 0.1; done
ctl ping >/dev/null || { cat "$LOG"; echo "qml sanity: gui did not start" >&2; exit 1; }
sleep 1   # first-run wizard is up
for a in setup-wizard open-settings operator launch-agent toggle-help; do ctl action.run action=$a >/dev/null; sleep 0.4; done
if grep -E "TypeError|ReferenceError|is not a function|is not defined|Unable to assign|overrides a member|qt\.qml\." "$LOG"; then
  echo "qml sanity: QML warnings above" >&2; exit 1
fi
echo "qml sanity: ok"
