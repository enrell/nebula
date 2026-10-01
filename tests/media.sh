#!/usr/bin/env bash
# Marketing media for the README and PRs: GIFs recorded from the screen and stills of views with real WebGL.
# Unlike tests/screenshots.sh (headless, no GL) this runs nebula on a virtual X server with Mesa's software GL,
# in a throw-away HOME with scripted stand-in agents (tests/media_standin.py: no accounts, no personal data).
# GIFs are real screen recordings (ffmpeg x11grab) driven by real mouse and keyboard input (xdotool).
#   tests/media.sh [path/to/nebula]         needs: Xvfb, Mesa (libgl1-mesa-dri), ffmpeg, xdotool, python3
# operator.gif shows REAL Claude Code as the operator (sonnet: it must get the tool calls right) and as the agents it launches (haiku) when `claude` is installed and
# logged in (MEDIA_AGENTS=real, the default then); MEDIA_AGENTS=standin uses tests/media_standin.py (no account needed).
# The login is copied into the throw-away HOME for the run and removed with it. MEDIA_ONLY=operator records just that scene.
# Writes docs/media/: hero.gif, operator.gif, views-live.gif, persist.gif (README), and views-modal.png,
# views-docked.png, views-errors.png, views-chembio.png, views-physics.png, views-research.png (docs/views.md).
set -euo pipefail
BIN=$(realpath "${1:-./build/nebula}")
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(realpath -m "$HERE/../docs/media")
for t in Xvfb ffmpeg xdotool python3; do command -v $t >/dev/null || { echo "media: $t is required" >&2; exit 2; }; done
mkdir -p "$OUT"
REAL_HOME=$HOME BASE_PATH=$PATH
export HOME=$(mktemp -d)
WORK=$HOME/work FRAMES=$HOME/frames
mkdir -p "$WORK" "$FRAMES" "$HOME/.config/nebula" "$HOME/agentbin"
# no user@host on screen: the distro's bashrc puts it in the prompt and the terminal title
printf '%s\n' "PS1='\$ '" 'PROMPT_COMMAND='"'"'printf "\033]0;%s\007" "${PWD/#$HOME/\~}"'"'" > "$HOME/.bashrc"
DISP=:$((90 + RANDOM % 9))
Xvfb "$DISP" -screen 0 1600x2200x24 >/dev/null 2>&1 & XVFB=$!
export DISPLAY=$DISP QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 QT_FORCE_STDERR_LOGGING=1 NEBULA_SECRETS=file
# Mesa's software GL is blocklisted for WebGL by Chromium; fine for recording (real GPUs are not)
export QTWEBENGINE_CHROMIUM_FLAGS=--ignore-gpu-blocklist
unset WAYLAND_DISPLAY QT_QUICK_BACKEND
export NEBULA_SOCKET=${XDG_RUNTIME_DIR:-/tmp}/nebula-media.sock NEBULA_STATE_DIR=$HOME/state
GUI=
ctl() { "$BIN" ctl "$@"; }
kill_host() { for pid in $(pgrep -f "$BIN --host" 2>/dev/null); do tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -qx "NEBULA_SOCKET=$NEBULA_SOCKET" && kill "$pid" 2>/dev/null || true; done; }
stop() { ctl action.run action=kill-session >/dev/null 2>&1 || true; kill $GUI 2>/dev/null || true; wait $GUI 2>/dev/null || true; kill_host; rm -f "$NEBULA_SOCKET" "$NEBULA_SOCKET.host"; }
trap 'set +e; kill $REC 2>/dev/null; stop; kill $XVFB 2>/dev/null; [ -z "${MEDIA_KEEP:-}" ] || cp "$HOME"/*.mkv "$HOME/gui.log" "$MEDIA_KEEP"/ 2>/dev/null || true; rm -rf "$HOME"' EXIT   # MEDIA_KEEP=dir keeps the raw recordings
REC=
kill_host; rm -f "$NEBULA_SOCKET" "$NEBULA_SOCKET.host"   # a host left over from an interrupted run would serve this socket
start() {
  "$BIN" >"$HOME/gui.log" 2>&1 & GUI=$!
  for _ in $(seq 80); do ctl ping >/dev/null 2>&1 && break; sleep 0.1; done
  ctl window.resize width=1400 height=860 >/dev/null; sleep 0.8
}
paneid() { ctl pane.list | python3 -c 'import json,sys;print(json.load(sys.stdin)[0]["id"])'; }
# record NAME [mouse]: capture the window's area of the screen (the pointer only with `mouse`) until
# finish NAME [WIDTH] [CAPTION_FILTER] turns it into a GIF
record() { ffmpeg -loglevel error -y -f x11grab -draw_mouse $([ "${2:-}" = mouse ] && echo 1 || echo 0) -framerate 20 -video_size 1400x860 -i "$DISP+0,0" -c:v ffv1 "$HOME/$1.mkv" & REC=$!; sleep 0.3; }
finish() {
  kill -INT $REC; wait $REC 2>/dev/null || true
  ffmpeg -loglevel error -y -i "$HOME/$1.mkv" \
    -vf "${3:+$3,}fps=14,scale=${2:-1000}:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=200:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle" \
    "$OUT/$1.gif"
}
# a caption over the frames from $2 to $3 seconds (the screen is black there: nebula is gone)
caption() { echo "drawtext=fontfile=$FONT:text='$1':fontcolor=white:fontsize=34:x=(w-text_w)/2:y=(h-text_h)/2:enable='between(t,$2,$3)'"; }
FONT=$(fc-match -f '%{file}' 'DejaVu Sans Mono:bold')
# real input: focus the window with a click, then type or drag like a person
click() { xdotool mousemove "$1" "$2" click 1; sleep 0.2; }
drag() {   # drag x0 y0 x1 y1 [steps]: press, move in steps, release
  local n=${5:-30}; xdotool mousemove "$1" "$2" mousedown 1
  for i in $(seq 1 "$n"); do xdotool mousemove $(( $1 + ($3 - $1) * i / n )) $(( $2 + ($4 - $2) * i / n )); sleep 0.04; done
  xdotool mouseup 1
}

# --- a small "experiment" project the agent reports on
cd "$WORK"
git init -q . && git -c user.email=a@b -c user.name=demo commit -q --allow-empty -m init
python3 - <<'PY'
import math
with open("bench.csv", "w") as f:
    f.write("run,before_ms,after_ms\n")
    for i in range(1, 17):
        f.write(f"r{i},{62 + 7 * math.sin(i * .8) + i * .6:.1f},{34 + 3 * math.cos(i * 1.3):.1f}\n")
with open("grid.csv", "w") as f:
    f.write("lr,batch,loss\n")
    for a in range(14):
        for b in range(14):
            f.write(f"{0.001 * (a + 1):.3f},{16 * (b + 1)},{0.4 + 0.9 * ((a - 6) / 6.5) ** 2 + 0.6 * ((b - 4.5) / 8) ** 2 + 0.08 * math.sin(a) * math.cos(b):.4f}\n")
PY
cat > report.md <<'MD'
---
title: Optimizer sweep · run 42
---
```nebula:stats
- {label: Best loss, value: 0.412, delta: -0.08, tone: good}
- {label: p95 latency, value: 34ms, delta: -41%, tone: good}
- {label: Runs, value: 196}
- {label: GPU hours, value: 118, delta: +12, tone: bad}
```

```nebula:chart3d
type: surface
title: Loss surface · learning rate × batch size (WebGL)
data: grid.csv
x: lr
y: batch
z: loss
rotate: true
height: 400
```

```nebula:chart
type: line
title: Latency per run
data: bench.csv
x: run
y: [before_ms, after_ms]
unit: ms
height: 240
```
MD

# --- the stand-in agent: prints like a coding agent, then shows its result with a view
cat > "$HOME/agentbin/claude" <<'SH'
#!/bin/sh
say() { printf '%s\n' "$1"; sleep "${2:-0.35}"; }
printf '\033[1;35m✻\033[0m \033[1mclaude\033[0m  \033[2m· Opus · ~/work\033[0m\n\n'
say "> sweep the optimizer settings and show me the results" 0.6
say "" 0.1
say "● Running 196 training runs (14 learning rates × 14 batch sizes)" 0.6
say "  ⎿ grid.csv  196 rows" 0.4
say "● Benchmarking the new data loader" 0.5
say "  ⎿ bench.csv  16 runs, p95 34ms (was 58ms)" 0.5
say "● view_show(file: \"report.md\")" 0.3
nebula ctl view.show file=report.md >/dev/null
say "  ⎿ view 3 \"Optimizer sweep · run 42\": 3 blocks, 0 errors, drawn" 0.1
sleep 600
SH
chmod +x "$HOME/agentbin/claude"
ln -s "$BIN" "$HOME/agentbin/nebula"
export PATH="$HOME/agentbin:$PATH"
echo '{"onboarded":true}' > "$HOME/.config/nebula/settings.json"

if [ "${MEDIA_ONLY:-}" != operator ]; then
# --- hero.gif: the agent works, then its view opens as a modal and the loss surface turns
start
# pay the web engine's first start before recording (a real session has usually shown a view already)
W=$(ctl view.show content='warm-up' | python3 -c 'import json,sys;print(json.load(sys.stdin)["view"])')
ctl view.close view="$W" >/dev/null
P=$(paneid)
ctl pane.send_text pane="$P" text="cd ~/work; PS1='\$ '; clear; claude" enter=true >/dev/null
record hero
sleep 9
ctl window.screenshot path="$OUT/views-modal.png" >/dev/null
sleep 3
finish hero

# --- views-docked.png: the same view docked next to the agent
V=$(ctl view.list | python3 -c 'import json,sys;print(json.load(sys.stdin)[0]["view"])')
ctl view.dock view="$V" where=right >/dev/null; sleep 4
ctl window.screenshot path="$OUT/views-docked.png" >/dev/null
stop

# --- views-errors.png: what the checker tells the agent, and what the user sees meanwhile
rm -rf "$NEBULA_STATE_DIR"
cat > broken.md <<'MD'
```nebula:stats
- {label: Throughput, value: 12k/s, tone: great}
```

```nebula:tabel
data: bench.csv
```

```nebula:chart
type: line
data: bench.csv
x: run
y: [before_ms, afterms]
```
MD
start
P=$(paneid)
ctl pane.send_text pane="$P" text="cd ~/work; PS1='\$ '" enter=true >/dev/null
sleep 0.3
printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05"}}' \
  "$(python3 -c 'import json;print(json.dumps({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"view_show","arguments":{"file":"broken.md"}}}))')" \
  | NEBULA_PANE=$P "$BIN" mcp | tail -1 | python3 -c 'import json,sys;print(json.loads(sys.stdin.read())["result"]["content"][0]["text"])' > "$HOME/report.txt"
ctl view.dock view="$(ctl view.list | python3 -c 'import json,sys;print(json.load(sys.stdin)[0]["view"])')" where=right >/dev/null
ctl pane.send_text pane="$P" text="clear; printf '\\033[1m● view_show(file: \"broken.md\")\\033[0m\\n'; sed 's/^/  /' ~/report.txt" enter=true >/dev/null
sleep 3
ctl window.screenshot path="$OUT/views-errors.png" >/dev/null
stop

# --- the science gallery: whole documents, captured with view.snapshot (the page only, no window chrome)
rm -rf "$NEBULA_STATE_DIR"
mkdir -p "$WORK/lab" && cp -r "$HERE/../renderer/test/fixtures/proj/." "$WORK/lab/" && cd "$WORK/lab"
python3 -c "p='refs.bib'; s=open(p).read(); open(p,'w').write(s[:s.index('@book{broken')])"   # the fixture's deliberate error
cat > chembio.md <<'MD'
---
title: Chemistry and biology
---
```nebula:molecule
title: Stimulants and analgesics
items:
  - {smiles: "CC(=O)Oc1ccccc1C(=O)O", name: aspirin}
  - {smiles: "CN1C=NC2=C1C(=O)N(C(=O)N2C)C", name: caffeine}
  - {smiles: "CC(C)Cc1ccc(cc1)C(C)C(=O)O", name: ibuprofen}
size: 200
```

```nebula:structure
title: HIV-1 capsid C-terminal domain (PDB 1A8O)
data: 1a8o.pdb
highlight:
  - {resn: MSE, style: stick, color: orange, label: Se-Met}
height: 380
```

```nebula:sequence
title: β-globin alignment
data: globins.fa
annotations:
  - {start: 1, end: 8, label: N-terminus, color: blue}
  - {start: 63, end: 67, label: heme pocket, color: red}
```

```nebula:tracks
title: CFTR locus (synthetic features)
region: "chr7:117,440,000-117,670,000"
tracks:
  - {type: features, label: genes, data: genes.bed}
  - {type: signal, label: coverage, data: coverage.bedgraph, color: blue}
  - {type: variants, label: variants, variants: [{pos: 117559590, ref: CTT, alt: C, label: F508del, impact: high}, {pos: 117610000, label: G551D, impact: moderate}]}
```
MD
cat > physics.md <<'MD'
---
title: Physics, networks and maps
---
```nebula:field
title: Damped pendulum — phase portrait
u: y
v: "-sin(x) - b y"
x: [-7, 7]
y: [-4, 4]
params:
  b: {value: 0.25, min: 0, max: 1.5, label: damping b}
xlabel: θ
ylabel: ω
height: 360
```

```nebula:volume
title: 2p orbital (Gaussian cube)
data: orbital.cube
height: 320
```

```mermaid
flowchart LR
  raw[(raw images)] --> seg[segment nuclei] --> qc{QC ok?}
  qc -- yes --> stats[per-cell stats] --> view[nebula view]
  qc -- no --> seg
```

```nebula:map
title: Regional warming (synthetic)
data: regions.geojson
color: warming
height: 340
```

```nebula:image
src: img/cells.png
compare: img/cells-segmented.png
labels: [raw, segmented]
scale: "0.65 µm"
zoom: true
```
MD
cat > research.md <<'MD'
---
title: Structure and spectra — lab notes
bibliography: refs.bib
---
# Structure and spectra

The double helix [@watson1953] fixed the geometry; the energy levels follow from the wave equation
[see @schrodinger1926, eq. 4], $\hat H \psi = E \psi$. The detector responds linearly (fit below), and the
strain data follow the template of [@ligo2016].

```nebula:chart
title: Detector calibration
type: scatter
data: calibration.csv
x: concentration
y: absorbance
error: sd
fit: linear
height: 260
```

```nebula:provenance
command: python src/sample.py --calibrate --seed 7
script: src/sample.py
environment: {python: "3.12.4", numpy: "2.1.0", scipy: "1.14.1"}
seed: 7
```

```nebula:references
```
MD
git init -q . && git add -A && git -c user.email=a@b -c user.name=demo commit -qm "lab notes"
start
# window heights fit each document, so the stills end where the content does
for shot in chembio:1760 physics:1960 research:1160; do
  doc=${shot%%:*}
  ctl window.resize width=1400 height=${shot#*:} >/dev/null; sleep 0.8
  V=$(ctl view.show file=$doc.md | python3 -c 'import json,sys;d=json.load(sys.stdin);assert d["ok"],d;print(d["view"])')
  sleep 6
  ctl view.snapshot view="$V" path="$OUT/views-$doc.png" >/dev/null
  ctl view.close view="$V" >/dev/null
done

# --- views-live.gif: a reader drags a slider (the field is re-sampled live) and turns a protein with the mouse
stop
rm -rf "$NEBULA_STATE_DIR"
cat > live.md <<'MD'
```nebula:field
title: Damped pendulum — drag the damping
u: y
v: "-sin(x) - b y"
x: [-7, 7]
y: [-4, 4]
params:
  b: {value: 0.1, min: 0, max: 1.5, label: damping b}
xlabel: θ
ylabel: ω
height: 330
```

```nebula:structure
title: HIV-1 capsid domain (PDB 1A8O) — drag to rotate
data: 1a8o.pdb
height: 300
```
MD
start
W=$(ctl view.show content='warm-up' | python3 -c 'import json,sys;print(json.load(sys.stdin)["view"])'); ctl view.close view="$W" >/dev/null
ctl view.show file=live.md >/dev/null; sleep 5
# positions in the 1400×860 window: the slider's thumb and track end, the middle of the protein
record views-live mouse
sleep 1
drag 445 121 575 121 60; sleep 1.2
drag 575 121 470 121 40; sleep 1
drag 810 670 1010 640 45; sleep 0.4
drag 1010 640 760 700 45; sleep 1.2
finish views-live
stop

fi   # MEDIA_ONLY
SCENE_PATH=$PATH
# --- operator.gif: ask the operator in plain words; it launches two agents in their own worktrees, one of them
# stops at a permission prompt, Ctrl+Shift+A jumps to it, and both finish
CRED=${CLAUDE_CREDENTIALS:-$REAL_HOME/.claude/.credentials.json}
if [ -z "${MEDIA_AGENTS:-}" ]; then
  if command -v claude >/dev/null && { [ -f "$CRED" ] || [ -n "${ANTHROPIC_API_KEY:-}" ]; }; then MEDIA_AGENTS=real; else MEDIA_AGENTS=standin; fi
fi
echo "media: operator scene with $MEDIA_AGENTS agents"

# poll the real state instead of sleeping: waitfor 'python expression over the agent list a' SECONDS (fails on timeout);
# WAIT_ON=pane.list polls all panes instead
waitfor() {
  local end=$((SECONDS + $2))
  while [ $SECONDS -lt $end ]; do
    ctl "${WAIT_ON:-agent.list}" | python3 -c 'import json,sys;a=json.load(sys.stdin);sys.exit(0 if ('"$1"') else 1)' 2>/dev/null && return 0
    sleep 0.5
  done
  echo "media: timed out waiting for: $1" >&2; ctl pane.list >&2; return 1
}
# milestones, in seconds since the recording started, to speed up the dull waits afterwards
mark() { MARKS+=("$(python3 -c "import time;print(round(time.time() - $REC_T0, 2))")"); }
record_t() { REC_T0=$(python3 -c 'import time;print(time.time())'); MARKS=(); record "$@"; }
# finish_cut NAME SPEED...: like finish, but plays the stretch before each mark (and the tail) at the given speed
finish_cut() {
  local name=$1; shift
  local n=$# i=0 a=0 fc="" cat="" sp
  kill -INT $REC; wait $REC 2>/dev/null || true
  for sp in "$@"; do
    local b=${MARKS[$i]:-99999}
    fc+="[0:v]trim=start=$a:end=$b,setpts=(PTS-STARTPTS)/$sp[v$i];"; cat+="[v$i]"; a=$b; i=$((i + 1))
  done
  ffmpeg -loglevel error -y -i "$HOME/$name.mkv" -filter_complex "${fc}${cat}concat=n=$n:v=1[c];[c]fps=14,scale=1000:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=200:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle" "$OUT/$name.gif"
}

rm -rf "$NEBULA_STATE_DIR"
DEMO=$HOME/demo
mkdir -p "$DEMO/tests"
cd "$DEMO"
# a small project with a genuinely flaky test: a 50 ms token and a 45 ms sleep pass or fail with the machine's load
cat > session.py <<'PY'
import time


class Session:
    def __init__(self, ttl):
        self.expires_at = time.time() + ttl

    def is_valid(self):
        return time.time() < self.expires_at
PY
cat > tests/test_session.py <<'PY'
import time
import unittest

from session import Session


class SessionTest(unittest.TestCase):
    def test_valid_before_expiry(self):
        s = Session(ttl=0.05)
        time.sleep(0.045)
        self.assertTrue(s.is_valid())

    def test_invalid_after_expiry(self):
        s = Session(ttl=0.05)
        time.sleep(0.06)
        self.assertFalse(s.is_valid())
PY
printf '# demo\n\nA tiny session library.\n' > README.md
git init -q -b main . && git add -A && git -c user.email=demo@example.com -c user.name=demo commit -qm "session library"

if [ "$MEDIA_AGENTS" = real ]; then
  mkdir -p "$HOME/.claude" "$HOME/realbin"
  [ -f "$CRED" ] && install -m 600 "$CRED" "$HOME/.claude/.credentials.json"
  ln -sf "$(PATH=$BASE_PATH command -v claude)" "$HOME/realbin/claude"; ln -sf "$BIN" "$HOME/realbin/nebula"
  for v in $(env | grep -o '^CLAUDE[A-Z_]*'); do unset "$v"; done   # when run from inside a Claude Code session
  export PATH="$HOME/realbin:$BASE_PATH" CLAUDE_CODE_HIDE_ACCOUNT_INFO=1 DISABLE_AUTOUPDATER=1 DISABLE_TELEMETRY=1
  # Claude Code asks to trust every new folder, parents do not count: trust the worktrees the prompt names, and accept
  # the dialog if the operator picks other names anyway
  python3 - "$HOME" "$DEMO" <<'PY'
import json, sys
home, demo = sys.argv[1:]
dirs = [demo, demo + "-fix-flaky", demo + "-docs-expiry"]
json.dump({"hasCompletedOnboarding": True, "theme": "dark", "projects": {d: {"hasTrustDialogAccepted": True} for d in dirs}},
          open(home + "/.claude.json", "w"))
PY
  # haiku: fast and cheap. Everything is allowed except committing: that is the one prompt the demo stops at
  cat > "$HOME/.claude/settings.json" <<'JSON'
{"model":"haiku","permissions":{"allow":["Bash","Read","Edit","Write","Glob","Grep"],"ask":["Bash(git commit:*)"]}}
JSON
  echo '{"onboarded":true,"operatorAgent":"native:claude","operatorModel":"sonnet"}' > "$HOME/.config/nebula/settings.json"
  PROMPT="Launch two claude agents side by side in worktrees: fix-flaky fixes the flaky test and commits, docs-expiry documents Session expiry in the README. Then close this shell."
else
  mkdir -p "$HOME/standin"
  for a in claude codex; do printf '#!/bin/sh\nexec python3 "%s/media_standin.py" %s "$@"\n' "$HERE" "$a" > "$HOME/standin/$a"; chmod +x "$HOME/standin/$a"; done
  ln -sf "$BIN" "$HOME/standin/nebula"
  export PATH="$HOME/standin:$PATH"
  echo '{"onboarded":true,"operatorAgent":"native:claude"}' > "$HOME/.config/nebula/settings.json"
  PROMPT="Launch claude and codex on the flaky auth test, each in its own worktree"
fi

[ -n "${MEDIA_KEEP:-}" ] && export NEBULA_ACP_TRACE=1   # the operator's wire log, in gui.log
start
if [ "$MEDIA_AGENTS" = real ]; then
  ctl integration.install agent=claude kind=hooks >/dev/null
  ( while sleep 1; do
      for id in $(ctl agent.list 2>/dev/null | python3 -c 'import json,sys;[print(p["id"]) for p in json.load(sys.stdin)]' 2>/dev/null); do
        ctl pane.read pane="$id" lines=30 2>/dev/null | grep -q "Yes, I trust this folder" && { echo "media: accepting the trust dialog in pane $id" >&2; ctl pane.send_keys pane="$id" keys=Down >/dev/null; ctl pane.send_keys pane="$id" keys=Enter >/dev/null; }
      done
    done ) & TRUST=$!
fi
P=$(paneid)
ctl pane.send_text pane="$P" text="cd $DEMO; PS1='\$ '; clear; git log --oneline -1" enter=true >/dev/null
sleep 0.5
record_t operator
click 800 400
xdotool key ctrl+shift+i; sleep 1.2
xdotool type --delay 28 "$PROMPT"
sleep 0.4; xdotool key Return; mark                                           # 1: prompt sent
if [ "$MEDIA_AGENTS" = real ]; then
  SPEEDS=(1)                                                                  # typing, at 1x
  waitfor 'len(a)>=2' 120; mark; SPEEDS+=(3)                                  # the operator launches both agents
  WAIT_ON=pane.list waitfor 'len(a)==2' 30 || true; sleep 2; mark; SPEEDS+=(1)   # ... closes the shell and says so
  xdotool key Escape
  waitfor 'any(x["state"]=="blocked" for x in a)' 240; sleep 1; mark; SPEEDS+=(5)   # they work until one asks
  # jump to whichever agent is blocked and approve it, as long as one is (each may ask more than once)
  for _ in 1 2 3 4 5; do
    xdotool key ctrl+shift+a; sleep 1.6
    xdotool key Return; sleep 1.5; mark; SPEEDS+=(1)
    sleep 2
    waitfor 'any(x["state"]=="blocked" for x in a) or all(x["state"] in ("done","idle") for x in a)' 240
    ctl agent.list | python3 -c 'import json,sys;sys.exit(0 if any(x["state"]=="blocked" for x in json.load(sys.stdin)) else 1)' || break
    sleep 1; mark; SPEEDS+=(5)
  done
  sleep 2.5; SPEEDS+=(3)                                                      # both finished, a short hold
  kill $TRUST 2>/dev/null
  finish_cut operator "${SPEEDS[@]}"
else
  sleep 7.5
  xdotool key Escape; sleep 3.5
  xdotool key ctrl+shift+a; sleep 1.5
  xdotool type --delay 120 "y"; xdotool key Return
  sleep 5
  finish operator
fi
stop

export PATH=$SCENE_PATH
cd "$WORK"
if [ "${MEDIA_ONLY:-}" != operator ]; then
# --- persist.gif: nebula is killed while agents work; the next launch finds everything still running
rm -rf "$NEBULA_STATE_DIR"
start
P=$(paneid)
ctl pane.send_text pane="$P" text="cd ~/work; PS1='\$ '; clear; for i in \$(seq 1 400); do printf '\\rtraining  epoch %3d/400' \$i; sleep 0.1; done" enter=true >/dev/null
ctl agent.launch agent=claude prompt="fix the flaky auth test" where=split-right >/dev/null
sleep 2.5
record persist
sleep 2.5
kill -9 $GUI; wait $GUI 2>/dev/null || true
sleep 2.2
start
sleep 5
finish persist 1000 "$(caption 'kill -9 nebula' 2.7 4.9)"
stop

fi   # MEDIA_ONLY

echo "media written to $OUT: $(cd "$OUT" && ls | tr '\n' ' ')"
