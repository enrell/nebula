#!/usr/bin/env bash
# A screenshot of nebula on a real Windows window (the qwindows platform plugin, software rendering), for Git Bash.
# Also proves that the GUI comes up on a real window, which the offscreen end-to-end test does not.
#   tests/shot_windows.sh path/to/nebula.exe outdir
set -euo pipefail
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'
BIN=$(realpath "${1:-./build/nebula.exe}")
OUT=$(realpath -m "${2:-shots}")
mkdir -p "$OUT"
export NEBULA_SOCKET=nebula-shot-win
STATE=$(mktemp -d); NEBULA_STATE_DIR=$(cygpath -w "$STATE"); export NEBULA_STATE_DIR
export APPDATA=$(cygpath -w "$STATE/appdata")
mkdir -p "$STATE/appdata/nebula"
echo '{"onboarded":true}' > "$STATE/appdata/nebula/settings.json"
unset QT_QPA_PLATFORM
export QT_QUICK_BACKEND=software
LOG=$STATE/gui.log
ctl() { "$BIN" ctl "$@"; }
cleanup() { ctl action.run action=kill-session >/dev/null 2>&1 || true; powershell -NoProfile -Command "Get-Process nebula -ErrorAction SilentlyContinue | Stop-Process -Force" || true; cp "$LOG" "$OUT/gui.log" 2>/dev/null || true; rm -rf "$STATE"; }
trap cleanup EXIT
fail() { echo "FAIL: $*" >&2; tail -30 "$LOG" >&2 || true; exit 1; }

"$BIN" >"$LOG" 2>&1 &
for _ in $(seq 300); do ctl ping >/dev/null 2>&1 && break; sleep 0.2; done
ctl ping >/dev/null 2>&1 || fail "gui did not start on a real window"
ctl window.resize width=1200 height=720 >/dev/null
P=$(ctl pane.list | grep -o '"id": *[0-9]*' | head -1 | grep -o '[0-9]*$')
for _ in $(seq 150); do ctl pane.read pane="$P" 2>/dev/null | grep -q 'PS ' && break; sleep 0.2; done
# the right pane lists a folder; the left one runs a stand-in agent (a copy of ping.exe named claude.exe), so the
# sidebar shows an agent that is working
ctl pane.split pane="$P" direction=right >/dev/null
sleep 1.5
Q=$(ctl pane.list | grep -o '"id": *[0-9]*' | grep -o '[0-9]*$' | grep -vx "$P" | tail -1)
ctl pane.send_text pane="$Q" text='Get-ChildItem C:\Windows -Directory | Select-Object -First 6 Name, LastWriteTime | Format-Table' enter=true >/dev/null
mkdir -p "$STATE/bin"
cp "$(cygpath -u "$SYSTEMROOT")/System32/PING.EXE" "$STATE/bin/claude.exe"
ctl pane.send_text pane="$P" text="\$env:Path = '$(cygpath -w "$STATE/bin");' + \$env:Path; Clear-Host; claude -n 100 127.0.0.1" enter=true >/dev/null
for _ in $(seq 60); do ctl agent.list 2>/dev/null | grep -q '"agent": *"claude"' && break; sleep 0.25; done
ctl pane.report_state pane="$P" state=working ttl=600 >/dev/null
sleep 4
ctl window.screenshot path="$(cygpath -w "$OUT/nebula-windows.png")" >/dev/null
[ -s "$OUT/nebula-windows.png" ] || fail "no screenshot"
echo "screenshot: $(wc -c < "$OUT/nebula-windows.png") bytes"
