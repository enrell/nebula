# Automation

nebula is scriptable from anything: a Unix-socket JSON API, the `nebula ctl` CLI and an MCP server for agents. Back to the [README](../README.md).

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

The MCP server (`nebula mcp`) exposes the same API as tools; see [agents](agents.md#mcp-server) and [views](views.md).
