#include "agentdetect.h"
#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK_EQ(a, b) do { const QString _a = (a), _b = (b); if (_a != _b) { std::printf("FAIL %s:%d  %s != %s\n", __FILE__, __LINE__, qPrintable(_a), qPrintable(_b)); ++failures; } } while (0)

using namespace AgentDetect;

int main() {
    CHECK_EQ(identify({"node", "/usr/lib/node_modules/@anthropic-ai/claude-code/cli.js"}, "node"), "claude");
    CHECK_EQ(identify({"/usr/bin/opencode"}, "opencode"), "opencode");
    CHECK_EQ(identify({"fish"}, "fish"), "");
    CHECK_EQ(identify({"btop"}, "btop"), "");

    CHECK_EQ(classify({"claude", "✻ Baking… (13m 40s · esc to interrupt)", "", false, true}), "working");
    CHECK_EQ(classify({"claude", "Do you want to proceed?\n❯ 1. Yes\n  2. No", "", false, true}), "blocked");
    CHECK_EQ(classify({"claude", "> \n? for shortcuts", "✳ Claude Code", false, true}), "idle");
    CHECK_EQ(classify({"claude", "> ", "⠂ Fix bug", false, true}), "working");
    CHECK_EQ(classify({"opencode", "esc interrupt", "", false, true}), "working");
    CHECK_EQ(classify({"opencode", "Permission required\nAllow once  Allow always  Reject", "", false, true}), "blocked");
    CHECK_EQ(classify({"codex", "Working (3s • esc to interrupt)", "", false, true}), "working");
    CHECK_EQ(classify({"codex", "Would you like to run the following command?\n1. Yes, proceed", "", false, true}), "blocked");
    CHECK_EQ(classify({"aider", "Apply changes? (Y)es/(N)o [Yes]:", "", false, true}), "blocked");
    CHECK_EQ(classify({"aider", "Continue? [y/n]", "", false, true}), "blocked");
    CHECK_EQ(classify({"aider", "> ", "", true, false}), "working");
    CHECK_EQ(classify({"aider", "> ", "", true, true}), "idle");
    CHECK_EQ(classify({"aider", "> ", "", false, false}), "idle");
    CHECK_EQ(classify({"claude", "bypass permissions on (shift+tab to cycle)", "", false, true}), "idle");

    if (failures) return 1;
    std::puts("agentdetect: all tests passed");
    return 0;
}
