# nebula - task runner. Run `just` to list recipes.

set shell := ["bash", "-euo", "pipefail", "-c"]

build_dir := "build"
dev_dir   := "build-dev"
prefix    := env_var_or_default("PREFIX", "/usr/local")

# dev instances use their own socket and state so they never touch your real session
# NEBULA_QML_DIR makes the dev instance load qml/ from disk and hot-reload it on save (no rebuild for UI changes)
dev_env := "NEBULA_SOCKET=${XDG_RUNTIME_DIR:-/tmp}/nebula-dev.sock NEBULA_STATE_DIR=/tmp/nebula-dev-state NEBULA_QML_DIR=" + justfile_directory() + "/qml QT_FORCE_STDERR_LOGGING=1"

default:
    @just --list --unsorted

# Configure + build (release)
build:
    cmake -S . -B {{build_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build {{build_dir}}

# Build (release) and run the real app
run *ARGS: build
    ./{{build_dir}}/nebula {{ARGS}}

# Debug build + run as an isolated dev instance (separate socket/state)
dev *ARGS:
    cmake -S . -B {{dev_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cmake --build {{dev_dir}}
    {{dev_env}} ./{{dev_dir}}/nebula {{ARGS}}

# Rebuild and restart the dev instance only when C++ changes (needs watchexec). QML edits hot-reload by themselves.
watch:
    watchexec --restart --watch src --watch CMakeLists.txt --exts cpp,h,txt -- just dev

# Run unit tests
test:
    cmake -S . -B {{dev_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Debug
    cmake --build {{dev_dir}}
    ctest --test-dir {{dev_dir}} --output-on-failure

# End-to-end check of the automation API against a throw-away instance (headless-capable via QT_QPA_PLATFORM=offscreen)
e2e: build
    ./tests/e2e.sh ./{{build_dir}}/nebula

# Views: rebuild the renderer bundles (renderer/dist, committed) and run the checker tests; needs node
renderer:
    cd renderer && npm ci && npm test

# Views end to end: checker report, drawing, updates, live reload, persistence
e2e-views: build
    ./tests/e2e_views.sh ./{{build_dir}}/nebula

# Session persistence: shells survive the GUI, layout survives the host
e2e-persist: build
    ./tests/e2e_persist.sh ./{{build_dir}}/nebula

# Profiles, launcher, LLM plumbing, hooks/MCP installers and the MCP bridge (fake provider, throw-away HOME)
e2e-agents: build
    ./tests/e2e_agents.sh ./{{build_dir}}/nebula

# Headless screenshot of a throw-away instance (never touches your desktop): just shot /tmp/nebula.png
shot OUT="/tmp/nebula.png": build
    #!/usr/bin/env bash
    set -euo pipefail
    unset WAYLAND_DISPLAY DISPLAY
    export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QPA_PLATFORMTHEME= GDK_BACKEND= NEBULA_SOCKET=${XDG_RUNTIME_DIR:-/tmp}/nebula-shot.sock NEBULA_STATE_DIR=$(mktemp -d)
    B=./{{build_dir}}/nebula
    $B & PID=$!
    trap '$B ctl action.run action=kill-session >/dev/null 2>&1 || true; kill $PID 2>/dev/null || true; rm -rf "$NEBULA_STATE_DIR"' EXIT
    for _ in $(seq 50); do $B ctl ping >/dev/null 2>&1 && break; sleep 0.1; done
    sleep 1
    $B ctl window.screenshot path={{OUT}}

# Regenerate the README screenshots (headless, throw-away HOME, fake agents)
screenshots: build
    ./tests/screenshots.sh ./{{build_dir}}/nebula

# Build an Arch package from this checkout (packaging/arch/PKGBUILD)
pkg:
    cd packaging/arch && makepkg -f

# Build every release artifact locally in docker, like the release workflow (x86_64 only): dist/
release-local VERSION="0.0.0-local":
    mkdir -p dist
    docker run --rm -v "{{justfile_directory()}}":/src:ro -v "{{justfile_directory()}}/dist":/out ubuntu:22.04 /src/packaging/release/bundle.sh {{VERSION}} /out
    docker run --rm -v "{{justfile_directory()}}":/src:ro -v "{{justfile_directory()}}/dist":/out debian:trixie /src/packaging/release/native.sh deb /out
    docker run --rm -v "{{justfile_directory()}}":/src:ro -v "{{justfile_directory()}}/dist":/out fedora:42 /src/packaging/release/native.sh rpm /out
    docker run --rm -v "{{justfile_directory()}}":/src:ro -v "{{justfile_directory()}}/dist":/out archlinux:latest /src/packaging/release/native.sh arch /out

# Lint QML files
lint:
    qmllint -I {{dev_dir}} qml/*.qml || true

# Format C++ sources
fmt:
    clang-format -i src/*.cpp src/*.h tests/*.cpp

# Talk to a running instance: just ctl pane.list / just ctl pane.send_text text=ls enter=true
ctl *ARGS:
    ./{{build_dir}}/nebula ctl {{ARGS}}

# Same, against the dev instance
dctl *ARGS:
    {{dev_env}} ./{{dev_dir}}/nebula ctl {{ARGS}}

# Stream agent events from the running instance
events:
    ./{{build_dir}}/nebula ctl --watch

# Install binary + desktop entry (PREFIX=/usr/local by default)
install: build
    cmake --install {{build_dir}} --prefix {{prefix}}

uninstall:
    rm -f {{prefix}}/bin/nebula {{prefix}}/share/applications/nebula.desktop {{prefix}}/share/icons/hicolor/scalable/apps/nebula.svg
    rm -rf {{prefix}}/share/licenses/nebula

# Remove build directories
clean:
    rm -rf {{build_dir}} {{dev_dir}}
