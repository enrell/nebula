#!/usr/bin/env bash
# Windows end to end, for Git Bash: the GUI starts, a pane runs PowerShell on a pseudo console, what is typed comes
# back, the shell's directory and a running program are noticed, secrets and hooks work, and the shell outlives
# the GUI (the pty host keeps it).
#   tests/e2e_windows.sh path/to/nebula.exe
set -euo pipefail
# Git Bash rewrites arguments that look like POSIX paths before it starts a Windows program: not here
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'
BIN=$(realpath "${1:-./build/nebula.exe}")
export NEBULA_SOCKET=nebula-e2e-win
STATE=$(mktemp -d); NEBULA_STATE_DIR=$(cygpath -w "$STATE"); export NEBULA_STATE_DIR
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software NEBULA_SECRETS=file QT_FORCE_STDERR_LOGGING=1
export APPDATA=$(cygpath -w "$STATE/appdata")
mkdir -p "$STATE/appdata/nebula"
echo '{"onboarded":true}' > "$STATE/appdata/nebula/settings.json"
WORK=$(mktemp -d)
LOG=$WORK/gui.log
GUI=
ctl() { "$BIN" ctl "$@"; }
fail() { echo "FAIL: $*" >&2; echo "--- gui log" >&2; tail -30 "$LOG" >&2 || true; exit 1; }
start_gui() { "$BIN" >>"$LOG" 2>&1 & GUI=$!; for _ in $(seq 300); do ctl ping >/dev/null 2>&1 && return; sleep 0.2; done; fail "gui did not start"; }
# nebula.exe processes that are not the pty host: the GUI
kill_gui() {
  powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='nebula.exe'\" | Where-Object { \$_.CommandLine -notmatch '--host' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" || true
  sleep 1
}
kill_all() { powershell -NoProfile -Command "Get-Process nebula -ErrorAction SilentlyContinue | Stop-Process -Force" || true; }
cleanup() { ctl action.run action=kill-session >/dev/null 2>&1 || true; kill_all; rm -rf "$STATE" "$WORK"; }
trap cleanup EXIT
pane_read() { ctl pane.read pane="$1" lines=60 2>/dev/null || true; }
wait_read() { local p=$1 pat=$2 n=${3:-150}; for _ in $(seq "$n"); do pane_read "$p" | grep -q -- "$pat" && return 0; sleep 0.2; done; return 1; }
first_pane() { ctl pane.list | grep -o '"id": *[0-9]*' | head -1 | grep -o '[0-9]*$'; }

# 1. the GUI starts and brings up a pane with a shell
start_gui
P=$(first_pane)
[ -n "$P" ] || fail "no pane"
wait_read "$P" 'PS \|>' 300 || fail "PowerShell prompt not seen: $(pane_read "$P")"
echo "start: ok (pane $P)"

# 2. typing reaches the shell and its output comes back
ctl pane.send_text pane="$P" text='Write-Output ("nebula-ok-" + (6*7))' enter=true >/dev/null
wait_read "$P" 'nebula-ok-42' || fail "output not seen: $(pane_read "$P")"
echo "typing: ok"

# 3. the shell's directory is followed (PowerShell reports it through nebula's prompt hook)
mkdir -p "$WORK/dir with space"
WIN_DIR=$(cygpath -w "$WORK/dir with space")
ctl pane.send_text pane="$P" text="Set-Location -LiteralPath '$WIN_DIR'" enter=true >/dev/null
for _ in $(seq 50); do ctl pane.get pane="$P" | grep -qi 'dir with space' && break; sleep 0.2; done
ctl pane.get pane="$P" | grep -qi 'dir with space' || fail "directory not followed: $(ctl pane.get pane="$P")"
echo "cwd: ok"

# 4. a program the shell runs is recognised as an agent: a copy of ping.exe named claude.exe stands in for one
mkdir -p "$WORK/bin"
cp "$(cygpath -u "$SYSTEMROOT")/System32/PING.EXE" "$WORK/bin/claude.exe"
ctl pane.send_text pane="$P" text="\$env:Path = '$(cygpath -w "$WORK/bin");' + \$env:Path; claude -n 60 127.0.0.1" enter=true >/dev/null
for _ in $(seq 80); do ctl agent.list 2>/dev/null | grep -q '"agent": *"claude"' && break; sleep 0.25; done
ctl agent.list | grep -q '"agent": *"claude"' || fail "agent not detected: $(ctl agent.list | tr -d '\n ' | cut -c1-300)"
ctl pane.send_keys pane="$P" keys='["Ctrl+C"]' >/dev/null
for _ in $(seq 50); do ctl agent.list 2>/dev/null | grep -q '"agent": *"claude"' || break; sleep 0.2; done
ctl agent.list | grep -q '"agent": *"claude"' && fail "agent still reported after it ended"
echo "agent detection: ok"

# 5. state reports from hooks: `ctl --hook` is silent and always succeeds, in or out of a pane
OUT=$(NEBULA_PANE=$P "$BIN" ctl --hook pane.report_state state=working ttl=60 '{"ignored":"json payload"}' 2>&1) || fail "hook failed"
[ -z "$OUT" ] || fail "hook printed: $OUT"
env -u NEBULA_PANE "$BIN" ctl --hook pane.report_state state=working >/dev/null || fail "hook outside a pane failed"
echo "hooks: ok"

# 6. a second pane, closed again
ctl pane.split pane="$P" direction=right >/dev/null
sleep 1
N=$(ctl pane.list | grep -c '"id"')
[ "$N" -ge 2 ] || fail "split did not create a pane ($N)"
echo "split: ok"

# 7. the shell outlives the GUI: kill the GUI, start it again, the pane and its scrollback are still there
ctl pane.send_text pane="$P" text='Write-Output ("mark-" + "persist")' enter=true >/dev/null
wait_read "$P" 'mark-persist' || fail "marker not seen"
sleep 1.5   # the layout is saved a moment after a change
kill_gui
ctl ping >/dev/null 2>&1 && fail "GUI still answers after being killed"
start_gui
wait_read "$P" 'mark-persist' 100 || fail "scrollback not replayed after reattach: $(pane_read "$P")"
ctl pane.send_text pane="$P" text='Write-Output ("alive-" + (7*6))' enter=true >/dev/null
wait_read "$P" 'alive-42' || fail "shell did not survive the GUI"
echo "reattach: ok"

# 8. closing the whole session ends the shells
ctl action.run action=kill-session >/dev/null
sleep 2
ctl ping >/dev/null 2>&1 && fail "still running after kill-session"
echo "e2e windows: ok"
