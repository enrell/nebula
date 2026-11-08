#include "isolation.h"
#include <algorithm>

namespace Isolation {

namespace {

bool letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool alnum(char c) { return letter(c) || (c >= '0' && c <= '9'); }

bool startsWith(const std::string &s, const std::string &prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

bool variableName(const std::string &n) {
    if (n.empty() || !(letter(n[0]) || n[0] == '_')) return false;
    return std::all_of(n.begin(), n.end(), [](char c) { return alnum(c) || c == '_'; });
}

} // namespace

const char *name(Kind k) {
    switch (k) {
    case Kind::Worktree: return "worktree";
    case Kind::Silo: return "silo";
    case Kind::None: break;
    }
    return "none";
}

bool available(const Probe &p) { return p.onLinux && p.cli && p.ready; }

std::string unavailableReason(const Probe &p) {
    if (!p.onLinux) return "silo runs only on Linux with KVM";
    if (!p.cli) return "the silo CLI was not found (install it, or point NEBULA_SILO at it)";
    if (!p.ready) return "silo is not set up: " + (p.problem.empty() ? std::string("`silo doctor` failed") : p.problem);
    return {};
}

bool validSetting(const std::string &setting) { return setting == "worktree" || setting == "auto" || setting == "silo"; }

Choice choose(const std::string &requested, bool worktree, const std::string &setting, const Probe &probe) {
    std::string want = requested;
    if (want.empty()) {
        if (!worktree) return {Kind::None, {}, {}};
        want = validSetting(setting) ? setting : "worktree";   // the setting only upgrades launches that asked for a worktree
    }
    if (want == "none") return {Kind::None, {}, {}};
    if (want == "worktree") return {Kind::Worktree, {}, {}};
    if (want == "silo") {
        if (available(probe)) return {Kind::Silo, {}, {}};
        return {Kind::None, unavailableReason(probe), {}};
    }
    if (want == "auto") {
        if (available(probe)) return {Kind::Silo, {}, {}};
        return {Kind::Worktree, {}, "a plain worktree, because " + unavailableReason(probe)};
    }
    return {Kind::None, "isolation must be none, worktree, silo or auto, not '" + requested + "'", {}};
}

std::string sessionName(const std::string &branch) {
    std::string out;
    for (char c : branch) {
        if (out.empty() && !alnum(c)) continue;   // silo names start with a letter or digit
        out += (alnum(c) || c == '.' || c == '_' || c == '-') ? c : '-';
        if (out.size() == 64) break;
    }
    return out.empty() ? "nebula" : out;
}

std::vector<std::string> forwardedNames(const std::vector<std::string> &env) {
    std::vector<std::string> out;
    for (const std::string &e : env) {
        const std::string n = e.substr(0, e.find('='));
        if (!variableName(n) || startsWith(n, "NEBULA_") || startsWith(n, "AINEBULA_")) continue;
        if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    }
    return out;
}

std::vector<std::string> defaultDomains(const std::string &agent) {
    // the API each CLI talks to with an API key; logins (subscriptions) live in the host's home and do not reach the VM
    if (agent == "claude") return {"api.anthropic.com"};
    if (agent == "codex") return {"api.openai.com"};
    if (agent == "gemini") return {"generativelanguage.googleapis.com"};
    return {};   // opencode, aider, custom agents: the provider depends on their configuration
}

std::vector<std::string> runArguments(const RunSpec &spec) {
    std::vector<std::string> a{"run", "--name", spec.session};
    if (!spec.domains.empty()) {
        a.insert(a.end(), {"--network", "proxy"});
        for (const std::string &d : spec.domains) a.insert(a.end(), {"--allow-domain", d});
    }
    for (const std::string &n : spec.env) a.insert(a.end(), {"--env", n});
    a.push_back("--");
    return a;
}

Phase phase(const std::string &s) {
    if (s == "created") return Phase::Starting;
    if (s == "running") return Phase::Running;
    if (s == "ready_to_apply") return Phase::Review;
    if (s == "failed") return Phase::Failed;
    if (s == "applied" || s == "discarded") return Phase::Closed;
    return Phase::Unknown;
}

const char *name(Phase p) {
    switch (p) {
    case Phase::Starting: return "starting";
    case Phase::Running: return "running";
    case Phase::Review: return "review";
    case Phase::Failed: return "failed";
    case Phase::Closed: return "closed";
    case Phase::Unknown: break;
    }
    return "unknown";
}

std::string hint(const std::string &code) {
    if (code == "not_found") return "no such silo session; `silo ls` lists them";
    if (code == "dirty_host") return "the worktree has uncommitted changes: commit or stash them there, then apply again";
    if (code == "diverged")
        return "the worktree's branch moved since the session started, and silo applies only as a fast-forward: "
               "discard the session, or bring the changes over by hand from the diff";
    if (code == "no_changes") return "the agent changed nothing: discard the session";
    if (code == "unsafe_workspace")
        return "the agent added .gitattributes, .gitmodules or a submodule, which silo refuses to import: review the diff, then discard";
    if (code == "not_ready" || code == "running")
        return "the agent is still running inside the VM: quit it in its pane first (a failed session can only be discarded)";
    if (code == "unapplied_changes")
        return "the session has changes that were never applied: review the diff, then discard with force=true to drop them";
    if (code == "closed") return "the session was already applied or discarded";
    if (code == "detached_head" || code == "submodules") return "silo needs a checked-out branch and a repository without submodules";
    return {};
}

} // namespace Isolation
