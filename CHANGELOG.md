# Changelog

All notable changes to nebula are documented here. Release tags match `project(nebula VERSION …)` in `CMakeLists.txt`.

## 0.3.0 — 2026-10-14

### Added

- **Windows 10 1809+ and Windows 11 (x64)**: native builds with ConPTY-backed terminals, portable `nebula-windows-x86_64.zip`, and `install.ps1` (user install, no admin).
- **Windows CI**: end-to-end tests and install-doc screenshot on a real desktop session.
- **Windows integration**: settings under `%APPDATA%\nebula`, secrets in the Credential Manager, automation via the `\\.\pipe\nebula-<user>` named pipe, and Windows toast notifications.

### Changed

- Terminal I/O on Windows goes through a shared `Pty` abstraction (ConPTY on Windows, POSIX PTY on Linux).
