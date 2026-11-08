# Isolation: agents in silo microVMs

Design note: how an agent launched by nebula (by you or by the operator) can run inside a silo session instead of a plain git worktree, and what is built so far. Back to the [README](../README.md) or [Agents](agents.md).

## What you gain

A worktree keeps parallel agents from colliding, but each agent still runs as you: it can read your home, your keys and your other repositories, and reach the whole network. In a silo session the agent runs in a microVM (KVM + Cloud Hypervisor) on a private clone of the repository:

- It sees only that clone at `/workspace`. Your checkout, your home, `~/.ssh` and the other projects are not there.
- No network by default. nebula asks for an allowlisted proxy to the agent's provider API only.
- Its changes reach the branch only when you apply them: one fast-forward commit. silo refuses when the worktree is dirty or the branch moved.
- CPU and memory are capped per agent.
- An approved permission prompt can only damage the private clone. That makes it reasonable to let the operator answer prompts, or to run an agent with its skip-permissions flag.

The workflow stays the same: one branch per agent, which you merge as usual.

## The pieces

```text
launch_agent / nebula ctl agent.launch   isolation=auto|silo|worktree|none, domains=[...]
  Launcher::launch
    Isolation::choose(option, worktree flag, setting, Silo::probe())   src/isolation.*  (plain C++, tests/test_isolation.cpp)
    git worktree add -b <branch> <repo>-<branch>                        worktree and silo
    silo only: types into the pane
      silo run --name <branch> [--network proxy --allow-domain H...] [--env NAME...] -- <the agent's usual command>

session_diff / session_apply / session_discard   (MCP and operator tools)
nebula ctl isolation.status|diff|apply|discard pane=ID | session=NAME
  Launcher::siloSession -> Silo::status|diff|apply|discard            src/silo.*  ->  silo <command> --json
```

There are three backends: **none** (the pane's directory), **worktree** and **silo**. silo always starts from a fresh worktree. `silo apply` fast-forwards the branch checked out where the session started. Started from your main checkout, two parallel agents would share one branch, and the second apply would always fail with `diverged`. Started from per-agent worktrees, each session lands on its own branch, and your checkout never moves. silo's `TestParallelSessionsFromLinkedWorktreesApplyToTheirOwnBranches` covers this.

The agent runs **in the pane**, not hidden in the background. `silo run` forwards a real pty over vsock, so the agent's TUI shows in the pane. nebula's screen-based state detection keeps working, and `send_text` and `send_keys` reach the agent. This needs no new channel.

### Which backend a launch gets

| Launch | Setting `isolation` | silo usable here | Result |
|---|---|---|---|
| no `worktree`, no `isolation` | any | any | none (unchanged) |
| `worktree=true` | `worktree` (default) | any | worktree (unchanged) |
| `worktree=true` | `auto` | yes / no | silo / worktree, with a `note` saying why |
| `worktree=true` | `silo` | yes / no | silo / the launch fails |
| `isolation=auto` | | yes / no | silo / worktree, with a `note` |
| `isolation=silo` | | yes / no | silo / the launch fails, and no worktree is created |
| `isolation=worktree` or `none` | | | as asked |

"Usable here" means three things: nebula runs on Linux, `silo` is found (`$NEBULA_SILO`, else `PATH`), and `silo doctor --json` passes. Doctor checks the platform, read/write access to `/dev/kvm`, the config, the backend assets, `git` and `unshare`. Windows never probes. Change the setting with `nebula ctl settings.set key=isolation value=auto`. The default stays `worktree` because subscription logins do not reach the VM (see below).

## How nebula talks to silo

nebula uses the CLI with `--json`. silo is Go and nebula is C++/Qt. silo keeps its state as durable JSON files guarded by file locks, which every CLI invocation shares. The CLI is the contract silo already keeps for people. The `Silo::` namespace is the only place that knows the commands, so a socket or daemon could replace it later. The `--json` flags, `silo status` and `silo doctor` were added to silo for this:

| Call | Success (exit 0) |
|---|---|
| `silo doctor --json` | `{ok, checks: [{name, ok, detail}]}`; exits 1 when a check fails |
| `silo status --json NAME` | the session record: `name, status, projectRoot, projectBranch, baseCommit, agentExitCode, lastError, appliedCommit, ...` |
| `silo diff --json [--stat-only] NAME` | `{session, hostSafe, hostError?, changed, commits, stat, patch}` |
| `silo apply --json NAME` / `silo discard --json [--force] NAME` | the record afterwards |
| `silo ls --json` | every record, newest first |

A failure prints `{"error": {"code", "message"}}`. nebula turns the code into a next step (`Isolation::hint`), for example `dirty_host` becomes "commit or stash them there, then apply again". The API error carries both, so the operator can relay it.

nebula stores nothing new. Each pane remembers its session in memory. After the window restarts, nebula finds the session again through `silo ls --json`: it is the newest open session whose `projectRoot` is the pane's worktree. silo's records are the source of truth.

## Lifecycle

| silo status | The pane | nebula agent state | `phase` | What you can do |
|---|---|---|---|---|
| `created`, `running` | the agent's TUI, through silo | working / blocked / idle / done, read from the screen as today | `starting`, `running` | watch, answer prompts, `session_diff` (a live view of the clone) |
| `ready_to_apply` | silo printed "session X is ready to apply"; back at the shell | no agent | `review` | diff, apply, discard |
| `failed` | silo's error | no agent | `failed` | diff, discard |
| `applied`, `discarded` | | | `closed` | nothing: after an apply, the worktree's branch has the commit |

nebula's `done` means "finished its turn while you looked elsewhere". Interactive agents stay open after that, so the session stays `running`. To apply, quit the agent (`/exit`, Ctrl+D) and silo moves to `ready_to_apply`. `diff` works at any time.

- **Detection.** The host sees `silo run ... -- claude ...`. `AgentDetect::identify` reads the command after `--`, so the pane is still a `claude` pane with the same state patterns, and `pane.get` adds `isolation: "silo"`.
- **Hooks.** The status hooks (`nebula ctl pane.report_state`) do not work inside the VM. There is no nebula binary or socket there, and `NEBULA_PANE` is not forwarded. They stay silent, and the state comes from the screen.
- **Restore.** After a reboot, a silo pane gets no resume command. `claude --continue` would start the agent on the host, outside the VM.

## Permission prompts

The agent asks in its TUI, inside the VM. silo puts the host terminal in raw mode and forwards the pty over vsock, so the prompt appears in the pane. nebula's patterns mark the pane **blocked**, `Ctrl+Shift+A` jumps to it, and the operator's `send_keys` answers through the same pty. None of this is new.

What changes is what is at stake. An approved command runs against the private clone, with no host files and no network beyond the allowlist. `session_apply` and `session_discard` are marked destructive, and the operator is told to call them only when you ask. They pass through the operator's normal approval: with "auto-approve" on, nothing stops a careless operator. An apply only fast-forwards the agent's own worktree branch, though, never your checkout.

## Credentials, network, the guest

- **Credentials.** The pane's profile variables (`ANTHROPIC_API_KEY`, `ANTHROPIC_BASE_URL`, ...) are forwarded by name with `--env NAME`. Their values never appear on the command line or in silo's state. nebula's own `NEBULA_*` variables stay out.
- **Network.** Default allowlists: `claude` gets `api.anthropic.com`, `codex` gets `api.openai.com`, `gemini` gets `generativelanguage.googleapis.com`. Others (opencode, aider, custom agents) depend on their configuration and need `domains=[...]`. Without domains there is no network.
- **Subscriptions.** Claude Code and ChatGPT logins live in your home directory, which the VM cannot see. Inside silo, agents need an API-key profile.
- **The agent binary.** The guest is Alpine (musl) with only what silo's bootstrap bakes in, so the agent CLI must be baked into the image (`SILO_EXTRA_FILES` / `SILO_EXTRA_APK`; see silo's README). Otherwise silo explains in the pane that it could not start the agent.

## Failure modes

| What | What happens | What you see |
|---|---|---|
| Windows, no `silo`, no KVM, not bootstrapped | the probe fails | `auto`: a worktree and a `note`. `silo`: the launch fails with doctor's reason |
| the agent is not in the guest image | `silo run` fails | silo's hint in the pane; the session is `failed`, so discard it |
| the branch name is reused and its session is still open | `silo run` refuses | silo suggests another name or a discard |
| apply while the agent runs | `not_ready` | "quit it in its pane first" |
| the worktree is dirty, or its branch moved | `dirty_host`, `diverged` | commit or stash it; or discard the session and redo the change by hand |
| the agent added `.gitattributes`, `.gitmodules` or a submodule | `unsafe_workspace` | review, then discard |
| discard with unapplied changes | `unapplied_changes` | "discard with force=true" |
| pane closed, host process killed or reboot while running | the VM dies and the session stays `running`; silo's MVP refuses to discard a running session | diff still works. A silo follow-up should detect dead sessions |
| silo hangs | calls time out after 30 to 120 s, and the window waits that long | follow-up: asynchronous calls |
| silo without `--json` | "printed no JSON" | upgrade silo |

## Status of this slice

Built: the backend choice, the `isolation` setting, `silo run` in the pane, agent detection through silo, `isolation.*` in the API and `nebula ctl`, the `session_*` tools and operator guidance, `tests/test_isolation.cpp`, and an end-to-end test against a stand-in silo (`tests/fake_silo.py`). Real microVM runs are tested in silo's own integration suite, not here.

Still to do:

- **UI.** For now the operator shows diffs with `view_show`, and there is `nebula ctl isolation.diff`. The plan:
  - a *review* mark on panes whose session is `ready_to_apply`;
  - a review modal with the diff and Apply / Discard buttons;
  - an Isolation choice in the launcher dialog and in Settings > Agents & AI.
- **Responsiveness.** Asynchronous silo calls, with deferred API replies like `llm.ask`.
- **Launch options.** Per-launch `--cpus` / `--memory`, and allowlist hosts taken from the profile's base URL.
- **Clean-up.** Removing the worktree after apply or discard (plain worktrees are not removed today either).
