#pragma once
#include <string>
#include <vector>

// Where a launched agent works: in the pane's directory, in a fresh git worktree, or inside a silo session started from
// such a worktree (silo runs a command in a private microVM workspace and brings its changes back as one git commit).
// The decisions live here, in plain C++ without Qt like src/win/, so tests/test_isolation.cpp checks them on its own;
// src/silo.cpp runs the silo CLI. See docs/isolation.md.
namespace Isolation {

enum class Kind { None, Worktree, Silo };
const char *name(Kind k);   // "none" | "worktree" | "silo"

// What this machine offers. silo exists only on Linux, and needs its CLI and a working setup (KVM, the guest
// image), which `silo doctor --json` reports.
struct Probe {
    bool onLinux = false;
    bool cli = false;        // the silo program was found
    bool ready = false;      // `silo doctor` passed
    std::string problem;     // why silo cannot be used (from doctor), shown to the user
};
bool available(const Probe &p);
std::string unavailableReason(const Probe &p);

struct Choice {
    Kind kind = Kind::None;
    std::string error;   // the launch must fail: silo was asked for and is not available, or the value is unknown
    std::string note;    // auto fell back to a worktree: why, for the caller
};

// requested: the launch's `isolation` option ("", none, worktree, silo, auto). worktree: its `worktree` flag.
// setting: what a launch that asks for a worktree without naming an isolation gets (worktree, auto or silo).
Choice choose(const std::string &requested, bool worktree, const std::string &setting, const Probe &probe);
bool validSetting(const std::string &setting);

// A silo session name from the worktree's branch (silo accepts [a-zA-Z0-9][a-zA-Z0-9._-]{0,63}).
std::string sessionName(const std::string &branch);

// Names of the variables to forward into the VM from a pane's profile environment ("NAME=value" entries).
// Values never go on the command line: silo reads them from its own environment. nebula's own variables
// (NEBULA_PANE, NEBULA_PROFILE, ...) mean nothing inside the VM, so they stay out.
std::vector<std::string> forwardedNames(const std::vector<std::string> &env);

// Hosts an agent CLI needs to reach its provider through silo's allowlisted proxy; empty when unknown.
std::vector<std::string> defaultDomains(const std::string &agent);

struct RunSpec {
    std::string session;
    std::vector<std::string> domains;   // proxy allowlist; empty = no network
    std::vector<std::string> env;       // variable names to forward
};
// The arguments after the silo program, ending in "--": the agent's own command line follows them.
std::vector<std::string> runArguments(const RunSpec &spec);

// A silo session's status, in terms of what the user can do next.
enum class Phase { Unknown, Starting, Running, Review, Failed, Closed };
Phase phase(const std::string &siloStatus);
const char *name(Phase p);   // "unknown" | "starting" | "running" | "review" | "failed" | "closed"

// The next step for a silo error code (silo --json), for the user and the operator; empty when there is none.
std::string hint(const std::string &code);

} // namespace Isolation
