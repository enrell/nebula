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

# a file view docked next to the focused pane: checked and drawn by the page
R=$(ctl view.show file=report.md where=right)
V=$(echo "$R" | json 'd["view"]')
[ "$(echo "$R" | json 'd["ok"]')" = True ] || fail "report.md should be valid: $R"
[ "$(echo "$R" | json 'd["rendered"]')" = True ] || fail "view not drawn: $R"
[ "$(echo "$R" | json 'd["title"]')" = Report ] || fail "front matter title: $R"
[ "$(ctl space.list | json 'len(d[0]["tabs"][0]["panes"])')" = 1 ] || fail "views must not be listed as terminal panes"
[ "$(ctl view.list | json 'd[0]["where"]')" = docked ] || fail "view.list"
echo "file view: ok"

# the default placement is a modal over the workspace; it hides and comes back, docks and pops out again
R=$(ctl view.show content='```nebula:chart
type: bar
rows: [{k: a, v: 1}, {k: b, v: 3}]
x: k
y: v
```')
M=$(echo "$R" | json 'd["view"]')
[ "$(echo "$R" | json 'd["rendered"]')" = True ] && [ "$(echo "$R" | json 'len(d["renderIssues"])')" = 0 ] || fail "modal chart: $R"
[ "$(ctl view.list | json '[v["where"] for v in d if v["view"] == '"$M"'][0]')" = modal ] || fail "default placement is not modal"
[ "$(ctl view.toggle)" = hidden ] || fail "view.toggle should hide the modal"
[ "$(ctl view.toggle)" = shown ] || fail "view.toggle should show it again"
ctl view.dock view="$M" where=down >/dev/null
[ "$(ctl view.list | json '[v["where"] for v in d if v["view"] == '"$M"'][0]')" = docked ] || fail "dock"
ctl view.dock view="$M" where=modal >/dev/null
[ "$(ctl view.list | json '[v["where"] for v in d if v["view"] == '"$M"'][0]')" = modal ] || fail "pop out"
ctl settings.set key=viewPlacement value=sideways >/dev/null 2>&1 && fail "invalid viewPlacement accepted"
echo "modal: ok"

# html blocks run in a sandbox; their errors come back as render issues
R=$(ctl view.show view="$M" content='```nebula:html
html: <script>throw new Error("boom")</script>
```')
for _ in $(seq 40); do ctl view.get view="$M" | json '" ".join(d["renderIssues"])' | grep -q boom && break; sleep 0.1; done
ctl view.get view="$M" | json '" ".join(d["renderIssues"])' | grep -q "html block: .*boom" || fail "sandbox error not reported: $(ctl view.get view="$M")"
echo "sandbox: ok"

# errors come back with line numbers; the view stays open showing error cards
R=$(ctl view.show where=right content='```nebula:tabel
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

# research record: a data file the page never loads (a bibliography) is watched too; export and snapshot
cat > paper.md <<'MD'
---
title: Paper
bibliography: refs.bib
---
DNA [@watson1953].

```nebula:chart
type: bar
rows: [{k: a, v: 1}, {k: b, v: 3}]
x: k
y: v
```

```nebula:provenance
command: make figures
```
MD
R=$(ctl view.show file=paper.md)
P=$(echo "$R" | json 'd["view"]')
[ "$(echo "$R" | json 'd["ok"]')" = True ] || fail "paper.md: $R"
sed -i 's/watson1953,/watson1953x,/' refs.bib
for _ in $(seq 30); do [ "$(ctl view.get view="$P" | json 'len(d["errors"])')" = 1 ] && break; sleep 0.1; done
ctl view.get view="$P" | json 'd["errors"][0]["message"]' | grep -q 'unknown citation key "watson1953"' || fail "bibliography not watched: $(ctl view.get view="$P")"
sed -i 's/watson1953x,/watson1953,/' refs.bib
for _ in $(seq 30); do [ "$(ctl view.get view="$P" | json 'd["ok"]')" = True ] && break; sleep 0.1; done
ctl view.export view="$P" path=paper.html >/dev/null || fail "html export"
grep -q '<meta name="generator" content="nebula">' paper.html || fail "exported html"
grep -q 'id="ref-1"' paper.html && grep -q 'data:image/png;base64' paper.html || fail "exported html lacks references or the frozen chart"
grep -q 'nebula-view:' paper.html && fail "exported html still points into nebula"
ctl view.export view="$P" format=pdf path=paper.pdf >/dev/null || fail "pdf export"
head -c 5 paper.pdf | grep -q '%PDF-' || fail "not a pdf"
ctl view.snapshot view="$P" path=shot.png >/dev/null || fail "snapshot"
python3 -c "import sys; d=open('shot.png','rb').read(24); sys.exit(0 if d[:8]==b'\x89PNG\r\n\x1a\n' and int.from_bytes(d[16:20],'big')>100 else 1)" || fail "snapshot is not a PNG"
OUT=$(ctl view.snapshot view="$P" block=9 2>&1) && fail "snapshot of a missing block succeeded"
echo "$OUT" | grep -q "no block 9" || fail "snapshot of a missing block: $OUT"
# agents get the snapshot as an MCP image
OUT=$(printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
  "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"view_snapshot\",\"arguments\":{\"view\":$P,\"block\":1}}}" | "$BIN" mcp)
echo "$OUT" | python3 -c '
import base64, json, sys
r = [json.loads(l) for l in sys.stdin if l.strip()][-1]["result"]
img = r["content"][0]
assert not r.get("isError") and img["type"] == "image" and img["mimeType"] == "image/png", r
assert base64.b64decode(img["data"])[:8] == b"\x89PNG\r\n\x1a\n"
assert "block 1" in r["content"][1]["text"]' || fail "view_snapshot over MCP: ${OUT:0:300}"
ctl view.close view="$P" >/dev/null
echo "research record, export and snapshot: ok"

# component catalogue
ctl view.components | json '{"table", "chart", "html", "math", "structure"} <= {c["name"] for c in d}' | grep -qx True || fail "components"
ctl view.components name=nebula:table | json 'd["example"]' | grep -q '^```nebula:table' || fail "describe"

# persistence: docked views come back after the GUI restarts; modal ones (and their stored content) do not
sleep 1.2
kill -9 $GUI; wait $GUI 2>/dev/null || true; rm -f "$NEBULA_SOCKET"
start_gui
[ "$(ctl view.list | json '" ".join(str(v["view"]) for v in d)')" = "$V $E" ] || fail "views not restored: $(ctl view.list)"
ls "$NEBULA_STATE_DIR/views" | grep -q "^$M\." && fail "inline content of the modal view left behind"
[ "$(ctl view.get view="$E" | json 'd["title"]')" = Fixed ] || fail "restored view lost its title"
echo "persistence: ok"

ctl view.close view="$E" >/dev/null
[ "$(ctl view.list | json 'len(d)')" = 1 ] || fail "view.close"
ls "$NEBULA_STATE_DIR/views" | grep -q "^$E\." && fail "inline content of a closed view left behind"
echo "close: ok"
if grep -E "TypeError|ReferenceError|is not defined|qt\.qml\.|view [0-9]+:" "$LOG"; then fail "warnings in the GUI log"; fi
echo "views e2e: ok"
