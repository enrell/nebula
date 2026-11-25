# Changelog

All notable changes to nebula are documented here. Release tags match `project(nebula VERSION …)` in `CMakeLists.txt`.

## 0.5.0 — 2026-11-25

### Added

- **Explorer**: the project's files as a tree next to the panes (`Ctrl+Shift+F`): open, create, rename, trash; git-ignored files dimmed; follows the disk. A click opens files in the editor (reusing it, as an IDE does), `.neb` and Markdown as views.
- **System tray**: closing the window keeps nebula in the tray; the icon or starting `nebula` again brings it back (`nebula ctl window.show`). Settings > General > *Keep running in the tray*.

### Fixed

- **The armed Kill all & quit button covered the sidebar footer icons.**

## 0.4.0 — 2026-11-25

### Added

- **Editor**: a native editor pane for code and text (no web engine). Syntax colours for about twenty languages, undo by word, find and replace with regular expressions, go to line, comment toggling, line moves, auto-indent and bracket closing, mouse selection. Unsaved text survives closing, reboots and Kill all & quit. Open files with `nebula FILE`, `Ctrl+Shift+P` (quick open) or `nebula ctl editor.open`; `.neb` views get an *edit* button and Markdown editors a live *preview*.
- **Sessions come back whole**: after a reboot or Kill all & quit each pane starts its program again. Agents reopen the exact conversation they were in, with their own flags (`claude --resume <id>`, `opencode --session <id>`, `codex resume <id>`; Claude Code's id comes from its hooks with no reinstall); editors, monitors, file managers and other interactive programs restart; anything else is typed at the prompt and waits for Enter. Settings > Session > *Restore programs* chooses how; *Discard session* starts empty next time.
- **Keybindings editor**: Settings > Keybindings can be searched, and shortcuts recorded (press the keys), removed and reset per action or all at once, with conflicts named; changes go to `keys.conf` as the smallest set of overrides, comments kept. Also `nebula ctl keys.list|keys.add|keys.remove|keys.reset`.
- **`.neb` documents**: `nebula FILE.neb` (also `.md`) opens a view document in the running instance, or starts nebula with it. Linux packages and `install.sh` register the `application/x-nebula-document` type and a `MimeType` / `%F` desktop entry; `install.ps1` associates `.neb` on Windows.

### Changed

- **Kill all & quit keeps the session** (it used to save an empty one); the new *Discard session* forgets it.

### Fixed

- **Install MCP for opencode** refused an `opencode.json` with a trailing comma ("is not plain JSON"). opencode and Gemini configs are read as JSONC; a file with comments is left alone with a message saying why, and `opencode.jsonc` is used when it is the one that exists.
- **"Increase font size" showed an empty key cap**: actions with several shortcuts are drawn one shortcut at a time.
- **A settings change made just before quitting was lost** (the save timer never fired).
- **Views crashed nebula or went blank**: QML freed the shared web profile once the garbage collector ran, so a view could lose its content, and the next view pane to appear (docking or popping out a view, opening another) crashed on the dangling pointer. The profile now has C++ ownership.

## 0.3.0 — 2026-10-14

### Added

- **Windows 10 1809+ and Windows 11 (x64)**: native builds with ConPTY-backed terminals, portable `nebula-windows-x86_64.zip`, and `install.ps1` (user install, no admin).
- **Windows CI**: end-to-end tests and install-doc screenshot on a real desktop session.
- **Windows integration**: settings under `%APPDATA%\nebula`, secrets in the Credential Manager, automation via the `\\.\pipe\nebula-<user>` named pipe, and Windows toast notifications.

### Changed

- Terminal I/O on Windows goes through a shared `Pty` abstraction (ConPTY on Windows, POSIX PTY on Linux).
