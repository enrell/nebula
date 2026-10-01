#!/usr/bin/env bash
# Portable build: nebula plus the Qt it needs in one relocatable directory, for `install.sh` on any distro.
# Built on Ubuntu 22.04 (glibc 2.35) with the official Qt from aqtinstall and a static libvterm, so it runs on
# practically every current x86_64 / aarch64 distro. System libs it still expects: glibc, OpenGL/EGL, fontconfig,
# freetype, X11/xcb or Wayland client libs (present on any desktop).
#   packaging/release/bundle.sh <version> <outdir>      (as root in ubuntu:22.04, or with sudo on a 22.04 runner)
set -euo pipefail
VER=${1:?version}
OUT=$(realpath -m "${2:?outdir}")
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
QT_VER=6.8.3
VTERM_VER=0.3.3
SUDO=$([ "$(id -u)" = 0 ] && echo || echo sudo)
case $(uname -m) in
  x86_64)  ARCH=x86_64;  QT_HOST=linux;       QT_ARCH=linux_gcc_64;    QT_SUB=gcc_64;    LD_ARCH=x86_64 ;;
  aarch64) ARCH=aarch64; QT_HOST=linux_arm64; QT_ARCH=linux_gcc_arm64; QT_SUB=gcc_arm64; LD_ARCH=aarch64 ;;
  *) echo "unsupported arch $(uname -m)" >&2; exit 2 ;;
esac
WORK=$(mktemp -d)
mkdir -p "$OUT"

export DEBIAN_FRONTEND=noninteractive
$SUDO apt-get update -qq
$SUDO apt-get install -y -qq --no-install-recommends build-essential cmake ninja-build pkg-config git curl file ca-certificates \
  libtool-bin python3-pip libgl-dev libegl-dev libxkbcommon-dev libxkbcommon-x11-0 libfontconfig1-dev libfreetype6 \
  libwayland-dev libwayland-egl1 libdbus-1-3 libglib2.0-0 \
  libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 \
  libxcb-xkb1 libxcb-xinerama0 libxcb-xinput0 \
  libnss3 libxcomposite1 libxdamage1 libxrandr2 libxtst6 libxkbfile1 libxshmfence1 libdrm2 libgbm1 \
  $(apt-cache show libasound2t64 >/dev/null 2>&1 && echo libasound2t64 || echo libasound2) >/dev/null

# Qt (official binaries)
python3 -m pip install -q aqtinstall
python3 -m aqt install-qt "$QT_HOST" desktop "$QT_VER" "$QT_ARCH" -m qtwebengine qtwebchannel qtpositioning qtserialport -O "$WORK/qt" >/dev/null
QTDIR=$WORK/qt/$QT_VER/$QT_SUB

# libvterm: distro versions are too old (0.1.x lacks the string/conceal API), link it statically
curl -fsSL "https://launchpad.net/libvterm/trunk/v0.3/+download/libvterm-$VTERM_VER.tar.gz" | tar -xz -C "$WORK"
make -s -C "$WORK/libvterm-$VTERM_VER" PREFIX="$WORK/vterm" install >/dev/null
rm -f "$WORK"/vterm/lib/libvterm.so* "$WORK"/vterm/lib/libvterm.la

PKG_CONFIG_PATH="$WORK/vterm/lib/pkgconfig" cmake -S "$ROOT" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_PREFIX_PATH="$QTDIR"
cmake --build "$WORK/build"
DESTDIR="$WORK/AppDir" cmake --install "$WORK/build"

# deploy Qt libs/plugins/QML into the tree and fix rpaths (linuxdeploy, used here only as a deployer)
cd "$WORK"
curl -fsSL -o linuxdeploy "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$LD_ARCH.AppImage"
curl -fsSL -o linuxdeploy-plugin-qt "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-$LD_ARCH.AppImage"
chmod +x linuxdeploy linuxdeploy-plugin-qt
export APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1 QMAKE="$QTDIR/bin/qmake" QML_SOURCES_PATHS="$ROOT/qml" PATH="$WORK:$PATH" LD_LIBRARY_PATH="$QTDIR/lib"
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so;libqoffscreen.so"
export EXTRA_QT_PLUGINS="wayland-decoration-client;wayland-shell-integration"
./linuxdeploy --appdir AppDir --plugin qt
# Wayland needs its GL buffer integration plugin, which the qt plugin does not pick up on its own
GI=AppDir/usr/plugins/wayland-graphics-integration-client
mkdir -p "$GI" && cp "$QTDIR"/plugins/wayland-graphics-integration-client/*.so "$GI/"
./linuxdeploy --appdir AppDir --deploy-deps-only "$GI"
test -e "$GI/libqt-plugin-wayland-egl.so"
test -e AppDir/usr/bin/qt.conf || printf '[Paths]\nPrefix = ../\nPlugins = plugins\nQml2Imports = qml\n' > AppDir/usr/bin/qt.conf

mv AppDir/usr nebula
strip --strip-unneeded nebula/bin/nebula
echo "$VER" > nebula/VERSION
tar -czf "$OUT/nebula-linux-$ARCH.tar.gz" nebula
echo "bundle: $OUT/nebula-linux-$ARCH.tar.gz ($(du -h "$OUT/nebula-linux-$ARCH.tar.gz" | cut -f1))"
