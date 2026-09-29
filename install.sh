#!/bin/sh
# nebula installer for any x86_64 / aarch64 Linux: installs the portable build (Qt bundled) from the latest GitHub
# release into ~/.local, with a desktop entry and icon. No root needed.
#
#   curl -fsSL https://raw.githubusercontent.com/enrell/nebula/main/install.sh | sh
#   curl -fsSL .../install.sh | sh -s -- --uninstall
#
# Environment: NEBULA_VERSION=0.1.1 (default: latest)   NEBULA_PREFIX=~/.local   NEBULA_BASE_URL=<mirror>
set -eu

REPO=enrell/nebula
PREFIX=${NEBULA_PREFIX:-$HOME/.local}
VERSION=${NEBULA_VERSION:-latest}
DIR=$PREFIX/lib/nebula
BIN=$PREFIX/bin/nebula
DESKTOP=$PREFIX/share/applications/nebula.desktop
ICON=$PREFIX/share/icons/hicolor/scalable/apps/nebula.svg

say() { printf '\033[1mnebula:\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mnebula:\033[0m %s\n' "$*" >&2; exit 1; }

if [ "${1:-}" = "--uninstall" ]; then
  rm -rf "$DIR"; rm -f "$BIN" "$DESKTOP" "$ICON"
  say "removed (your settings in ~/.config/nebula are untouched)"
  exit 0
fi

[ "$(uname -s)" = Linux ] || die "nebula only runs on Linux"
case $(uname -m) in
  x86_64|amd64) ARCH=x86_64 ;;
  aarch64|arm64) ARCH=aarch64 ;;
  *) die "no prebuilt build for $(uname -m); build from source (see the README)" ;;
esac
for c in curl tar sha256sum; do command -v $c >/dev/null 2>&1 || die "$c is required"; done

if [ -n "${NEBULA_BASE_URL:-}" ]; then base=$NEBULA_BASE_URL
elif [ "$VERSION" = latest ]; then base="https://github.com/$REPO/releases/latest/download"
else base="https://github.com/$REPO/releases/download/v${VERSION#v}"; fi

# a native package is smaller and uses the system Qt; mention it where there is one
if command -v pacman >/dev/null 2>&1; then say "tip: the release also has an Arch package (nebula-*.pkg.tar.zst): sudo pacman -U <url>"
elif command -v apt-get >/dev/null 2>&1; then say "tip: on Debian 13 / Ubuntu 25.04+ the release also has a .deb"
elif command -v dnf >/dev/null 2>&1; then say "tip: on Fedora 41+ the release also has an .rpm"; fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"
file=nebula-linux-$ARCH.tar.gz
say "downloading nebula ($VERSION, $ARCH)..."
curl -fL --progress-bar -o "$file" "$base/$file" || die "download failed"
curl -fsSL -o SHA256SUMS "$base/SHA256SUMS" || die "could not fetch SHA256SUMS"
grep " $file\$" SHA256SUMS | sha256sum -c - >/dev/null 2>&1 || die "checksum mismatch, refusing to install"
say "checksum ok"
tar -xzf "$file"

mkdir -p "$PREFIX/lib" "$PREFIX/bin" "$PREFIX/share/applications" "$(dirname "$ICON")"
rm -rf "$DIR"
mv nebula "$DIR"
rm -f "$BIN"                                    # may be an older AppImage install
ln -s "$DIR/bin/nebula" "$BIN"
cp "$DIR/share/icons/hicolor/scalable/apps/nebula.svg" "$ICON"
cat > "$DESKTOP" <<DESK
[Desktop Entry]
Type=Application
Name=Nebula
Comment=Native workspace manager for terminal AI agents
Exec=$BIN
Icon=nebula
Terminal=false
Categories=Development;System;TerminalEmulator;
StartupWMClass=nebula
DESK

say "installed $(cat "$DIR/VERSION" 2>/dev/null) to $DIR"
case ":$PATH:" in *":$PREFIX/bin:"*) ;; *) say "add $PREFIX/bin to your PATH to run 'nebula' from a shell" ;; esac
