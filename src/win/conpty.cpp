#include "conpty.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace conpty {

namespace {

// The pseudo console API arrived in Windows 10 1809. It is looked up at run time, so nebula starts on older
// systems (and builds against older SDKs) and says what is missing instead of failing to load.
using PseudoConsole = void *;
constexpr DWORD_PTR kAttributePseudoConsole = 0x00020016;   // PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE

struct Api {
    HRESULT(WINAPI *create)(COORD, HANDLE, HANDLE, DWORD, PseudoConsole *) = nullptr;
    HRESULT(WINAPI *resize)(PseudoConsole, COORD) = nullptr;
    void(WINAPI *close)(PseudoConsole) = nullptr;
    bool ok() const { return create && resize && close; }
};

const Api &api() {
    static const Api a = [] {
        Api r;
        HMODULE k = GetModuleHandleW(L"kernel32.dll");
        auto load = [k](const char *name) { return reinterpret_cast<void *>(GetProcAddress(k, name)); };
        r.create = reinterpret_cast<decltype(r.create)>(load("CreatePseudoConsole"));
        r.resize = reinterpret_cast<decltype(r.resize)>(load("ResizePseudoConsole"));
        r.close = reinterpret_cast<decltype(r.close)>(load("ClosePseudoConsole"));
        return r;
    }();
    return a;
}

} // namespace

struct Session {
    PseudoConsole console = nullptr;
    HANDLE inWrite = INVALID_HANDLE_VALUE;    // our end of the pipe that feeds the console's input
    HANDLE outRead = INVALID_HANDLE_VALUE;    // our end of the pipe the console prints to
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    DWORD pid = 0;
};

static std::wstring lastError(const wchar_t *what) {
    const DWORD e = GetLastError();
    return std::wstring(what) + L" failed (" + std::to_wstring(e) + L")";
}

Session *start(const std::wstring &cmdline, const std::wstring &cwd, const std::wstring &env, int rows, int cols, std::wstring *error) {
    auto fail = [&](const wchar_t *what) -> Session * {
        if (error) *error = lastError(what);
        return nullptr;
    };
    if (!api().ok()) {
        if (error) *error = L"this version of Windows has no pseudo console (Windows 10 1809 or newer is needed)";
        return nullptr;
    }
    // 64 KiB pipes: a paste is taken in one go instead of blocking on the default 4 KiB
    HANDLE inRead = nullptr, outWrite = nullptr;
    auto s = std::make_unique<Session>();
    if (!CreatePipe(&inRead, &s->inWrite, nullptr, 65536)) return fail(L"CreatePipe(input)");
    if (!CreatePipe(&s->outRead, &outWrite, nullptr, 65536)) {
        CloseHandle(inRead);
        CloseHandle(s->inWrite);
        return fail(L"CreatePipe(output)");
    }
    const COORD size = {SHORT(std::max(cols, 1)), SHORT(std::max(rows, 1))};
    const HRESULT hr = api().create(size, inRead, outWrite, 0, &s->console);
    // the console keeps its own references to these two ends
    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (FAILED(hr)) {
        CloseHandle(s->inWrite);
        CloseHandle(s->outRead);
        if (error) *error = L"CreatePseudoConsole failed (" + std::to_wstring(static_cast<unsigned long>(hr)) + L")";
        return nullptr;
    }

    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    auto *attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    bool ok = InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
              UpdateProcThreadAttribute(attrs, 0, kAttributePseudoConsole, s->console, sizeof(PseudoConsole), nullptr, nullptr);
    PROCESS_INFORMATION pi = {};
    if (ok) {
        STARTUPINFOEXW si = {};
        si.StartupInfo.cb = sizeof si;
        si.lpAttributeList = attrs;
        std::wstring cmd = cmdline;   // CreateProcessW may modify it
        cmd.push_back(L'\0');
        DWORD flags = EXTENDED_STARTUPINFO_PRESENT;
        if (!env.empty()) flags |= CREATE_UNICODE_ENVIRONMENT;
        ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, flags, env.empty() ? nullptr : const_cast<wchar_t *>(env.c_str()),
                            cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi) != 0;
    }
    const std::wstring why = ok ? std::wstring() : lastError(L"CreateProcess");
    DeleteProcThreadAttributeList(attrs);
    if (!ok) {
        api().close(s->console);
        CloseHandle(s->inWrite);
        CloseHandle(s->outRead);
        if (error) *error = why;
        return nullptr;
    }
    s->process = pi.hProcess;
    s->thread = pi.hThread;
    s->pid = pi.dwProcessId;
    return s.release();
}

DWORD pid(const Session *s) { return s->pid; }
HANDLE process(const Session *s) { return s->process; }

bool exitCode(const Session *s, DWORD *code) {
    DWORD c = 0;
    if (!GetExitCodeProcess(s->process, &c) || c == STILL_ACTIVE) return false;
    if (code) *code = c;
    return true;
}

size_t read(Session *s, char *buf, size_t cap) {
    DWORD n = 0;
    if (!ReadFile(s->outRead, buf, DWORD(std::min<size_t>(cap, 1u << 20)), &n, nullptr)) return 0;
    return n;
}

bool write(Session *s, const char *data, size_t len) {
    while (len) {
        DWORD n = 0;
        if (!WriteFile(s->inWrite, data, DWORD(std::min<size_t>(len, 1u << 20)), &n, nullptr) || n == 0) return false;
        data += n;
        len -= n;
    }
    return true;
}

void resize(Session *s, int rows, int cols) {
    const COORD size = {SHORT(std::max(cols, 1)), SHORT(std::max(rows, 1))};
    api().resize(s->console, size);
}

void kill(Session *s) { TerminateProcess(s->process, 1); }

void closeConsole(Session *s) { api().close(s->console); }

void closeInput(Session *s) {
    if (s->inWrite != INVALID_HANDLE_VALUE) CloseHandle(s->inWrite);
    s->inWrite = INVALID_HANDLE_VALUE;
}

void release(Session *s) {
    closeInput(s);
    CloseHandle(s->outRead);
    CloseHandle(s->thread);
    CloseHandle(s->process);
    delete s;
}

} // namespace conpty
