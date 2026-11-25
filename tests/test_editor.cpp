#include "editor/highlighter.h"
#include "editor/textbuffer.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

static int failures = 0;
#define CHECK_EQ(a, b) do { const QString _a = (a), _b = (b); if (_a != _b) { std::printf("FAIL %s:%d  [%s] != [%s]\n", __FILE__, __LINE__, qPrintable(_a), qPrintable(_b)); ++failures; } } while (0)
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

using P = TextBuffer::Pos;

// "kind:text" for every span, to compare colouring in one line
static QString spans(const Highlighter &h, const QString &line, int state = 0, int *end = nullptr) {
    static const char *names[] = {"plain", "kw", "type", "str", "com", "num", "fn", "pre", "head", "em", "link", "code", "const", "tag"};
    QVector<Highlighter::Span> out;
    const int e = h.highlight(line, state, &out);
    if (end) *end = e;
    QStringList parts;
    for (const auto &s : out) if (s.kind != Highlighter::Plain) parts << QString("%1:%2").arg(names[s.kind], line.mid(s.start, s.length));
    return parts.join(' ');
}

int main() {
    // ---- buffer: load/save round-trips the file's shape
    {
        TextBuffer b;
        CHECK(b.loadBytes("a\r\nb\r\n"));
        CHECK(b.crlf() && b.finalNewline() && b.lineCount() == 3);
        CHECK_EQ(QString::fromUtf8(b.bytes()), "a\r\nb\r\n");
        CHECK(b.loadBytes("\xEF\xBB\xBFx\ny"));
        CHECK(b.bom() && !b.finalNewline());
        CHECK(b.bytes() == QByteArray("\xEF\xBB\xBFx\ny"));
        CHECK(b.loadBytes(""));
        CHECK(b.lineCount() == 1 && b.bytes().isEmpty());
        QString err;
        CHECK(!b.loadBytes(QByteArray("a\0b", 3), &err));
        CHECK_EQ(err, "binary file");
        CHECK(!b.loadBytes("\xff\xfe bad", &err));
        CHECK_EQ(err, "not UTF-8 text");
    }
    // ---- edits, undo merging, redo
    {
        TextBuffer b;
        b.loadBytes("hello\nworld");
        int first = -1, removed = -1, added = -1;
        b.onChanged = [&](int f, int r, int a) { first = f; removed = r; added = a; };
        P e = b.insert({0, 5}, ",\nbig");
        CHECK(e == (P{1, 3}));
        CHECK(first == 0 && removed == 1 && added == 2);
        CHECK_EQ(b.text(), "hello,\nbig\nworld");
        CHECK(b.modified());
        TextBuffer::Range sel;
        CHECK(b.undo(&sel));
        CHECK_EQ(b.text(), "hello\nworld");
        CHECK(!b.modified());
        CHECK(b.redo(&sel));
        CHECK_EQ(b.text(), "hello,\nbig\nworld");
        b.remove({{0, 3}, {2, 2}});
        CHECK_EQ(b.text(), "helrld");
        CHECK(first == 0 && removed == 3 && added == 1);
        CHECK_EQ(b.text({{0, 1}, {0, 4}}), "elr");

        // typing merges into one step; a space starts the next word
        TextBuffer t;
        t.loadBytes("");
        for (QChar c : QString("ab cd")) t.replace({t.end(), t.end()}, QString(c));   // the editor types through replace
        CHECK_EQ(t.text(), "ab cd");
        t.undo(nullptr);
        CHECK_EQ(t.text(), "ab");
        t.undo(nullptr);
        CHECK_EQ(t.text(), "");
        CHECK(!t.canUndo());
        // a group undoes as one
        t.redo(nullptr); t.redo(nullptr);
        t.beginGroup();
        t.insert({0, 0}, "    ");
        t.insert({0, 0}, "x");
        t.endGroup();
        CHECK_EQ(t.text(), "x    ab cd");
        t.undo(nullptr);
        CHECK_EQ(t.text(), "ab cd");
        // a new edit after undo drops the redo branch, and the saved point stays honest
        t.markSaved();
        t.insert({0, 0}, "!");
        CHECK(t.modified());
        t.undo(nullptr);
        CHECK(!t.modified());
        CHECK(t.canRedo());
        t.insert({0, 0}, "?");
        CHECK(!t.canRedo());
    }
    // ---- search and replace
    {
        TextBuffer b;
        b.loadBytes("foo bar\nFoo food\nbar foo");   // no final newline
        TextBuffer::FindOptions o;
        TextBuffer::Range r;
        CHECK(b.find("foo", {0, 1}, o, &r) && r.a == (P{1, 0}));
        o.caseSensitive = true;
        CHECK(b.find("foo", {0, 1}, o, &r) && r.a == (P{1, 4}));
        o.wholeWord = true;
        CHECK(b.find("foo", {0, 1}, o, &r) && r.a == (P{2, 4}));
        CHECK(b.find("foo", {2, 5}, o, &r) && r.a == (P{0, 0}));   // wraps
        o.wrap = false;
        CHECK(!b.find("foo", {2, 5}, o, &r));
        TextBuffer::FindOptions back; back.backward = true;
        CHECK(b.find("bar", {2, 0}, back, &r) && r.a == (P{0, 4}));
        TextBuffer::FindOptions cs; cs.caseSensitive = true;
        CHECK(!b.find("bar\nfoo", {0, 0}, cs, &r));   // "bar\nFoo" only matches without case
        CHECK(b.find("bar\nfoo", {0, 0}, TextBuffer::FindOptions{}, &r) && r.a == (P{0, 4}));
        CHECK(b.find("food\nbar", {0, 0}, TextBuffer::FindOptions{}, &r) && r.a == (P{1, 4}) && r.b == (P{2, 3}));
        CHECK(b.countMatches("foo", TextBuffer::FindOptions{}) == 4);
        TextBuffer::FindOptions rx; rx.regex = true; rx.caseSensitive = true;
        CHECK(b.replaceAll("(\\w+) foo$", "\\1-FOO", rx) == 1);
        CHECK_EQ(b.text(), "foo bar\nFoo food\nbar-FOO");
        CHECK(b.replaceAll("o", "0", TextBuffer::FindOptions{}) == 8);
        b.undo(nullptr);
        CHECK_EQ(b.text(), "foo bar\nFoo food\nbar-FOO");
    }
    // ---- save: atomic, through symlinks, keeps permissions (POSIX: Windows has no symlink by QFile::link nor an
    // execute bit)
#ifndef Q_OS_WIN
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("f.txt");
        QFile f(path); CHECK(f.open(QIODevice::WriteOnly)); f.write("x\n"); f.close();
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QFile::link(path, dir.filePath("link.txt"));
        TextBuffer b;
        CHECK(b.load(dir.filePath("link.txt")));
        b.insert({0, 1}, "y");
        CHECK(b.save(dir.filePath("link.txt")));
        CHECK(QFileInfo(dir.filePath("link.txt")).isSymLink());
        QFile g(path); CHECK(g.open(QIODevice::ReadOnly));
        CHECK_EQ(QString::fromUtf8(g.readAll()), "xy\n");
        CHECK(QFile::permissions(path) & QFileDevice::ExeOwner);
        CHECK(!b.modified());
    }
#endif
    {
        QTemporaryDir dir;
        TextBuffer b;
        b.loadBytes("one\r\ntwo\r\n");
        b.insert({1, 3}, "!");
        CHECK(b.save(dir.filePath("crlf.txt")));
        QFile g(dir.filePath("crlf.txt")); CHECK(g.open(QIODevice::ReadOnly));
        CHECK(g.readAll() == QByteArray("one\r\ntwo!\r\n"));
    }
    // ---- highlighting
    {
        CHECK_EQ(Highlighter::languageFor("src/main.cpp"), "cpp");
        CHECK_EQ(Highlighter::languageFor("notes.neb"), "markdown");
        CHECK_EQ(Highlighter::languageFor("CMakeLists.txt"), "cmake");
        CHECK_EQ(Highlighter::languageFor("run", "#!/usr/bin/env python3"), "python");
        CHECK_EQ(Highlighter::languageFor("LICENSE"), "");

        Highlighter cpp("cpp");
        CHECK_EQ(spans(cpp, "int main() { return 0; } // done"), "type:int fn:main kw:return num:0 com:// done");
        CHECK_EQ(spans(cpp, "#include <QString>"), "pre:#include <QString>");
        CHECK_EQ(spans(cpp, "auto s = \"a \\\" b\"; QString x;"), "type:auto str:\"a \\\" b\" type:QString");
        int st = 0;
        CHECK_EQ(spans(cpp, "x = 1; /* open", 0, &st), "num:1 com:/* open");
        CHECK(st != 0);
        CHECK_EQ(spans(cpp, "still */ y", st, &st), "com:still */");
        CHECK(st == 0);

        Highlighter py("python");
        CHECK_EQ(spans(py, "def f(x): return None  # c"), "kw:def fn:f kw:return const:None com:# c");
        CHECK_EQ(spans(py, "s = \"\"\"doc", 0, &st), "str:\"\"\"doc");
        CHECK_EQ(spans(py, "end\"\"\" + 1", st, &st), "str:end\"\"\" num:1");

        Highlighter rs("rust");
        CHECK_EQ(spans(rs, "fn f<'a>(x: &'a str) -> Option<u8> { println!(\"hi\") }"), "kw:fn fn:f type:str type:Option type:u8 pre:println str:\"hi\"");
        Highlighter sh("shell");
        CHECK_EQ(spans(sh, "echo \"$HOME\" a#b # c"), "type:echo str:\"$HOME\" com:# c");
        Highlighter json("json");
        CHECK_EQ(spans(json, "  \"name\": \"x\", \"n\": 1.5e3, \"ok\": true"), "kw:\"name\" str:\"x\" kw:\"n\" num:1.5e3 kw:\"ok\" const:true");

        Highlighter md("markdown");
        st = md.initialState();
        CHECK_EQ(spans(md, "---", st, &st), "pre:---");
        CHECK_EQ(spans(md, "title: Report", st, &st), "kw:title str: Report");
        CHECK_EQ(spans(md, "---", st, &st), "pre:---");
        CHECK_EQ(spans(md, "# Results", st, &st), "head:# Results");
        CHECK_EQ(spans(md, "Some **bold** and `code` [link](http://x)", st, &st), "em:**bold** code:`code` link:[link] com:(http://x)");
        CHECK_EQ(spans(md, "```nebula:stats", st, &st), "code:```nebula:stats");
        CHECK_EQ(spans(md, "data: results.csv", st, &st), "kw:data code:: results.csv");
        CHECK_EQ(spans(md, "```", st, &st), "code:```");
        CHECK(st == 0);
        CHECK_EQ(spans(md, "---", st, &st), "pre:---");   // a rule, not front matter, after the start
    }
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("editor: ok\n");
    return 0;
}
