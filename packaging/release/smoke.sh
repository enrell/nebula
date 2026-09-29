#!/usr/bin/env bash
# Headless smoke test of an AppImage: it must start, answer on its socket and render a frame.
#   packaging/release/smoke.sh path/to/nebula-x86_64.AppImage
set -euo pipefail
AI=$(realpath "$1")
export HOME=$(mktemp -d)
unset WAYLAND_DISPLAY DISPLAY
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software APPIMAGE_EXTRACT_AND_RUN=1 NEBULA_SECRETS=file
export NEBULA_SOCKET=$HOME/smoke.sock NEBULA_STATE_DIR=$HOME/state APPIMAGE=$AI
# extract once: every APPIMAGE_EXTRACT_AND_RUN invocation would otherwise unpack 150 MB again
( cd "$HOME" && "$AI" --appimage-extract >/dev/null )
RUN="$HOME/squashfs-root/AppRun"
"$RUN" >"$HOME/gui.log" 2>&1 & GUI=$!
trap 'kill $GUI 2>/dev/null || true; pkill -f "squashfs-root/.*--host" 2>/dev/null || true' EXIT
for _ in $(seq 100); do "$RUN" ctl ping >/dev/null 2>&1 && break; sleep 0.2; done
"$RUN" ctl ping >/dev/null || { echo "smoke: AppImage did not start" >&2; cat "$HOME/gui.log" >&2; exit 1; }
"$RUN" ctl window.screenshot path="$HOME/shot.png" >/dev/null
[ "$(stat -c %s "$HOME/shot.png")" -gt 5000 ] || { echo "smoke: blank screenshot" >&2; exit 1; }
"$RUN" ctl action.run action=kill-session >/dev/null 2>&1 || true
echo "smoke: AppImage ok"
