#!/usr/bin/env bash
# Builds a native package inside the matching distro container and installs + smoke-tests it there.
#   packaging/release/native.sh deb  <outdir>   # debian:trixie / ubuntu >= 25.04 (Qt >= 6.5)
#   packaging/release/native.sh rpm  <outdir>   # fedora >= 41
#   packaging/release/native.sh arch <outdir>   # archlinux
# Runs as root in a throw-away container (installs build deps).
set -euo pipefail
KIND=${1:?deb|rpm|arch}
OUT=$(realpath -m "${2:?outdir}")
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=$(mktemp -d)
mkdir -p "$OUT"
cp -a "$ROOT/." "$WORK/src"; rm -rf "$WORK"/src/build* "$WORK/src/dist"

case $KIND in
deb)
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq
  apt-get install -y -qq --no-install-recommends build-essential cmake ninja-build pkg-config git file dpkg-dev \
    qt6-base-dev qt6-declarative-dev qt6-base-private-dev libvterm-dev libgl-dev >/dev/null
  cmake -S "$WORK/src" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build "$WORK/build"
  (cd "$WORK/build" && cpack -G DEB >/dev/null)
  PKG=$(ls "$WORK"/build/*.deb)
  apt-get install -y -qq "$PKG" >/dev/null   # resolves the runtime deps the package declares
  ;;
rpm)
  dnf install -y -q gcc-c++ cmake ninja-build pkgconf git file rpm-build \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtbase-private-devel libvterm-devel >/dev/null
  cmake -S "$WORK/src" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build "$WORK/build"
  (cd "$WORK/build" && cpack -G RPM >/dev/null)
  PKG=$(ls "$WORK"/build/*.rpm)
  dnf install -y -q "$PKG" >/dev/null
  ;;
arch)
  pacman -Syu --noconfirm --needed base-devel git cmake ninja qt6-base qt6-declarative qt6-wayland libvterm >/dev/null
  useradd -m builder 2>/dev/null || true
  chown -R builder "$WORK"
  su builder -c "cd '$WORK/src/packaging/arch' && NEBULA_RELEASE=1 makepkg -f --nocheck --noconfirm >/dev/null"
  PKG=$(ls "$WORK"/src/packaging/arch/*.pkg.tar.zst)
  pacman -U --noconfirm "$PKG" >/dev/null
  ;;
*) echo "unknown kind $KIND" >&2; exit 2 ;;
esac

# the installed package must start and load all of its QML without warnings
"$ROOT/tests/qml_sanity.sh" /usr/bin/nebula
cp "$PKG" "$OUT/"
ls -la "$OUT"
