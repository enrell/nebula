// The view checker as nebula runs it: renderer/dist/core.js in a QJSEngine (ViewEngine) with the real file
// sandbox (ViewFiles). Runs the same cases as renderer/test/core.test.js, so node and V4 must agree.
//   test_views <renderer/test dir>
#include "views/viewengine.h"
#include "views/viewfiles.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } } while (0)

static QByteArray read(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static void runCase(const QString &dir, const QString &name) {
    const QString src = QString::fromUtf8(read(dir + "/cases/" + name));
    const QJsonObject expect = QJsonDocument::fromJson(read(dir + "/cases/" + QString(name).replace(".md", ".expect.json"))).object();
    const QJsonObject r = ViewEngine::instance().check(src, dir + "/fixtures/proj");
    const QByteArray n = name.toUtf8();
    CHECK(!r.contains("fatal"), "%s: %s", n.constData(), qPrintable(r["fatal"].toString()));
    for (const char *sev : {"error", "warning"}) {
        QJsonArray got;
        for (const QJsonValue &d : r["diagnostics"].toArray())
            if (d["severity"].toString() == sev) got << d;
        const QJsonArray want = expect[QString(sev) + "s"].toArray();
        CHECK(got.size() == want.size(), "%s: %d %ss, expected %d: %s", n.constData(), int(got.size()), sev, int(want.size()),
              QJsonDocument(got).toJson(QJsonDocument::Compact).constData());
        for (int i = 0; i < qMin(got.size(), want.size()); ++i) {
            const QJsonObject d = got[i].toObject();
            CHECK(d["line"].toInt() == want[i][0].toInt(), "%s: %s %d on line %d, expected %d", n.constData(), sev, i, d["line"].toInt(), want[i][0].toInt());
            const QString text = d["message"].toString() + ' ' + d["hint"].toString();
            CHECK(text.contains(want[i][1].toString()), "%s: \"%s\" lacks \"%s\"", n.constData(), qPrintable(text), qPrintable(want[i][1].toString()));
        }
    }
    QJsonArray ok;
    for (const QJsonValue &b : r["blocks"].toArray()) ok << b["ok"];
    CHECK(ok == expect["ok"].toArray(), "%s: block ok flags %s", n.constData(), QJsonDocument(ok).toJson(QJsonDocument::Compact).constData());
}

static void sandbox() {
    QTemporaryDir tmp;
    const QString base = tmp.path() + "/project", outside = tmp.path() + "/secret.txt";
    QDir().mkpath(base + "/sub");
    QFile(outside).open(QIODevice::WriteOnly);
    { QFile f(base + "/sub/a.txt"); f.open(QIODevice::WriteOnly); f.write("hello"); }
    QFile::link(outside, base + "/leak.txt");
    QFile::link(tmp.path(), base + "/up");

    CHECK(ViewFiles::resolve(base, "sub/a.txt").error.isEmpty(), "plain relative path");
    CHECK(ViewFiles::resolve(base, "sub/../sub/a.txt").error.isEmpty(), "`..` that stays inside");
    CHECK(ViewFiles::resolve(base, "../secret.txt").error.contains("outside"), "`..` escape");
    CHECK(ViewFiles::resolve(base, outside).error.contains("relative"), "absolute path");
    CHECK(ViewFiles::resolve(base, "~/x").error.contains("relative"), "home path");
    CHECK(ViewFiles::resolve(base, "leak.txt").error.contains("links outside"), "symlink to a file outside");
    CHECK(ViewFiles::resolve(base, "up/secret.txt").error.contains("links outside"), "symlinked directory outside");
    CHECK(ViewFiles::resolve(base, "sub").error.contains("not a file"), "directory");
    CHECK(ViewFiles::resolve(base, "nope.txt").error.contains("file not found"), "missing file");
    CHECK(ViewFiles::readTextJson(base, "sub/a.txt").contains("\"text\":\"hello\""), "read text");
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString dir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("renderer/test");
    const QStringList cases = QDir(dir + "/cases").entryList({"*.md"}, QDir::Files, QDir::Name);
    CHECK(!cases.isEmpty(), "no cases in %s/cases", qPrintable(dir));
    for (const QString &c : cases) runCase(dir, c);
    CHECK(ViewEngine::instance().components().size() == 11, "component catalogue");
    CHECK(ViewEngine::instance().describe("table")["example"].toString().startsWith("```nebula:table"), "describe table");
    CHECK(ViewEngine::instance().describe("nope").isEmpty(), "describe unknown");
    sandbox();
    if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    std::printf("views: %d cases ok\n", int(cases.size()));
    return 0;
}
