#pragma once
// A child process on a Windows pseudo console (ConPTY). Plain Win32 and the standard library, no Qt, so it
// can be built and exercised on its own (tests/conpty_probe.cpp).
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
#include <cstddef>
#include <string>

namespace conpty {

struct Session;

// Starts `cmdline` (a complete Windows command line) in `cwd` on a new pseudo console of rows x cols.
// `env` is a UTF-16 environment block (NAME=value strings, each NUL terminated, the block ending in an extra
// NUL), or empty to inherit the current environment. Returns nullptr and fills `error` when it cannot start.
Session *start(const std::wstring &cmdline, const std::wstring &cwd, const std::wstring &env, int rows, int cols, std::wstring *error);

DWORD pid(const Session *s);
HANDLE process(const Session *s);                    // signalled when the process has ended
bool exitCode(const Session *s, DWORD *code);        // false while it still runs

// What the console prints, as bytes of VT text. Blocks; returns 0 once the console has closed.
size_t read(Session *s, char *buf, size_t cap);
// Input for the program, as VT text (what a terminal sends). Blocks until the console has taken it.
bool write(Session *s, const char *data, size_t len);
void resize(Session *s, int rows, int cols);

// Ends the process. Whatever else is attached to the console ends when the console is closed.
void kill(Session *s);

// Tearing a session down takes three steps, in this order. Keep reading (and discard the output) until read()
// returns 0, because closing the console can wait for it to finish printing:
//   closeConsole(s)  closes the pseudo console; read() then returns 0 and the programs on it end. Can block, so
//                    call it from a thread that is not the UI thread.
//   closeInput(s)    makes a blocked write() fail
//   release(s)       frees the session; only once the reader and the writer have left
void closeConsole(Session *s);
void closeInput(Session *s);
void release(Session *s);

} // namespace conpty
