#pragma once
#include <QString>
#include <QStringList>

// What differs between Linux and Windows for the rest of the code: which shell panes run and how to quote for it,
// how a program name becomes a file, and how to raise a desktop notification.
namespace Platform {

#ifdef Q_OS_WIN
inline constexpr bool isWindows = true;
#else
inline constexpr bool isWindows = false;
#endif

enum class ShellKind { Posix, PowerShell, Cmd };

// The shell for a pane when the settings name none: $SHELL (/bin/sh without it); on Windows PowerShell 7 if
// installed, else Windows PowerShell, else cmd.exe.
QString defaultShell();

// What the pty host runs for `shell` (empty = the default). Unix: the shell's path. Windows: a complete command
// line. A PowerShell without arguments also gets a small prompt hook that reports its working directory, because
// PowerShell's `cd` does not change the process's directory, which is the only place nebula could read it from.
QString shellCommandLine(const QString &shell);

ShellKind shellKind(const QString &shell);
// the kind of the shell panes run: the setting, else the default
ShellKind paneShellKind();

// One argument for the pane's shell: 'single quotes' for POSIX shells and PowerShell, "double quotes" for cmd.exe.
QString quote(const QString &arg);
QString quote(const QString &arg, ShellKind kind);

// A bare program name resolved to its file, so QProcess can start things the system finds by other rules (on
// Windows: claude -> C:/.../claude.cmd). Names with a path, and names that are not found, come back unchanged.
QString program(const QString &name);

// A desktop notification (notify-send, a toast on Windows)
void notify(const QString &title, const QString &body);

} // namespace Platform
