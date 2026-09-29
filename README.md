# nebula

Native Qt Quick (QML) workspace manager for terminal AI agents, inspired by herdr. Follows the current Omarchy theme and terminal font live.

Stack: C++20 + Qt 6 Quick, libvterm (emulation), forkpty (PTY).

## Development (`just`)

| Recipe | What |
|---|---|
| `just build` / `just run` | release build / build and run |
| `just dev` | debug build, isolated instance (own socket + state) |
| `just watch` | rebuild + restart dev instance on change (watchexec) |
| `just test` / `just e2e` | unit tests / API end-to-end test |
| `just ctl <args>` / `just dctl <args>` | talk to the running / dev instance |
| `just events` | stream agent events |
| `just install` / `just clean` / `just fmt` / `just lint` | misc |

## Keybindings (no prefix; press `Ctrl+Shift+?` in the app)

| Keys | Action |
|---|---|
| `Ctrl+Shift+D` / `Ctrl+Shift+E` | split right / down |
| `Ctrl+Shift+W` / `Ctrl+Shift+Z` | close / zoom pane |
| `Ctrl+Shift+Arrows` | focus pane |
| `Ctrl+Shift+T`, `Ctrl+Tab`, `Ctrl+Shift+Tab`, `Ctrl+PgUp/PgDn`, `Alt+1-9` | new tab, next/prev, jump |
| `Ctrl+Shift+N` / `Ctrl+Shift+Q` / `Ctrl+Shift+R` | new / close / rename space |
| `Ctrl+Shift+PgUp/PgDn`, `Ctrl+Alt+1-9` | prev/next space, jump |
| `Ctrl+Shift+A` | jump to the agent that is blocked / done |
| `Ctrl+Shift+L` / `Ctrl+Shift+M` | launch an agent / broadcast a prompt |
| `Ctrl+Shift+I` | operator (built-in AI that runs nebula) |
| `Ctrl+Shift+B` / `Ctrl+,` | toggle sidebar / settings |
| `Ctrl++` `Ctrl+-` `Ctrl+0` / `Ctrl+wheel` | font size |
| `Ctrl+Shift+C/V`, `Shift+PgUp/PgDn` | copy/paste, scrollback |

All `Ctrl+Shift+…` combos are unused by terminal programs, so nothing is stolen from your shell/TUIs. Override in `~/.config/nebula/keys.conf`:

    Ctrl+Shift+D = split-right
    Alt+Left = focus-left
    Ctrl+Shift+Q = none

Actions: split-right split-down close-pane zoom-pane focus-left/right/up/down new-tab close-tab next-tab prev-tab goto-tab:N new-space rename-space close-space next-space prev-space goto-space:N next-attention toggle-sidebar open-settings launch-agent operator broadcast toggle-help font-larger font-smaller font-reset.

## Agents: profiles, launcher, hooks, MCP, operator

**Profiles** (Settings > Providers): a named API key (Anthropic, OpenAI, OpenRouter, Gemini or any OpenAI-compatible endpoint) injected as env vars (`ANTHROPIC_API_KEY`, ...) into the panes that use it. Keys live in the system keyring via Secret Service (`secret-tool`; falls back to a 0600 file), never in `settings.json`/`session.json`. Set a default profile, a per-space profile (right-click a space), or pick one per launch. `nebula ctl profile.list|set|remove|default|test`.

**Launcher** (`Ctrl+Shift+L`, or `+` next to *agents*): pick claude/codex/opencode/gemini/aider/shell (add your own in `~/.config/nebula/agents.conf`, one `name = command` per line), profile, model, directory, where to open it (split/tab/space), optionally a fresh **git worktree + branch** so parallel agents don't collide, and an initial prompt. `nebula ctl agent.launch agent=claude prompt="fix the flaky test" worktree=true branch=fix/flaky`. `Ctrl+Shift+M` broadcasts one prompt to several agents.

**Hooks** (Settings > Agent integrations): exact `working / blocked / done` state instead of screen heuristics. Claude Code (`~/.claude/settings.json`), opencode (plugin) and codex (`notify`, turn completion only). Your files are merged, never overwritten, and backed up as `*.nebula-bak`; Remove restores them. Hooks run `nebula ctl pane.report_state` and are silent no-ops outside nebula.

**MCP server**: `nebula mcp` (stdio). Install it for claude/opencode/codex/gemini from the same settings section, or add it yourself: `claude mcp add --scope user nebula -- /path/to/nebula mcp`. Tools: `whoami`, `list_panes`, `list_agents`, `list_spaces`, `list_profiles` (never secrets), `read_pane`, `send_text`, `send_keys`, `launch_agent`, `wait_for_state`, `broadcast`, `close_pane`, `create_space`. This lets one agent delegate to and supervise others. It is real terminal control: only install it in agents you trust.

**Operator** (`Ctrl+Shift+I`, Settings > Operator): a built-in agent that operates *nebula itself*. It is a real agent CLI: nebula spawns it, gives it a `nebula` MCP server (the same tools external agents get, `src/tools.cpp`), and streams its turns into a panel. Because the agent authenticates itself, **subscriptions work out of the box** — Codex (ChatGPT) and Claude Code (subscription) are driven **natively**, with no adapter and no npm: Claude through `claude -p --input-format stream-json`, Codex through `codex app-server` (`src/nativeagents.cpp`). `opencode acp`, `gemini --acp` or any other [ACP](https://agentclientprotocol.com) command set under Settings > Agents & AI use ACP. Empty auto-detects codex → claude → opencode → gemini, and the panel header itself cycles through the detected presets. The model can be pinned via `operatorModel` (Settings > Operator > Model); when left empty, a stale model in the agent's own CLI config is healed automatically through `session/set_config_option` (or the native equivalent) using the account's real model list. Tell it things like "launch claude and codex on the flaky auth test, each in a worktree" or "which agents are blocked? approve the first one"; it lists/reads panes, launches and prompts agents, answers permission prompts, and reorganizes spaces/tabs/splits. Every tool call the agent wants to run shows an Allow/Deny prompt in the panel unless you turn on Auto-approve. Pane text is marked untrusted in the instructions sent to the agent. Non-interactive use: `nebula ctl operator.ask prompt="..." approve=all` (`approve=none` rejects every permission request).

**Summaries** (optional, off by default because it sends terminal output to your provider): a one-line "what it did" when an unfocused agent finishes, shown in the sidebar and the desktop notification. Toggle under Settings > AI features. `nebula ctl llm.ask prompt="..."` is a plain one-shot chat request against a profile.

## Sessions (zellij-style persistence)

Panes are owned by a small background daemon (`nebula --host`, started automatically), not by the window. Close the window, crash the GUI or `kill -9` it: shells and their jobs keep running. Next launch reattaches every pane, replaying up to 2 MB of output per pane (including alt-screen/mouse modes) so scrollback and TUIs come back as you left them.

The layout (spaces, tabs, splits, ratios, names, focus, zoom) is saved continuously to `session.json` in the state dir. If the host is gone too (reboot), the layout is rebuilt with fresh shells in the same directories; panes that were running an agent get a `[nebula] session restored` note and the resume command (`claude --continue`, `opencode --continue`, `codex resume --last`, ...) typed at the prompt, ready for Enter. Panes that survive in the host but not in the layout show up in a `recovered` space.

Closing a pane/tab/space kills its shells. `kill-session` (Settings, or `nebula ctl action.run action=kill-session`) terminates everything and stops the host. Tests: `just e2e-persist`.

## Mouse

Click a pane to focus; drag dividers (double-click resets to 50%); drag the sidebar edge to resize it. Double-click selects a word, triple-click a line, drag selects (auto-copies), middle-click pastes, `Ctrl+click` opens a link, right-click opens a context menu (copy/paste/split/zoom/close/settings). Hover a pane for split/zoom/close buttons; tabs and spaces have hover close buttons; double-click a space to rename. The scrollbar appears while scrolled back and is draggable.

## Settings

Gear icon in the sidebar footer, or `Ctrl+,`. Stored in `~/.config/nebula/settings.json` (font family/size, sidebar width, shell, scrollback, copy-on-select, link clicks, focus-follows-mouse, notifications). Also scriptable: `nebula ctl settings.get`, `nebula ctl settings.set key=fontSize value=12`.

## Automation API

Unix socket `$XDG_RUNTIME_DIR/nebula.sock` (override with `NEBULA_SOCKET`), newline-delimited JSON:

    {"id":1,"method":"pane.send_text","params":{"pane":2,"text":"ls","enter":true}}
    {"id":1,"ok":true,"result":true}

CLI: `nebula ctl <method> key=value ...` (see `nebula ctl --help`), e.g.

    nebula ctl pane.list
    nebula ctl pane.split direction=down
    nebula ctl pane.send_keys keys='["Ctrl+C"]'
    nebula ctl pane.read lines=30
    nebula ctl --watch          # agent.state events

Shells started inside nebula get `NEBULA_PANE` and `NEBULA_SOCKET`. Agent hooks can report exact state (overrides heuristics for `ttl` seconds), e.g. in a Claude Code hook: `nebula ctl pane.report_state state=working`.

## Agent state

Agent = foreground process (claude, opencode, codex, gemini, aider, ...). State is classified from the bottom of the screen with per-agent patterns (permission prompts -> `blocked`, "esc to interrupt" and title spinners -> `working`), plus output activity for unfocused panes, with debounce so spinners don't flicker. `done` = finished while you were elsewhere; cleared when you focus it. Notifications via `notify-send`.

## IME

The terminal item implements Qt's input-method protocol (cursor rectangle, preedit rendering at the cursor, commit), so fcitx5/ibus composition works.
