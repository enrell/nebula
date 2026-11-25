#include "restore.h"
#include <cstdio>

static int failures = 0;
#define CHECK_EQ(a, b) do { const QString _a = (a), _b = (b); if (_a != _b) { std::printf("FAIL %s:%d  [%s] != [%s]\n", __FILE__, __LINE__, qPrintable(_a), qPrintable(_b)); ++failures; } } while (0)
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

using namespace Restore;

int main() {
    // agents come back in their exact conversation, with the flags they were started with, without the old prompt
    CHECK_EQ(plan("claude", {"claude", "--dangerously-skip-permissions"}, "a1b2").command, "claude --dangerously-skip-permissions --resume a1b2");
    CHECK(plan("claude", {"claude"}, "a1b2").autorun);
    CHECK_EQ(plan("claude", {"claude", "--model", "opus", "fix the bug"}, "").command, "claude --model opus --continue");
    CHECK_EQ(plan("claude", {"claude", "--resume", "old", "-c"}, "new").command, "claude --resume new");
    CHECK_EQ(plan("claude", {"claude", "-r", "abc"}, "").command, "claude --resume abc");
    CHECK_EQ(plan("claude", {"node", "/usr/lib/node_modules/@anthropic-ai/claude-code/cli.js", "--verbose"}, "s").command, "claude --verbose --resume s");
    CHECK_EQ(plan("claude", {"claude", "--permission-mode=plan"}, "s").command, "claude --permission-mode=plan --resume s");
    CHECK_EQ(plan("opencode", {"opencode", "-m", "anthropic/x", "."}, "ses_1").command, "opencode -m anthropic/x --session ses_1");
    CHECK_EQ(plan("opencode", {"opencode", "--continue"}, "").command, "opencode --continue");
    CHECK_EQ(plan("codex", {"codex", "--yolo", "write tests"}, "019a-thread").command, "codex resume 019a-thread --yolo");
    CHECK_EQ(plan("codex", {"codex", "-m", "gpt-5"}, "").command, "codex resume --last -m gpt-5");
    CHECK_EQ(plan("codex", {"codex", "resume", "t-9"}, "").command, "codex resume t-9");

    CHECK_EQ(sessionFromArgs("claude", {"claude", "--session-id=x-1"}), "x-1");
    CHECK_EQ(sessionFromArgs("opencode", {"opencode", "-s", "ses_9"}), "ses_9");
    CHECK_EQ(sessionFromArgs("codex", {"codex", "resume", "--last"}), "");

    CHECK_EQ(sessionFromPayload(R"({"session_id":"5f1e-ab","hook_event_name":"Stop"})"), "5f1e-ab");
    CHECK_EQ(sessionFromPayload(R"({"type":"agent-turn-complete","thread-id":"0199-abc"})"), "0199-abc");
    CHECK_EQ(sessionFromPayload(R"({"sessionID":"ses_x"})"), "ses_x");
    CHECK_EQ(sessionFromPayload("not json"), "");
    CHECK_EQ(sessionFromPayload(R"x({"session_id":"$(rm -rf ~)"})x"), "");   // never something a shell would expand

    // other programs: interactive ones run again, anything else is typed at the prompt and waits for Enter
    CHECK_EQ(plan("", {"nvim", "src/main.cpp"}, "").command, "nvim src/main.cpp");
    CHECK(plan("", {"nvim", "src/main.cpp"}, "").autorun);
    CHECK(plan("", {"btop"}, "").autorun);
    CHECK(plan("", {"tail", "-f", "/var/log/x"}, "").autorun);
    CHECK(!plan("", {"tail", "/var/log/x"}, "").autorun);
    CHECK(plan("", {"python3"}, "").autorun);
    CHECK(!plan("", {"python3", "train.py"}, "").autorun);
    CHECK(!plan("", {"make", "deploy"}, "").autorun);
    CHECK_EQ(plan("", {"git", "commit", "-m", "it's done"}, "").command, "git commit -m 'it'\\''s done'");
    CHECK_EQ(plan("", {}, "").command, "");

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("restore: ok\n");
    return 0;
}
