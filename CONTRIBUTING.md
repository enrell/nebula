# Contributing to nebula

Thanks for helping. This guide covers building nebula, where things live, and above all what makes a pull request
easy to review and merge.

## Build and run

You need Qt >= 6.5 (Quick, WebEngine, WebChannel), libvterm >= 0.3, CMake and Ninja; `node` only if you touch
`renderer/`. [`just`](https://github.com/casey/just) wraps the usual loops:

| Recipe | What |
|---|---|
| `just build` / `just run` / `just install` | release build / build and run / install |
| `just dev` | debug build, isolated instance (own socket and state); QML reloads from disk on save |
| `just watch` | rebuild and restart the dev instance when C++ changes |
| `just test` | C++ unit tests (including the view checker running in Qt's JS engine) |
| `just e2e`, `just e2e-persist`, `just e2e-agents`, `just e2e-views` | end-to-end tests against a throw-away instance |
| `just renderer` | rebuild and test the views renderer (`renderer/dist` is committed) |
| `just screenshots` / `just media` | regenerate the screenshots (headless) / the README GIFs and view images (Xvfb, Mesa, ffmpeg, xdotool) |
| `just ctl <args>` / `just dctl <args>` / `just events` | talk to the running / dev instance, stream agent events |
| `just pkg` / `just release-local` | Arch package from the checkout / every release artifact in docker containers |
| `just lint` / `just fmt` | static checks / formatting |

Every test and screenshot script runs headless in a throw-away `HOME` and never touches your real session.

## Where things live

| Path | What |
|---|---|
| `src/terminalsession.*`, `src/terminalview.*` | terminal emulation (libvterm) and drawing |
| `src/host.*`, `src/hostclient.*` | the pty host daemon that keeps shells alive across GUI restarts |
| `src/workspace.*` | spaces, tabs, the split layout, keybindings, the modal view stack |
| `src/apiserver.*`, `src/cli.cpp`, `src/tools.cpp`, `src/mcp.cpp` | automation API, `nebula ctl`, MCP tools for agents |
| `src/operator.*`, `src/nativeagents.*`, `src/acp.*` | the built-in operator agent |
| `src/views/` | view panes: checker host, file sandbox, `nebula-view://` scheme |
| `renderer/` | view components: schemas and checks (`src/components`), page renderer (`src/page`), tests |
| `qml/` | the UI |
| `tests/` | unit, end-to-end, QML sanity and media scripts |

## What makes a good pull request

A reviewer should understand **why** the change exists, **what it changes for people using and developing
nebula**, and **how you know it works**, without running it first. The pull request template asks for exactly this.

### 1. One purpose

A PR does one thing: a fix, a feature, a refactor. Mixed PRs are hard to review and hard to revert. If you found
something unrelated on the way, open a separate PR (or an issue).

### 2. Reasoning: what and why

Start with the problem, not the diff:

- **What was wrong or missing**, and who noticed it (a user report, a failing test, your own use).
- **Why this approach**: the options you considered and why you picked this one. "Moved X into Y because both
  the checker and the page need it" is worth more than a list of changed files.
- **What it deliberately does not do**, and follow-ups you expect.

### 3. Developer and user experience

Describe how the change feels to the people who meet it:

- **Users**: what they see or do differently. New keybindings, settings, defaults, messages.
- **Agents**: new or changed MCP tools, `nebula ctl` methods, view components; show the call and its output.
- **Developers**: new commands, files, dependencies, build steps, and anything a packager has to know.

Before/after snippets work well:

```text
before: nebula ctl view.show file=r.md     → opens next to the pane, squeezing it
after:  nebula ctl view.show file=r.md     → opens as a modal; `where=right` still docks it
```

### 4. Show it: images for anything visual

**Every change a user can see needs images in the PR description**: a screenshot, and a GIF when it moves or
has several steps (open, dock, hide). Show before and after when you change existing UI.

- Produce them with the scripts, not from your own session: `just screenshots` (headless) or `just media`
  (real WebGL, needs Xvfb, Mesa and ffmpeg). Both use a throw-away `HOME` with stand-in agents, so no personal
  data, paths or accounts leak into the repository.
- If you change what the README shows, commit the regenerated images in the same PR.
- Keep GIFs short (under ~20 s, under ~3 MB) and at a readable size (about 1000 px wide).

### 5. Tests: what ran, what did not

- Add or update tests with the change: C++ unit tests (`tests/test_*.cpp`), `renderer/test` cases for view
  components, the `tests/e2e_*.sh` scripts for API and UI flows.
- List what you ran and the result, and say plainly what you could **not** test and why (another distro, a GPU,
  Wayland only). Reviewers can then test exactly that.
- CI runs the unit tests, every end-to-end script, the QML sanity check (fails on any QML warning) and checks that
  `renderer/dist` matches its sources.

### 6. Risks

Call out anything that needs a careful look: compatibility (settings, `session.json`, API), packaging and new
dependencies, security (anything that reads files, runs commands or talks to the network; see
[SECURITY.md](SECURITY.md)), performance.

## Code

- Match the surrounding code: naming, idiom, comment density. Comments explain *why*, not what.
- Keep one source of truth. For example, view components are defined once in `renderer/src/components`, and the
  checker, the page and the MCP tool descriptions all derive from that list.
- No new warnings: the build uses `-Wall -Wextra`, and the QML sanity check fails on QML warnings.

### Adding a view component

1. `renderer/src/components/<name>.js`: `summary`, JSON Schema, a valid `example`, and `resolve()` for the checks
   a schema cannot express (files, columns, ranges), reporting with `ctx.error(path, message, hint)` so every
   problem has a line number and a way to fix it.
2. Register it in `renderer/src/components/index.js` and add a renderer in `renderer/src/page/components.js`
   that draws only from the resolved props and uses the theme's CSS variables.
3. Add `renderer/test/cases/*.md` cases (valid and invalid), run `just renderer` and commit `renderer/dist`.
4. Document it in the README's Views table, and include a screenshot in the PR.

## Commits

Short imperative subject with the area first (`Views: …`, `Terminal: …`), then a body that says why. Rebase or
squash fixups before review so the history reads as a story.

## Security issues

Do not open public issues for vulnerabilities; see [SECURITY.md](SECURITY.md).
