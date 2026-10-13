// Exercises the Windows layer without Qt: a program on a pseudo console (output, resize, exit code, kill),
// looking at other processes (name, arguments, working directory, the newest child), typing into a real shell,
// and the Credential Manager. Windows only; run by ctest.
//
// Wine has no usable pseudo console (children keep Wine's own stdout, console input does not arrive), so
// WIN_PROBE_LENIENT=1 only requires there that a program starts, runs and exits with its code; the rest of the
// console checks run on real Windows (CI).
//
// The probe starts copies of itself as the program under test:
//   win_probe --exit N      prints a line and exits with code N
//   win_probe --sleep N     sleeps N seconds
//   win_probe --shell       runs "--sleep 30" under a cmd.exe, as an npm shim does (shell -> cmd.exe -> program)
#include "win/conpty.h"
#include "win/winproc.h"
#include "win/winsecret.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); std::fflush(stdout); } } while (0)

static std::string narrow(const std::wstring &w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? char(c) : '?');
    return s;
}

static std::wstring lowerSlashes(std::wstring s) {
    for (auto &c : s) c = c == L'/' ? L'\\' : wchar_t(std::towlower(c));
    while (s.size() > 3 && s.back() == L'\\') s.pop_back();
    return s;
}

static std::wstring selfPath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    return std::wstring(buf, n);
}

// ---- the program under test

static int childMain(int argc, char **argv) {
    const std::string mode = argv[1];
    if (mode == "--exit") {
        std::printf("bye-%s\n", argc > 2 ? argv[2] : "0");
        std::fflush(stdout);
        return argc > 2 ? std::atoi(argv[2]) : 0;
    }
    if (mode == "--sleep") {
        std::printf("sleeping\n");
        std::fflush(stdout);
        Sleep(DWORD((argc > 2 ? std::atoi(argv[2]) : 1) * 1000));
        return 0;
    }
    if (mode == "--shell") {
        std::wstring cmd = L"cmd.exe /c \"\"" + selfPath() + L"\" --sleep 30\"";
        STARTUPINFOW si = {};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return 3;
        std::printf("shell-ready\n");
        std::fflush(stdout);
        WaitForSingleObject(pi.hProcess, 40000);
        return 0;
    }
    return 2;
}

// ---- the test

// A program on a pseudo console with a thread reading what it prints.
struct Run {
    conpty::Session *s = nullptr;
    std::mutex m;
    std::string out;
    std::thread reader;

    bool start(const std::wstring &cmd, const std::wstring &cwd) {
        std::wstring err;
        s = conpty::start(cmd, cwd, L"", 24, 80, &err);
        CHECK(s != nullptr, "start '%s': %s", narrow(cmd).c_str(), narrow(err).c_str());
        if (!s) return false;
        reader = std::thread([this] {
            char buf[4096];
            while (size_t n = conpty::read(s, buf, sizeof buf)) {
                std::lock_guard<std::mutex> g(m);
                out.append(buf, n);
            }
        });
        return true;
    }
    bool seen(const char *needle, int ms) {
        for (int waited = 0; waited < ms; waited += 50) {
            {
                std::lock_guard<std::mutex> g(m);
                if (out.find(needle) != std::string::npos) return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }
    bool send(const char *text) { return conpty::write(s, text, std::strlen(text)); }
    bool exited(int ms) { return WaitForSingleObject(conpty::process(s), DWORD(ms)) == WAIT_OBJECT_0; }
    // the teardown order conpty.h documents: the reader keeps draining while the console closes
    void finish(bool lenient) {
        if (!s) return;
        conpty::kill(s);
        conpty::closeConsole(s);
        conpty::closeInput(s);
        if (lenient) reader.detach();   // Wine may leave the read blocked for good
        else reader.join();
        if (!lenient) conpty::release(s);
        s = nullptr;
    }
};

int main(int argc, char **argv) {
    if (argc > 1 && std::strncmp(argv[1], "--", 2) == 0) return childMain(argc, argv);
    const bool lenient = std::getenv("WIN_PROBE_LENIENT") != nullptr;
    const std::wstring self = L"\"" + selfPath() + L"\"";
    wchar_t tmpBuf[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpBuf);
    const std::wstring tmp = tmpBuf;

    // 1. a program on a console: its output, its exit code
    {
        Run r;
        if (r.start(self + L" --exit 7", tmp)) {
            if (!lenient) CHECK(r.seen("bye-7", 15000), "output of --exit not seen");
            CHECK(r.exited(15000), "program did not exit");
            DWORD code = 0;
            CHECK(conpty::exitCode(r.s, &code) && code == 7, "exit code %lu", static_cast<unsigned long>(code));
            CHECK(conpty::pid(r.s) != 0, "pid");
            r.finish(lenient);
        }
    }

    // 2. a long-running program on a console: resize and kill
    {
        Run r;
        if (r.start(self + L" --sleep 30", tmp)) {
            if (!lenient) CHECK(r.seen("sleeping", 15000), "output of --sleep not seen");
            conpty::resize(r.s, 30, 100);   // must not disturb it
            DWORD code = 0;
            CHECK(!conpty::exitCode(r.s, &code), "program ended early");
            conpty::kill(r.s);
            CHECK(r.exited(15000), "kill did not end the program");
            r.finish(lenient);
        }
    }

    // 3. looking at processes: a "shell" (started without any console) and the program it runs
    {
        std::wstring cmd = self + L" --shell";
        STARTUPINFOW si = {};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi = {};
        const bool ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, tmp.c_str(), &si, &pi) != 0;
        CHECK(ok, "CreateProcess shell");
        if (ok) {
            DWORD wrapper = 0, child = 0;
            for (int i = 0; i < 150 && !child; ++i) {
                wrapper = winproc::newestChild(pi.dwProcessId);
                child = winproc::foreground(pi.dwProcessId);
                if (!child || child == wrapper) { child = 0; std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
            }
            CHECK(wrapper != 0, "no child found");
            CHECK(child != 0 && child != wrapper, "foreground did not look through cmd.exe");
            if (wrapper) {
                const std::string wname = narrow(winproc::exeName(wrapper));
                CHECK(wname == "cmd.exe" || wname == "CMD.EXE", "newest child is %s", wname.c_str());
            }
            if (child) {
                const std::string name = narrow(winproc::exeName(child));
                std::printf("child %lu: %s\n", static_cast<unsigned long>(child), name.c_str());
                CHECK(name == "win_probe.exe", "child is %s", name.c_str());
                const auto args = winproc::arguments(child);
                CHECK(args.size() == 3 && narrow(args[1]) == "--sleep" && narrow(args[2]) == "30", "arguments: %zu", args.size());
                const std::wstring cwd = winproc::currentDirectory(child);
                std::printf("cwd: %s\n", narrow(cwd).c_str());
                CHECK(lowerSlashes(cwd) == lowerSlashes(tmp), "cwd '%s' != '%s'", narrow(cwd).c_str(), narrow(tmp).c_str());
                CHECK(winproc::newestChild(child) == 0, "a program with no children has a child");
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, child);
                if (h) { TerminateProcess(h, 0); CloseHandle(h); }
            }
            CHECK(winproc::newestChild(0xFFFFFFF0u) == 0, "newestChild of nothing");
            CHECK(winproc::foreground(0xFFFFFFF0u) == 0, "foreground of nothing");
            CHECK(winproc::exeName(0xFFFFFFF0u).empty(), "exeName of nothing");
            CHECK(winproc::arguments(0xFFFFFFF0u).empty(), "arguments of nothing");
            WaitForSingleObject(pi.hProcess, 15000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }

    // 4. typing into a real shell
    if (lenient) {
        std::printf("SKIP typing into cmd.exe (WIN_PROBE_LENIENT)\n");
    } else {
        Run r;
        if (r.start(L"cmd.exe /Q", tmp)) {
            CHECK(r.send("echo nebula-%OS%\r"), "write");
            CHECK(r.seen("nebula-Windows_NT", 20000), "echo output not seen");
            CHECK(r.send("exit 5\r"), "write exit");
            CHECK(r.exited(15000), "shell did not exit");
            DWORD code = 0;
            CHECK(conpty::exitCode(r.s, &code) && code == 5, "exit code %lu", static_cast<unsigned long>(code));
            r.finish(false);
        }
    }

    // 5. secrets
    {
        const std::wstring target = L"nebula-probe/test";
        std::string got;
        CHECK(winsecret::set(target, "s3cret-\xc3\xa9"), "secret set");
        CHECK(winsecret::get(target, &got) && got == "s3cret-\xc3\xa9", "secret get '%s'", got.c_str());
        CHECK(winsecret::remove(target), "secret remove");
        CHECK(!winsecret::get(target, &got), "secret still there");
    }

    std::printf(failures ? "win_probe: %d failure(s)\n" : "win_probe: ok\n", failures);
    std::fflush(stdout);
    return failures ? 1 : 0;
}
