#!/usr/bin/env bash
# Marketing media for the README and PRs: an animated GIF and screenshots of views with real WebGL.
# Unlike tests/screenshots.sh (headless, no GL) this runs nebula on a virtual X server with Mesa's software GL,
# in a throw-away HOME with a scripted stand-in agent (no accounts, no personal data).
#   tests/media.sh [path/to/nebula]         needs: Xvfb, Mesa (libgl1-mesa-dri), ffmpeg, python3
# Writes docs/media/{hero.gif, views-modal.png, views-docked.png, views-errors.png} and the science gallery
# {views-chembio.png, views-physics.png, views-research.png, views-science.gif}.
set -euo pipefail
BIN=$(realpath "${1:-./build/nebula}")
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(realpath -m "$HERE/../docs/media")
for t in Xvfb ffmpeg python3; do command -v $t >/dev/null || { echo "media: $t is required" >&2; exit 2; }; done
mkdir -p "$OUT"
export HOME=$(mktemp -d)
WORK=$HOME/work FRAMES=$HOME/frames
mkdir -p "$WORK" "$FRAMES" "$HOME/.config/nebula" "$HOME/agentbin"
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
trap 'stop; kill $XVFB 2>/dev/null; rm -rf "$HOME"' EXIT
start() {
  "$BIN" >"$HOME/gui.log" 2>&1 & GUI=$!
  for _ in $(seq 80); do ctl ping >/dev/null 2>&1 && break; sleep 0.1; done
  ctl window.resize width=1400 height=860 >/dev/null; sleep 0.8
}
paneid() { ctl pane.list | python3 -c 'import json,sys;print(json.load(sys.stdin)[0]["id"])'; }

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

# --- hero.gif: the agent works, then its view opens as a modal and the loss surface turns
start
# pay the web engine's first start before recording (a real session has usually shown a view already)
W=$(ctl view.show content='warm-up' | python3 -c 'import json,sys;print(json.load(sys.stdin)["view"])')
ctl view.close view="$W" >/dev/null
P=$(paneid)
ctl pane.send_text pane="$P" text="cd ~/work; PS1='\$ '; clear; claude" enter=true >/dev/null
n=0
shot() { ctl window.screenshot path="$FRAMES/$(printf %04d $n).png" >/dev/null; n=$((n + 1)); }
t0=$(date +%s.%N)
for _ in $(seq 110); do shot; sleep 0.02; done
t1=$(date +%s.%N)
fps=$(python3 -c "print(max(4, round($n / ($t1 - $t0))))")
ctl window.screenshot path="$OUT/views-modal.png" >/dev/null
ffmpeg -loglevel error -y -framerate "$fps" -i "$FRAMES/%04d.png" \
  -vf "scale=1000:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=256:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle" \
  "$OUT/hero.gif"

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

# --- views-science.gif: a spinning protein beside a live animation
cat > motion.md <<'MD'
```nebula:structure
title: 1A8O, spinning
data: 1a8o.pdb
spin: true
height: 330
```

```nebula:animation
title: Standing wave
t: [0, 6.283]
x: [0, 10]
duration: 4
functions:
  - {y: "sin(x - t)", label: right}
  - {y: "sin(x + t)", label: left}
  - {y: "sin(x - t) + sin(x + t)", label: sum}
height: 300
```
MD
ctl window.resize width=1100 height=900 >/dev/null; sleep 0.8
V=$(ctl view.show file=motion.md | python3 -c 'import json,sys;print(json.load(sys.stdin)["view"])')
sleep 5
rm -f "$FRAMES"/*.png
n=0
t0=$(date +%s.%N)
for _ in $(seq 60); do ctl window.screenshot path="$FRAMES/$(printf %04d $n).png" >/dev/null; n=$((n + 1)); done
t1=$(date +%s.%N)
fps=$(python3 -c "print(max(4, round($n / ($t1 - $t0))))")
ffmpeg -loglevel error -y -framerate "$fps" -i "$FRAMES/%04d.png" \
  -vf "scale=800:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=256:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle" \
  "$OUT/views-science.gif"
echo "media written to $OUT: $(cd "$OUT" && ls | tr '\n' ' ')"
