# Install

Back to the [README](../README.md).

Every release has native packages (small, use your distro's Qt) and a portable build (Qt bundled, any distro):

| Distro | Install |
|---|---|
| Arch / Omarchy | `sudo pacman -U https://github.com/enrell/nebula/releases/latest/download/nebula-<ver>-1-x86_64.pkg.tar.zst` (AUR `nebula-bin` / `nebula-git` once AUR registration reopens; the PKGBUILDs are in `packaging/aur/`) |
| Debian 13, Ubuntu 25.04+ | download `nebula_<ver>_amd64.deb` / `arm64.deb` from the release, `sudo apt install ./nebula_*.deb` |
| Fedora 41+ | `sudo dnf install <url of nebula-<ver>-1.x86_64.rpm>` |
| Anything else (x86_64, aarch64) | `curl -fsSL https://raw.githubusercontent.com/enrell/nebula/main/install.sh \| sh` |
| Windows 10 1809+, Windows 11 (x64) | in PowerShell: `irm https://raw.githubusercontent.com/enrell/nebula/main/install.ps1 \| iex` |

The installer puts the portable build in `~/.local/lib/nebula` (no root, checksum verified) and links `~/.local/bin/nebula`; `... | sh -s -- --uninstall` removes it. The x86_64 build is made on Ubuntu 22.04 (glibc 2.35 or newer), the aarch64 one on Ubuntu 24.04 (glibc 2.39 or newer); both only expect the usual desktop libraries (OpenGL/EGL, fontconfig, X11 or Wayland).

## Windows

`install.ps1` downloads `nebula-windows-x86_64.zip` from the release (checksum verified), unpacks it into `%LOCALAPPDATA%\Programs\nebula`, adds that folder to your user `PATH` and creates a Start Menu entry. No administrator rights are needed. Run it with `-Version 0.3.0` for a specific release, `-Zip nebula-windows-x86_64.zip` to install a file you already have, or `-Uninstall` to remove it (your settings in `%APPDATA%\nebula` stay). To remove it from a one-liner: `& ([scriptblock]::Create((irm https://raw.githubusercontent.com/enrell/nebula/main/install.ps1))) -Uninstall`. You can also unzip the release anywhere and run `nebula.exe`.

Panes run PowerShell 7 if it is installed, else Windows PowerShell; pick another shell (`cmd`, Git Bash, `nu`, ...) under Settings > Terminal. Terminals need the Windows pseudo console, so Windows 10 version 1809 or newer.

Things that differ from Linux:

- **Where things are**: settings and other config in `%APPDATA%\nebula`, session state in `%LOCALAPPDATA%\nebula\state`, API keys in the Windows Credential Manager. The automation socket is the named pipe `\\.\pipe\nebula-<user>`.
- **Working directory**: PowerShell's `cd` does not change the directory of the process, so nebula adds a small prompt hook to the PowerShell it starts, which reports the directory to the window (it wraps whatever prompt your profile sets up, so oh-my-posh and friends keep working). New splits open where the shell is, and the sidebar shows the git branch. Without the hook (a PowerShell you start yourself with arguments, or other shells) the process's own directory is used, which is right for `cmd` and Git Bash.
- **Agents**: they are recognised from the process tree (`claude.exe`, `node ... claude-code`, `.cmd` shims), and the status hooks call `nebula.exe ctl --hook`, so they need no `sh`. Agents that run inside WSL cannot be seen from outside; run nebula's Linux build inside WSL (WSLg) for those.
- **`nebula ctl` and `nebula mcp`** work from any terminal. nebula.exe is a GUI program, so in an interactive PowerShell its output can appear after the next prompt; piping it (`nebula ctl pane.list | more`) or using `Start-Process -Wait -NoNewWindow` shows it in order.
- **Notifications** are Windows toasts, sent through Windows PowerShell.
- **Theme**: there is no Omarchy on Windows, so nebula uses its dark default colours and the Cascadia Mono font (or Consolas); both can be changed in the settings.

**From source** (Qt >= 6.5 with Quick, WebEngine and WebChannel, libvterm >= 0.3, CMake, Ninja):

    sudo pacman -S qt6-base qt6-declarative qt6-wayland qt6-webengine qt6-webchannel libvterm cmake ninja   # or your distro's equivalents
    just build && sudo just install

**From source on Windows** (Visual Studio 2022 C++ tools, Qt 6.8 for MSVC 2022 with the WebEngine, WebChannel and Positioning modules, CMake, Ninja; libvterm is built from `third_party/libvterm`), in an "x64 Native Tools" prompt:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
    cmake --build build
    ctest --test-dir build --output-on-failure
    packaging\release\windows.ps1 -Version 0.0.0-dev -Build build     # a portable zip in dist\

Releases are built by CI when a `vX.Y.Z` tag matching the version in `CMakeLists.txt` is pushed (`.github/workflows/release.yml`).
