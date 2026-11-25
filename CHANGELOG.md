# Changelog

All notable changes to nebula are documented here. Release tags match `project(nebula VERSION …)` in `CMakeLists.txt`.

## Unreleased

### Fixed

- **Views crashed nebula or went blank**: QML freed the shared web profile once the garbage collector ran, so a view could lose its content, and the next view pane to appear (docking or popping out a view, opening another) crashed on the dangling pointer. The profile now has C++ ownership.

## 0.3.0 — 2026-10-14

### Added

- **Windows 10 1809+ and Windows 11 (x64)**: native builds with ConPTY-backed terminals, portable `nebula-windows-x86_64.zip`, and `install.ps1` (user install, no admin).
- **Windows CI**: end-to-end tests and install-doc screenshot on a real desktop session.
- **Windows integration**: settings under `%APPDATA%\nebula`, secrets in the Credential Manager, automation via the `\\.\pipe\nebula-<user>` named pipe, and Windows toast notifications.

### Changed

- Terminal I/O on Windows goes through a shared `Pty` abstraction (ConPTY on Windows, POSIX PTY on Linux).
