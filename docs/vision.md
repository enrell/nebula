Where nebula is going, and the rules that get it there. Back to the [README](../README.md).

## Thesis

**nebula is a native workbench for people who build and research: an agent that remembers your projects, and work you can see instead of read.**

Agent tools today are chat windows over a hidden shell, mostly built on Electron and designed for people who will never use them. The people who do use them are developers and scientists, who work on their own machines, care about privacy and know their tools. nebula is built for them.

## Principles

1. **It remembers, so you don't have to.** Nobody should explain a project to an agent twice. nebula keeps memory of every project you touch and gives it to every agent, its own and the ones you run in panes.
2. **Show, don't scroll.** Agent work arrives as views: plans, diffs, timelines, tables, plots, molecules. Text is the exception, not the medium.
3. **Native, local, private.** Qt Quick, not Electron. Memory and data stay on your machine. Local models are first class.
4. **Made for experts.** Dense, keyboard first, the terminal at the center. No onboarding theater.

## Memory

Memory belongs to nebula, not to an agent. nebula already sees every pane, every agent, every working directory, command, exit code, branch and view; no single agent sees that much. Memory is built from the workspace, not only from one chat.

- **Every agent benefits.** Memory is served through the MCP server, so Claude Code or Codex opened inside nebula starts with the project's briefing.
- **Plain files** under `~/.local/share/nebula/memory/`, three layers:
  - **events**: an append-only JSONL log per project, collected from panes, agents and git;
  - **pages**: distilled Markdown (what the project is, decisions, conventions, current work, known issues), readable and editable in any editor;
  - **briefing**: a short summary built from the pages and recent events, handed to an agent when a session starts.
- **Projects are keyed by git root**, plus one user page for who you are and how you work. Cross-project context comes from reading other projects' pages.
- **Consolidation is automatic**: when an agent finishes, what changed is distilled into the pages with the model you choose, local or not.
- **Boring storage**: files and BM25 search. No vector database, no SQLite. Embeddings may come later as an option.
- **Memory is visible**: pages, the timeline and "what the agent knows about this project" are views. Hidden memory is memory you can't trust.

## Views

Views are what sets nebula apart, and they become the default output:

- every agent turn produces a view: the plan as a live checklist, edits as a diff review, runs as tables and charts;
- one **activity view** across all agents: what each did, what is running, what is blocked, what is next;
- keep growing the science components (molecules, proteins, genomes, math with sliders): researchers are a first-class audience.

## Architecture

- **C++ is the body**: the native UI, terminals, the host, views and the socket API. Stable; changed rarely.
- **Go is the mind**: `nebula-agent`, a service started by the host so it outlives the window. Two jobs: **memory** (collect events, distill pages, build briefings) and **the agent** (an [ACP](https://agentclientprotocol.com) server driven by the existing operator panel, LLM providers, tools that call nebula through the socket API).
- **MCP** is how every other agent reaches memory and views.
- Subscription CLIs (Claude Code, Codex) remain first class as agents in panes and as workers the nebula agent delegates to.

### The rule that does not bend: pure Go

`nebula-agent` and every one of its dependencies are pure Go.

- Always built with `CGO_ENABLED=0`.
- No cgo, C, C++, Fortran or prebuilt object (`.syso`) files in any non-standard package. Go assembly inside the standard library is accepted.
- Enforced, not trusted: `agent/check-pure-go.sh` (run by ctest, so by CI) fails when this lists anything. Packages are listed with cgo on, because with it off Go silently drops files that import "C" and optional C code in a dependency would pass unseen:
  ```sh
  CGO_ENABLED=1 go list -deps -f '{{if not .Standard}}{{if or .CgoFiles .CFiles .CXXFiles .FFiles .SysoFiles}}{{.ImportPath}}{{end}}{{end}}' ./...
  ```
- One exception, for tests only: `go test -race`, whose race detector needs cgo. It never touches the shipped binary, which is still built with `CGO_ENABLED=0` and checked.
- Standard library first; every dependency has to justify itself. No SQLite (neither `mattn/go-sqlite3`, which needs cgo, nor `modernc.org/sqlite`, which is translated C). Secrets stay in the C++ side's keyring and reach the agent as environment variables.

## Order

1. Faster C++ builds, so the core stops slowing iteration.
2. The `nebula-agent` skeleton, wired into `just`, CMake and CI with the pure-Go check.
3. Memory v1: event log, project pages, briefing over MCP. Claude and Codex get better immediately.
4. Memory as views: pages and the activity timeline.
5. The nebula agent loop on top of memory.
