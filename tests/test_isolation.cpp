// The isolation decisions on their own, without Qt: which backend a launch gets, the silo command line, how silo's
// session states and error codes read for the user.
#include "isolation.h"
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
#define CHECK_EQ(a, b) do { const std::string _a = (a), _b = (b); if (_a != _b) { std::printf("FAIL %s:%d  '%s' != '%s'\n", __FILE__, __LINE__, _a.c_str(), _b.c_str()); ++failures; } } while (0)

using namespace Isolation;
using Strings = std::vector<std::string>;

static std::string join(const Strings &v) {
    std::string out;
    for (const std::string &s : v) out += (out.empty() ? "" : " ") + s;
    return out;
}

int main() {
    Probe ready;
    ready.onLinux = ready.cli = ready.ready = true;
    Probe windows;
    windows.cli = true;
    Probe missing;
    missing.onLinux = true;
    Probe broken;
    broken.onLinux = broken.cli = true;
    broken.problem = "kvm: /dev/kvm is not readable and writable";

    // nothing asked for: unchanged, whatever the setting
    CHECK(choose("", false, "auto", ready).kind == Kind::None);
    CHECK(choose("", false, "silo", ready).kind == Kind::None);
    // worktree=true follows the setting; the default setting keeps today's worktrees even when silo is there
    CHECK(choose("", true, "worktree", ready).kind == Kind::Worktree);
    CHECK(choose("", true, "", ready).kind == Kind::Worktree);
    CHECK(choose("", true, "bogus", ready).kind == Kind::Worktree);
    CHECK(choose("", true, "auto", ready).kind == Kind::Silo);
    CHECK(choose("", true, "silo", ready).kind == Kind::Silo);
    // auto falls back to a worktree and says why; Windows and a missing CLI never fail a launch
    const Choice onWindows = choose("auto", false, "worktree", windows);
    CHECK(onWindows.kind == Kind::Worktree && onWindows.error.empty());
    CHECK(onWindows.note.find("only on Linux") != std::string::npos);
    CHECK(choose("", true, "auto", missing).kind == Kind::Worktree);
    CHECK(choose("", true, "auto", missing).note.find("not found") != std::string::npos);
    CHECK(choose("", true, "auto", broken).note.find("/dev/kvm") != std::string::npos);
    // silo asked for explicitly (option or setting) and unavailable: the launch fails rather than silently not isolating
    CHECK(!choose("silo", false, "worktree", broken).error.empty());
    CHECK(choose("silo", false, "worktree", broken).error.find("/dev/kvm") != std::string::npos);
    CHECK(!choose("", true, "silo", windows).error.empty());
    // the option wins over the setting
    CHECK(choose("worktree", true, "silo", ready).kind == Kind::Worktree);
    CHECK(choose("none", true, "silo", ready).kind == Kind::None);
    CHECK(choose("silo", false, "worktree", ready).kind == Kind::Silo);
    CHECK(choose("vm", false, "worktree", ready).error.find("'vm'") != std::string::npos);
    CHECK(validSetting("auto") && validSetting("worktree") && validSetting("silo") && !validSetting("") && !validSetting("none"));
    CHECK_EQ(name(Kind::Silo), "silo");
    CHECK_EQ(name(Kind::Worktree), "worktree");
    CHECK_EQ(name(Kind::None), "none");
    CHECK_EQ(unavailableReason(ready), "");

    // session names: silo accepts [a-zA-Z0-9][a-zA-Z0-9._-]{0,63}
    CHECK_EQ(sessionName("fix/flaky"), "fix-flaky");
    CHECK_EQ(sessionName("claude-4821"), "claude-4821");
    CHECK_EQ(sessionName("/-feat/x"), "feat-x");
    CHECK_EQ(sessionName("v1.2_rc"), "v1.2_rc");
    CHECK_EQ(sessionName(""), "nebula");
    CHECK_EQ(sessionName("///"), "nebula");
    CHECK_EQ(sessionName(std::string(80, 'a')), std::string(64, 'a'));

    // forwarded variables: names only, valid ones, nebula's own left out, no duplicates
    CHECK_EQ(join(forwardedNames({"ANTHROPIC_API_KEY=sk-x", "NEBULA_PROFILE=work", "AINEBULA_PROFILE=work", "OPENAI_BASE_URL=http://h",
                                  "ANTHROPIC_API_KEY=again", "1BAD=x", "bad-name=x", "=x", "_OK=1"})),
             "ANTHROPIC_API_KEY OPENAI_BASE_URL _OK");
    CHECK(forwardedNames({}).empty());

    // the silo command line; the agent's own command follows the final --
    CHECK_EQ(join(runArguments({"fix-flaky", {}, {}})), "run --name fix-flaky --");
    CHECK_EQ(join(runArguments({"s", {"api.anthropic.com"}, {"ANTHROPIC_API_KEY"}})),
             "run --name s --network proxy --allow-domain api.anthropic.com --env ANTHROPIC_API_KEY --");
    CHECK_EQ(join(runArguments({"s", {"a.example", "b.example"}, {"A", "B"}})),
             "run --name s --network proxy --allow-domain a.example --allow-domain b.example --env A --env B --");
    CHECK_EQ(join(defaultDomains("claude")), "api.anthropic.com");
    CHECK_EQ(join(defaultDomains("codex")), "api.openai.com");
    CHECK(defaultDomains("shell").empty() && defaultDomains("opencode").empty());

    // silo session states as the user's next step
    CHECK(phase("created") == Phase::Starting);
    CHECK(phase("running") == Phase::Running);
    CHECK(phase("ready_to_apply") == Phase::Review);
    CHECK(phase("failed") == Phase::Failed);
    CHECK(phase("applied") == Phase::Closed && phase("discarded") == Phase::Closed);
    CHECK(phase("") == Phase::Unknown && phase("paused") == Phase::Unknown);
    CHECK_EQ(name(Phase::Review), "review");
    CHECK_EQ(name(Phase::Unknown), "unknown");

    // every error code silo documents has advice, except the catch-all
    for (const char *code : {"not_found", "dirty_host", "diverged", "no_changes", "unsafe_workspace", "detached_head", "submodules",
                             "not_ready", "running", "unapplied_changes", "closed"})
        CHECK(!hint(code).empty());
    CHECK(hint("error").empty() && hint("").empty());
    CHECK(hint("unapplied_changes").find("force=true") != std::string::npos);
    CHECK(hint("running").find("quit") != std::string::npos);

    if (failures) return 1;
    std::puts("isolation: all tests passed");
    return 0;
}
