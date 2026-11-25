#!/usr/bin/env bash
# Starts an isolated instance and exercises the automation API.
set -euo pipefail
BIN=$(realpath "${1:-./build/nebula}")
export NEBULA_SOCKET=${XDG_RUNTIME_DIR:-/tmp}/nebula-e2e.sock
export NEBULA_STATE_DIR=$(mktemp -d)
# always headless: never open a window on the user's desktop
unset WAYLAND_DISPLAY DISPLAY
export QT_FORCE_STDERR_LOGGING=1  # Qt logs to journald when stderr is not a tty
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

# ---- editor: open, type (auto-indent, closing brackets), save exactly, undo, unsaved changes guard closing
ED=$(mktemp -d)
printf 'def f(x):\n    return x\n' > "$ED/a.py"
E=$(ctl editor.open file="$ED/a.py" | python3 -c 'import json,sys;print(json.load(sys.stdin)["pane"])')
get() { ctl editor.get pane="$E" text=true | python3 -c "import json,sys;d=json.load(sys.stdin);print($1)"; }
[ "$(get 'd["language"]')" = Python ] || fail "language: $(get 'd')"
ctl pane.send_keys pane="$E" keys='["Ctrl+End"]'
ctl pane.send_text pane="$E" text="def g(y):"
ctl pane.send_keys pane="$E" keys='["Enter"]'
ctl pane.send_text pane="$E" text="return g"
ctl pane.send_keys pane="$E" keys='["(", "y", ")"]'   # typed: ( closes itself, ) steps over it
[ "$(get 'repr(d["text"])')" = "'def f(x):\n    return x\ndef g(y):\n    return g(y)'" ] || fail "typing: $(get 'repr(d["text"])')"
[ "$(get 'd["dirty"]')" = True ] || fail "dirty after typing"
{ ctl pane.close pane="$E" 2>&1 || true; } | grep -q 'unsaved changes' || fail "closing an unsaved editor must refuse"
ctl editor.save pane="$E"
[ "$(cat "$ED/a.py")" = "$(printf 'def f(x):\n    return x\ndef g(y):\n    return g(y)')" ] || fail "saved file: $(cat "$ED/a.py" | od -c | head)"
[ "$(get 'd["dirty"]')" = False ] || fail "dirty after save"
ctl pane.send_keys pane="$E" keys='["Ctrl+Z"]'
[ "$(get 'd["dirty"]')" = True ] && get 'd["text"]' | grep -qv 'return g(y)' || fail "undo"
ctl pane.close pane="$E" force=true
[ "$(ctl editor.list | python3 -c 'import json,sys;print(len(json.load(sys.stdin)))')" = 0 ] || fail "force close"
# a file changed on disk reloads by itself when there are no unsaved changes
E=$(ctl editor.open file="$ED/a.py" | python3 -c 'import json,sys;print(json.load(sys.stdin)["pane"])')
printf 'changed outside\n' > "$ED/a.py"
for _ in $(seq 30); do get 'd["text"]' | grep -q 'changed outside' && break; sleep 0.1; done
get 'd["text"]' | grep -q 'changed outside' || fail "external change not reloaded"
# opening the same file again focuses the editor instead of a second one
[ "$(ctl editor.open file="$ED/a.py" | python3 -c 'import json,sys;print(json.load(sys.stdin)["pane"])')" = "$E" ] || fail "reopen"
{ ctl editor.open file="$ED" 2>&1 || true; } | grep -q 'is a directory' || fail "directory"
printf 'a\0b' > "$ED/bin.dat"
{ ctl editor.open file="$ED/bin.dat" 2>&1 || true; } | grep -q 'binary file' || fail "binary"
ctl pane.close pane="$E"
# files open in tabs of their own; closing the last tab holding only a file leaves a shell, not an empty space
NT=$(ctl space.list | python3 -c 'import json,sys;print(len(json.load(sys.stdin)[0]["tabs"]))')
printf -- '---\ntitle: Doc\n---\n# doc\n' > "$ED/doc.neb"
F=$(ctl file.open file="$ED/a.py" | python3 -c 'import json,sys;print(json.load(sys.stdin)["pane"])')
ctl file.open file="$ED/doc.neb" >/dev/null
[ "$(ctl space.list | python3 -c 'import json,sys;print(len(json.load(sys.stdin)[0]["tabs"]))')" = $((NT + 2)) ] || fail "file.open: one tab per file"
ctl view.list | python3 -c 'import json,sys;assert [v for v in json.load(sys.stdin) if v["title"]=="Doc" and v["where"]=="docked"]' || fail ".neb must open as a docked view, not a modal"
[ "$(ctl file.open file="$ED/a.py" | python3 -c 'import json,sys;print(json.load(sys.stdin)["pane"])')" = "$F" ] || fail "file.open of an open file"
ctl tab.close tab=$((NT + 1)) >/dev/null                       # the Doc view
for _ in $(seq $NT); do ctl tab.close tab=0 >/dev/null; done      # every terminal tab: only a.py is left
[ "$(ctl space.list | python3 -c 'import json,sys;print([t["title"] for t in json.load(sys.stdin)[0]["tabs"]])')" = "['a.py']" ] || fail "setup: $(ctl space.list)"
[ "$(ctl pane.list | python3 -c 'import json,sys;print(len(json.load(sys.stdin)))')" = 0 ] || fail "setup: terminals left"
ctl pane.close pane="$F" >/dev/null                               # what Ctrl+W does on the last file
sleep 0.5
ctl ping >/dev/null || fail "closing the last file closed nebula"
[ "$(ctl pane.list | python3 -c 'import json,sys;print(len(json.load(sys.stdin)))')" = 1 ] || fail "no shell left after closing the last file"
rm -rf "$ED"
echo "editor: ok"
echo "e2e: ok"
