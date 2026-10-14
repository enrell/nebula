#pragma once
// Looking at other processes on Windows: what the /proc files give nebula on Linux. Plain Win32, no Qt.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace winproc {

// The program a process runs, without its directory: "pwsh.exe".
std::wstring exeName(DWORD pid);
// The arguments the process was started with (argv[0] first), split the way Windows splits them.
std::vector<std::wstring> arguments(DWORD pid);
// The working directory of the process; empty when it cannot be read (another user, a protected process).
std::wstring currentDirectory(DWORD pid);

// Windows has no foreground process group, so "what is the user talking to" is answered from the process tree:
// the most recently started child of `shell`, ignoring the console helpers. 0 when the shell has no such child.
DWORD newestChild(DWORD shell);

// What the user is running in `shell`: newestChild, looking through cmd.exe wrappers (an npm shim such as
// claude.cmd runs node under a cmd.exe, and node is the program that matters). 0 when there is none.
DWORD foreground(DWORD shell);

} // namespace winproc
