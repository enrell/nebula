#!/usr/bin/env bash
# Views end to end: view.show from a file and inline, the checker report, drawing in the web engine, updates by
# id, live reload of a watched file, the file sandbox of the page, persistence across a GUI restart, close.
#   tests/e2e_views.sh path/to/nebula
set -euo pipefail
BIN=$(realpath "${1:-./build/nebula}")
HERE=$(cd "$(dirname "$0")" && pwd)
export NEBULA_SOCKET=${XDG_RUNTIME_DIR:-/tmp}/nebula-e2e-v.sock
export NEBULA_STATE_DIR=$(mktemp -d)
unset WAYLAND_DISPLAY DISPLAY
export QT_FORCE_STDERR_LOGGING=1
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QPA_PLATFORMTHEME= GDK_BACKEND=
WORK=$(mktemp -d) GUI= LOG=$WORK/gui.log
ctl() { "$BIN" ctl "$@"; }
fail() { echo "FAIL: $*" >&2; tail -20 "$LOG" >&2; exit 1; }
json() { python3 -c "import json,sys; d=json.load(sys.stdin); print($1)"; }
start_gui() { "$BIN" >>"$LOG" 2>&1 & GUI=$!; for _ in $(seq 80); do ctl ping >/dev/null 2>&1 && return; sleep 0.1; done; fail "gui did not start"; }
kill_host() { for pid in $(pgrep -f "$BIN --host" 2>/dev/null); do tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -qx "NEBULA_SOCKET=$NEBULA_SOCKET" && kill "$pid" 2>/dev/null || true; done; }
cleanup() { ctl action.run action=kill-session >/dev/null 2>&1 || true; kill $GUI 2>/dev/null || true; kill_host; rm -rf "$NEBULA_STATE_DIR" "$WORK" "$NEBULA_SOCKET" "$NEBULA_SOCKET.host"; }
trap cleanup EXIT

cp -r "$HERE/../renderer/test/fixtures/proj" "$WORK/proj"
cd "$WORK/proj"
cat > report.md <<'MD'
---
title: Report
---
```nebula:stats
- {label: Tests, value: 42, delta: +3, tone: good}
```

```nebula:table
data: results.csv
```

![pixel](img/pixel.png)
MD

start_gui
ctl window.resize width=1200 height=800 >/dev/null

# a file view: checked, placed next to the focused pane, drawn by the page
R=$(ctl view.show file=report.md)
V=$(echo "$R" | json 'd["view"]')
[ "$(echo "$R" | json 'd["ok"]')" = True ] || fail "report.md should be valid: $R"
[ "$(echo "$R" | json 'd["rendered"]')" = True ] || fail "view not drawn: $R"
[ "$(echo "$R" | json 'd["title"]')" = Report ] || fail "front matter title: $R"
[ "$(ctl space.list | json 'len(d[0]["tabs"][0]["panes"])')" = 1 ] || fail "views must not be listed as terminal panes"
[ "$(ctl view.list | json 'd[0]["view"]')" = "$V" ] || fail "view.list"
echo "file view: ok"

# errors come back with line numbers; the view stays open showing error cards
R=$(ctl view.show content='```nebula:tabel
rows: []
```')
E=$(echo "$R" | json 'd["view"]')
[ "$(echo "$R" | json 'd["ok"]')" = False ] || fail "unknown component should fail: $R"
echo "$R" | json 'd["errors"][0]["hint"]' | grep -q 'did you mean "nebula:table"' || fail "no suggestion: $R"
# fixing it in place: same view id, title kept
R=$(ctl view.show view="$E" content='```nebula:table
rows: [{a: 1}]
```' title=Fixed)
[ "$(echo "$R" | json 'd["view"]')" = "$E" ] && [ "$(echo "$R" | json 'd["ok"]')" = True ] || fail "update by id: $R"
R=$(ctl view.show view="$E" content='# again')
[ "$(echo "$R" | json 'd["title"]')" = Fixed ] || fail "title lost on update: $R"
echo "errors and updates: ok"

# a watched file reloads on change
printf '```nebula:stats\n- {label: A, value: 1, tone: nope}\n```\n' > report.md
for _ in $(seq 30); do [ "$(ctl view.get view="$V" | json 'len(d["errors"])')" = 1 ] && break; sleep 0.1; done
[ "$(ctl view.get view="$V" | json 'd["errors"][0]["line"]')" = 2 ] || fail "watched file not re-checked: $(ctl view.get view="$V")"
echo "live reload: ok"

# component catalogue
[ "$(ctl view.components | json '",".join(c["name"] for c in d)')" = "callout,stats,table,checklist,code,image" ] || fail "components"
ctl view.components name=nebula:table | json 'd["example"]' | grep -q '^```nebula:table' || fail "describe"

# persistence: views come back after the GUI restarts
sleep 1.2
kill -9 $GUI; wait $GUI 2>/dev/null || true; rm -f "$NEBULA_SOCKET"
start_gui
[ "$(ctl view.list | json '" ".join(str(v["view"]) for v in d)')" = "$V $E" ] || fail "views not restored: $(ctl view.list)"
[ "$(ctl view.get view="$E" | json 'd["title"]')" = Fixed ] || fail "restored view lost its title"
echo "persistence: ok"

ctl view.close view="$E" >/dev/null
[ "$(ctl view.list | json 'len(d)')" = 1 ] || fail "view.close"
ls "$NEBULA_STATE_DIR/views" | grep -q "^$E\." && fail "inline content of a closed view left behind"
echo "close: ok"
if grep -E "TypeError|ReferenceError|is not defined|qt\.qml\.|view [0-9]+:" "$LOG"; then fail "warnings in the GUI log"; fi
echo "views e2e: ok"
