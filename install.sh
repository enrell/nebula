#!/bin/sh
# nebula installer (any x86_64 Linux): downloads the AppImage from the latest GitHub release and installs it
# into ~/.local, with a desktop entry and icon. No root needed.
#
#   curl -fsSL https://raw.githubusercontent.com/enrell/nebula/main/install.sh | sh
#   curl -fsSL .../install.sh | sh -s -- --uninstall
#
# Environment: NEBULA_VERSION=0.1.0 (default: latest)   NEBULA_PREFIX=~/.local   NEBULA_BASE_URL=<mirror>
set -eu

REPO=enrell/nebula
PREFIX=${NEBULA_PREFIX:-$HOME/.local}
VERSION=${NEBULA_VERSION:-latest}
APP=$PREFIX/bin/nebula
DESKTOP=$PREFIX/share/applications/nebula.desktop
ICON=$PREFIX/share/icons/hicolor/scalable/apps/nebula.svg

say() { printf '\033[1mnebula:\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mnebula:\033[0m %s\n' "$*" >&2; exit 1; }

if [ "${1:-}" = "--uninstall" ]; then
  rm -f "$APP" "$DESKTOP" "$ICON"
  say "removed $APP (your settings in ~/.config/nebula are untouched)"
  exit 0
fi

[ "$(uname -s)" = Linux ] || die "nebula only runs on Linux"
[ "$(uname -m)" = x86_64 ] || die "no prebuilt binary for $(uname -m); build from source (see the README)"
command -v curl >/dev/null 2>&1 || die "curl is required"
command -v sha256sum >/dev/null 2>&1 || die "sha256sum is required"

if command -v pacman >/dev/null 2>&1; then
  say "tip: on Arch, 'yay -S nebula-bin' (AUR) uses your system Qt. Continuing with the AppImage."
fi

if [ -n "${NEBULA_BASE_URL:-}" ]; then base=$NEBULA_BASE_URL   # mirror / local testing
elif [ "$VERSION" = latest ]; then base="https://github.com/$REPO/releases/latest/download"
else base="https://github.com/$REPO/releases/download/v${VERSION#v}"; fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"

say "downloading nebula ($VERSION)..."
curl -fL --progress-bar -o nebula-x86_64.AppImage "$base/nebula-x86_64.AppImage" || die "download failed (is there a release yet?)"
curl -fsSL -o SHA256SUMS "$base/SHA256SUMS" || die "could not fetch SHA256SUMS"
grep ' nebula-x86_64.AppImage$' SHA256SUMS | sha256sum -c - >/dev/null 2>&1 || die "checksum mismatch, refusing to install"
say "checksum ok"

chmod +x nebula-x86_64.AppImage
mkdir -p "$PREFIX/bin" "$PREFIX/share/applications" "$PREFIX/share/icons/hicolor/scalable/apps"
install -m755 nebula-x86_64.AppImage "$APP"

# the icon lives inside the AppImage; extracting a file needs no FUSE
if ./nebula-x86_64.AppImage --appimage-extract 'usr/share/icons/hicolor/scalable/apps/nebula.svg' >/dev/null 2>&1; then
  install -m644 squashfs-root/usr/share/icons/hicolor/scalable/apps/nebula.svg "$ICON"
fi
cat > "$DESKTOP" <<DESK
[Desktop Entry]
Type=Application
Name=Nebula
Comment=Native workspace manager for terminal AI agents
Exec=$APP
Icon=nebula
Terminal=false
Categories=Development;System;TerminalEmulator;
StartupWMClass=nebula
DESK

say "installed to $APP"
case ":$PATH:" in *":$PREFIX/bin:"*) ;; *) say "add $PREFIX/bin to your PATH to run 'nebula' from a shell" ;; esac
say "needs libfuse2 (or run with APPIMAGE_EXTRACT_AND_RUN=1) and the usual desktop libs: OpenGL (libglvnd), fontconfig, harfbuzz"
