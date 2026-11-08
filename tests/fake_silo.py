#!/usr/bin/env python3
"""A stand-in for the silo CLI in the end-to-end tests: no microVM, no clone.

`run` records a session and then plays the agent itself, reading lines until `exit`, so silo stays the pane's
foreground program as it does when the agent runs inside the VM. status/diff/apply/discard/ls/doctor answer in
silo's --json shapes from a state file. State and a log of every call go to $FAKESILO_DIR ($HOME/fakesilo);
a file named doctor-fail there makes `doctor` report a missing /dev/kvm.
"""
import json
import os
import subprocess
import sys
import time

ROOT = os.environ.get("FAKESILO_DIR") or os.path.join(os.environ["HOME"], "fakesilo")
STATE = os.path.join(ROOT, "sessions.json")
os.makedirs(ROOT, exist_ok=True)
with open(os.path.join(ROOT, "calls.log"), "a") as log:
    log.write(json.dumps(sys.argv[1:]) + "\n")


def load():
    try:
        with open(STATE) as f:
            return json.load(f)
    except FileNotFoundError:
        return {}


def save(sessions):
    with open(STATE + ".tmp", "w") as f:
        json.dump(sessions, f)
    os.replace(STATE + ".tmp", STATE)


def emit(value, code=0):
    print(json.dumps(value, indent=2))
    sys.exit(code)


def fail(code, message):
    print(json.dumps({"error": {"code": code, "message": message}}))
    print("error: " + message, file=sys.stderr)
    sys.exit(1)


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True).stdout.strip()


def session(name):
    s = load().get(name)
    if s is None:
        fail("not_found", "session not found: " + name)
    return s


def update(name, **fields):
    sessions = load()
    sessions[name].update(fields, updatedAt=time.time())
    save(sessions)
    return sessions[name]


args = sys.argv[1:]
command, rest = (args[0], args[1:]) if args else ("", [])
flags = [a for a in rest if a.startswith("--")]
names = [a for a in rest if not a.startswith("--")]

if command == "doctor":
    ok = not os.path.exists(os.path.join(ROOT, "doctor-fail"))
    detail = "/dev/kvm is readable and writable" if ok else "/dev/kvm is not readable and writable: no such file or directory"
    emit({"ok": ok, "checks": [{"name": "kvm", "ok": ok, "detail": detail}]}, 0 if ok else 1)

if command == "run":
    end = rest.index("--")
    name = rest[rest.index("--name") + 1]
    sessions = load()
    if name in sessions and sessions[name]["status"] not in ("applied", "discarded"):
        print("error: session name already exists: " + name, file=sys.stderr)
        sys.exit(1)
    sessions[name] = {"schemaVersion": 1, "name": name, "projectRoot": git("rev-parse", "--show-toplevel"),
                      "projectBranch": git("symbolic-ref", "--short", "HEAD"), "baseCommit": git("rev-parse", "HEAD"),
                      "agent": rest[end + 1:], "status": "running", "updatedAt": time.time()}
    save(sessions)
    print("fake silo: running " + " ".join(rest[end + 1:]), flush=True)
    for line in sys.stdin:
        if line.strip() == "exit":
            break
        print("agent got: " + line.strip(), flush=True)
    update(name, status="ready_to_apply", agentExitCode=0)
    print("silo: session %s is ready to apply (agent exit 0)" % name, file=sys.stderr, flush=True)
    sys.exit(0)

if command == "status":
    emit(session(names[0]))

if command == "diff":
    s = session(names[0])
    if s["status"] in ("applied", "discarded"):
        fail("closed", "session is %s and no longer has a workspace" % s["status"])
    patch = "" if "--stat-only" in flags else "diff --git a/agent.txt b/agent.txt\n+agent\n"
    emit({"session": s, "hostSafe": True, "changed": True, "commits": "", "stat": " agent.txt | 1 +", "patch": patch})

if command == "apply":
    s = session(names[0])
    if s["status"] != "ready_to_apply":
        fail("not_ready", "session must be ready_to_apply, found " + s["status"])
    emit(update(names[0], status="applied", appliedCommit="f" * 40))

if command == "discard":
    s = session(names[0])
    if s["status"] == "running":
        fail("running", "cannot discard a running session")
    if "--force" not in flags:
        fail("unapplied_changes", "session has unapplied changes; inspect them or use --force")
    emit(update(names[0], status="discarded"))

if command == "ls":
    emit(sorted(load().values(), key=lambda s: -s["updatedAt"]))

print("fake silo: unknown command " + command, file=sys.stderr)
sys.exit(2)
