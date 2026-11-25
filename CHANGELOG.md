# Changelog

All notable changes to nebula are documented here. Release tags match `project(nebula VERSION …)` in `CMakeLists.txt`.

## Unreleased

### Added

- **Editor**: a native editor pane for code and text (no web engine). Syntax colours for about twenty languages, undo by word, find and replace with regular expressions, go to line, comment toggling, line moves, auto-indent and bracket closing, mouse selection. Unsaved text survives closing, reboots and Kill all & quit. Open files with `nebula FILE`, `Ctrl+Shift+P` (quick open) or `nebula ctl editor.open`; `.neb` views get an *edit* button and Markdown editors a live *preview*.
- **`.neb` documents**: `nebula FILE.neb` (also `.md`) opens a view document in the running instance, or starts nebula with it. Linux packages and `install.sh` register the `application/x-nebula-document` type and a `MimeType` / `%F` desktop entry; `install.ps1` associates `.neb` on Windows.

## 0.3.0 — 2026-10-14

### Added

- **Windows 10 1809+ and Windows 11 (x64)**: native builds with ConPTY-backed terminals, portable `nebula-windows-x86_64.zip`, and `install.ps1` (user install, no admin).
- **Windows CI**: end-to-end tests and install-doc screenshot on a real desktop session.
- **Windows integration**: settings under `%APPDATA%\nebula`, secrets in the Credential Manager, automation via the `\\.\pipe\nebula-<user>` named pipe, and Windows toast notifications.

### Changed

- Terminal I/O on Windows goes through a shared `Pty` abstraction (ConPTY on Windows, POSIX PTY on Linux).
