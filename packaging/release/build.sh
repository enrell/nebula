#!/usr/bin/env bash
# Builds the release artifacts inside an Arch environment (the CI container, or `just release-local`):
#   nebula-<ver>-x86_64-linux.tar.gz   binary tarball with a /usr prefix layout (uses the system Qt; for the AUR nebula-bin)
#   nebula-x86_64.AppImage             self-contained (bundles Qt), for `curl | sh` installs on any distro
#   SHA256SUMS
# usage: packaging/release/build.sh <version> <outdir>
set -euo pipefail
VER=${1:?version}
OUT=$(realpath -m "${2:?outdir}")
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=$(mktemp -d)
mkdir -p "$OUT"

cmake -S "$ROOT" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$WORK/build"

# --- tarball
DESTDIR="$WORK/stage" cmake --install "$WORK/build"
strip --strip-unneeded "$WORK/stage/usr/bin/nebula"
tar -C "$WORK/stage" -czf "$OUT/nebula-$VER-x86_64-linux.tar.gz" usr

# --- AppImage
DESTDIR="$WORK/AppDir" cmake --install "$WORK/build"
cd "$WORK"
curl -fsSL -o linuxdeploy https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
curl -fsSL -o linuxdeploy-plugin-qt https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x linuxdeploy linuxdeploy-plugin-qt
# Wayland (Omarchy/Hyprland) plus offscreen for the smoke test; xcb is deployed by default
export EXTRA_PLATFORM_PLUGINS="libqwayland.so;libqoffscreen.so"
export EXTRA_QT_PLUGINS="wayland-decoration-client;wayland-graphics-integration-client;wayland-shell-integration"
export APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1 QMAKE=/usr/bin/qmake6 QML_SOURCES_PATHS="$ROOT/qml" LINUXDEPLOY_OUTPUT_VERSION="$VER" PATH="$WORK:$PATH"
./linuxdeploy --appdir AppDir --plugin qt --output appimage \
  -d AppDir/usr/share/applications/nebula.desktop -i AppDir/usr/share/icons/hicolor/scalable/apps/nebula.svg
mv ./*.AppImage "$OUT/nebula-x86_64.AppImage"
"$ROOT/packaging/release/smoke.sh" "$OUT/nebula-x86_64.AppImage"   # never ship an AppImage that does not start

cd "$OUT" && sha256sum "nebula-$VER-x86_64-linux.tar.gz" nebula-x86_64.AppImage > SHA256SUMS
ls -la "$OUT"
