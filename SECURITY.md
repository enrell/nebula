# Security Policy

## Supported versions

nebula is pre-1.0. Only the latest release receives security fixes.

| Version | Supported |
|---|---|
| latest release | yes |
| older releases | no |

## Reporting a vulnerability

Please **do not open a public issue** for security problems.

Report privately through GitHub: go to the
[Security tab](https://github.com/enrell/nebula/security/advisories/new) of
this repository and choose *Report a vulnerability*.

Include the affected version (`nebula --version`), your distro, steps to
reproduce and the impact you expect. You can expect an acknowledgement within
a few days. Fixes are released as a new version and credited in the release
notes unless you prefer otherwise.

## Security model

nebula gives programs (and people) control of your terminals by design. What
is and is not a vulnerability follows from that:

- **Automation socket** (`$XDG_RUNTIME_DIR/nebula.sock`): anyone who can
  connect can read and type into every pane. It lives in your per-user runtime
  directory; a way for another local user to reach it is a vulnerability.
- **MCP server** (`nebula mcp`) and **operator**: expose the same terminal
  control to agents. Only install the MCP server in agents you trust. Operator
  tool calls are auto-approved by default and can be switched to per-call
  approval in the chat header. Pane text is passed to agents as untrusted
  data, but prompt injection through terminal output is an inherent risk.
- **API keys** are stored in the system keyring (Secret Service) and only fall
  back to `~/.config/nebula/secrets.json` (mode 0600). They are never written
  to `settings.json` or `session.json`, and `list_profiles` never returns them.
- **Hooks** edit agent config files (`~/.claude/settings.json`, ...) by
  merging, with `*.nebula-bak` backups, and are no-ops outside nebula.
- **Views** render agent-written documents in Qt WebEngine with an off-the-record profile that blocks all network
  requests and serves only the files a document references, resolved inside the document's directory (no absolute
  paths, `..` or symlink escapes). Raw HTML in Markdown is not rendered and pages cannot navigate. A way for a view
  document to read other files, reach the network or run script outside the page is a vulnerability.
- **Summaries** are off by default because they send terminal output to your
  configured provider.
- **Installer** (`install.sh`) verifies a checksum and installs into your home
  directory without root.

Out of scope: an attacker who already runs code as your user, and behaviour of
third-party agent CLIs you launch inside nebula.
