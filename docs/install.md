# Install

Back to the [README](../README.md).

Every release has native packages (small, use your distro's Qt) and a portable build (Qt bundled, any distro):

| Distro | Install |
|---|---|
| Arch / Omarchy | `sudo pacman -U https://github.com/enrell/nebula/releases/latest/download/nebula-<ver>-1-x86_64.pkg.tar.zst` (AUR `nebula-bin` / `nebula-git` once AUR registration reopens; the PKGBUILDs are in `packaging/aur/`) |
| Debian 13, Ubuntu 25.04+ | download `nebula_<ver>_amd64.deb` / `arm64.deb` from the release, `sudo apt install ./nebula_*.deb` |
| Fedora 41+ | `sudo dnf install <url of nebula-<ver>-1.x86_64.rpm>` |
| Anything else (x86_64, aarch64) | `curl -fsSL https://raw.githubusercontent.com/enrell/nebula/main/install.sh \| sh` |

The installer puts the portable build in `~/.local/lib/nebula` (no root, checksum verified) and links `~/.local/bin/nebula`; `... | sh -s -- --uninstall` removes it. The x86_64 build is made on Ubuntu 22.04 (glibc 2.35 or newer), the aarch64 one on Ubuntu 24.04 (glibc 2.39 or newer); both only expect the usual desktop libraries (OpenGL/EGL, fontconfig, X11 or Wayland).

**From source** (Qt >= 6.5 with Quick, WebEngine and WebChannel, libvterm >= 0.3, CMake, Ninja):

    sudo pacman -S qt6-base qt6-declarative qt6-wayland qt6-webengine qt6-webchannel libvterm cmake ninja   # or your distro's equivalents
    just build && sudo just install

Releases are built by CI when a `vX.Y.Z` tag matching the version in `CMakeLists.txt` is pushed (`.github/workflows/release.yml`).
