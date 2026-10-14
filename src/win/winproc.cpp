#include "winproc.h"
#include <tlhelp32.h>
#include <winternl.h>
#include <shellapi.h>
#include <algorithm>
#include <cwctype>

static_assert(sizeof(void *) == 8, "winproc reads the 64-bit process environment block");

namespace winproc {

namespace {

// The parts of the process parameters we need, at their 64-bit offsets (they have been stable since Windows 7):
// the public winternl.h only declares the first few fields.
struct UnicodeString64 {
    USHORT length;
    USHORT maximumLength;
    ULONG pad;
    PWSTR buffer;
};
struct ProcessParameters64 {
    BYTE head[0x38];
    UnicodeString64 currentDirectory;   // 0x38
    BYTE handleAndDllPath[0x18];        // CURDIR.Handle + DllPath
    UnicodeString64 imagePathName;      // 0x60
    UnicodeString64 commandLine;        // 0x70
};
static_assert(offsetof(ProcessParameters64, currentDirectory) == 0x38, "layout");
static_assert(offsetof(ProcessParameters64, imagePathName) == 0x60, "layout");
static_assert(offsetof(ProcessParameters64, commandLine) == 0x70, "layout");

struct Handle {
    HANDLE h;
    explicit Handle(HANDLE v) : h(v) {}
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    explicit operator bool() const { return h && h != INVALID_HANDLE_VALUE; }
};

using NtQueryInformationProcessFn = LONG(NTAPI *)(HANDLE, ULONG, PVOID, ULONG, PULONG);

// Reads one UNICODE_STRING out of the target's process parameters.
bool readParameter(DWORD pid, size_t offset, std::wstring *out) {
    static const auto query = reinterpret_cast<NtQueryInformationProcessFn>(
        reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
    if (!query) return false;
    Handle proc(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid));
    if (!proc) {
        // PROCESS_QUERY_INFORMATION is what reading another process's memory needs on older systems
        proc.h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!proc) return false;
    }
    PROCESS_BASIC_INFORMATION pbi = {};
    ULONG got = 0;
    if (query(proc.h, 0 /* ProcessBasicInformation */, &pbi, sizeof pbi, &got) != 0 || !pbi.PebBaseAddress) return false;
    // PEB.ProcessParameters is the pointer at offset 0x20
    PVOID params = nullptr;
    SIZE_T n = 0;
    if (!ReadProcessMemory(proc.h, reinterpret_cast<const BYTE *>(pbi.PebBaseAddress) + 0x20, &params, sizeof params, &n) || !params) return false;
    UnicodeString64 str = {};
    if (!ReadProcessMemory(proc.h, reinterpret_cast<const BYTE *>(params) + offset, &str, sizeof str, &n) || !str.buffer) return false;
    if (str.length == 0) { out->clear(); return true; }
    if (str.length > 64 * 1024) return false;   // not a plausible command line or directory: do not trust it
    std::wstring s(str.length / sizeof(wchar_t), L'\0');
    if (!ReadProcessMemory(proc.h, str.buffer, s.data(), str.length, &n)) return false;
    s.resize(n / sizeof(wchar_t));
    *out = std::move(s);
    return true;
}

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
    return s;
}

} // namespace

std::wstring exeName(DWORD pid) {
    Handle proc(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (proc) {
        wchar_t buf[MAX_PATH * 4];
        DWORD len = DWORD(std::size(buf));
        if (QueryFullProcessImageNameW(proc.h, 0, buf, &len)) {
            std::wstring path(buf, len);
            const size_t slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? path : path.substr(slash + 1);
        }
    }
    // protected processes: fall back to the name the snapshot lists
    Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snap) {
        PROCESSENTRY32W e = {};
        e.dwSize = sizeof e;
        for (BOOL ok = Process32FirstW(snap.h, &e); ok; ok = Process32NextW(snap.h, &e))
            if (e.th32ProcessID == pid) return e.szExeFile;
    }
    return {};
}

std::vector<std::wstring> arguments(DWORD pid) {
    std::wstring cmd;
    std::vector<std::wstring> args;
    if (!readParameter(pid, offsetof(ProcessParameters64, commandLine), &cmd) || cmd.empty()) return args;
    int argc = 0;
    if (LPWSTR *argv = CommandLineToArgvW(cmd.c_str(), &argc)) {
        for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
        LocalFree(argv);
    }
    return args;
}

std::wstring currentDirectory(DWORD pid) {
    std::wstring dir;
    if (!readParameter(pid, offsetof(ProcessParameters64, currentDirectory), &dir)) return {};
    // "C:\Users\me\" -> "C:\Users\me"; a drive root keeps its slash
    while (dir.size() > 3 && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    return dir;
}

DWORD newestChild(DWORD shell) {
    Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snap) return 0;
    // conhost.exe/OpenConsole.exe belong to the console, not to what runs in it
    static const wchar_t *ignored[] = {L"conhost.exe", L"openconsole.exe", L"wslhost.exe"};
    DWORD best = 0;
    ULONGLONG bestTime = 0;
    PROCESSENTRY32W e = {};
    e.dwSize = sizeof e;
    for (BOOL ok = Process32FirstW(snap.h, &e); ok; ok = Process32NextW(snap.h, &e)) {
        if (e.th32ParentProcessID != shell || e.th32ProcessID == shell) continue;
        const std::wstring name = lower(e.szExeFile);
        if (std::any_of(std::begin(ignored), std::end(ignored), [&](const wchar_t *i) { return name == i; })) continue;
        Handle p(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.th32ProcessID));
        FILETIME created, exited, kernel, user;
        ULONGLONG t = 0;
        if (p && GetProcessTimes(p.h, &created, &exited, &kernel, &user)) t = (ULONGLONG(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        // a recycled parent id could make an older process look like a child: it must not predate its parent
        if (t >= bestTime) { bestTime = t; best = e.th32ProcessID; }
    }
    return best;
}

DWORD foreground(DWORD shell) {
    DWORD current = newestChild(shell);
    for (int depth = 0; current && depth < 4; ++depth) {
        if (lower(exeName(current)) != L"cmd.exe") break;
        const DWORD inner = newestChild(current);
        if (!inner) break;
        current = inner;
    }
    return current;
}

} // namespace winproc
