#include "highlighter.h"
#include <QFileInfo>
#include <QHash>

namespace {

QSet<QString> words(const char *list) {
    QSet<QString> out;
    for (const QString &w : QString::fromLatin1(list).split(' ', Qt::SkipEmptyParts)) out.insert(w);
    return out;
}

const char *kCKeywords = "if else for while do switch case default break continue return goto sizeof typedef struct union enum "
                         "static extern const volatile inline register restrict";
const char *kCTypes = "void char short int long float double signed unsigned bool size_t ssize_t int8_t int16_t int32_t int64_t "
                      "uint8_t uint16_t uint32_t uint64_t intptr_t uintptr_t qint64 quint64 qreal auto";
const char *kCppKeywords = "if else for while do switch case default break continue return goto sizeof typedef struct union enum "
                           "static extern const volatile inline register class public private protected virtual override final "
                           "template typename namespace using new delete this try catch throw noexcept constexpr consteval "
                           "constinit explicit friend mutable operator co_await co_return co_yield static_cast dynamic_cast "
                           "reinterpret_cast const_cast decltype alignof alignas concept requires export import module signals "
                           "slots emit Q_OBJECT Q_PROPERTY Q_INVOKABLE";

} // namespace

QString Highlighter::languageFor(const QString &path, const QString &firstLine) {
    const QFileInfo fi(path);
    const QString name = fi.fileName(), ext = fi.suffix().toLower();
    static const QHash<QString, QString> byName = {
        {"CMakeLists.txt", "cmake"}, {"Makefile", "make"}, {"makefile", "make"}, {"GNUmakefile", "make"}, {"Justfile", "make"},
        {"justfile", "make"}, {"Dockerfile", "shell"}, {"PKGBUILD", "shell"}, {".bashrc", "shell"}, {".zshrc", "shell"},
        {".profile", "shell"}, {".gitignore", "ini"}, {".editorconfig", "ini"}, {"Cargo.lock", "toml"}};
    if (byName.contains(name)) return byName[name];
    static const QHash<QString, QString> byExt = {
        {"c", "c"}, {"h", "cpp"}, {"cc", "cpp"}, {"cpp", "cpp"}, {"cxx", "cpp"}, {"hpp", "cpp"}, {"hh", "cpp"}, {"hxx", "cpp"},
        {"ino", "cpp"}, {"m", "c"}, {"mm", "cpp"}, {"rs", "rust"}, {"py", "python"}, {"pyw", "python"}, {"pyi", "python"},
        {"js", "javascript"}, {"mjs", "javascript"}, {"cjs", "javascript"}, {"jsx", "javascript"}, {"ts", "typescript"},
        {"tsx", "typescript"}, {"mts", "typescript"}, {"qml", "qml"}, {"go", "go"}, {"java", "java"}, {"kt", "java"},
        {"kts", "java"}, {"cs", "java"}, {"sh", "shell"}, {"bash", "shell"}, {"zsh", "shell"}, {"fish", "shell"}, {"ps1", "shell"},
        {"json", "json"}, {"jsonc", "json"}, {"json5", "json"}, {"yaml", "yaml"}, {"yml", "yaml"}, {"toml", "toml"},
        {"md", "markdown"}, {"markdown", "markdown"}, {"neb", "markdown"}, {"cmake", "cmake"}, {"lua", "lua"}, {"sql", "sql"},
        {"css", "css"}, {"scss", "css"}, {"less", "css"}, {"html", "html"}, {"htm", "html"}, {"xml", "html"}, {"svg", "html"},
        {"ui", "html"}, {"qrc", "html"}, {"vue", "html"}, {"mk", "make"}, {"ini", "ini"}, {"conf", "ini"}, {"cfg", "ini"},
        {"service", "ini"}, {"desktop", "ini"}, {"env", "shell"}, {"glsl", "c"}, {"frag", "c"}, {"vert", "c"}, {"zig", "rust"}};
    if (byExt.contains(ext)) return byExt[ext];
    if (firstLine.startsWith("#!")) {
        if (firstLine.contains("python")) return "python";
        if (firstLine.contains("node") || firstLine.contains("bun") || firstLine.contains("deno")) return "javascript";
        if (firstLine.contains("lua")) return "lua";
        if (firstLine.contains("sh")) return "shell";
    }
    return {};
}

QString Highlighter::displayName(const QString &language) {
    static const QHash<QString, QString> names = {
        {"c", "C"}, {"cpp", "C++"}, {"rust", "Rust"}, {"python", "Python"}, {"javascript", "JavaScript"}, {"typescript", "TypeScript"},
        {"qml", "QML"}, {"go", "Go"}, {"java", "Java"}, {"shell", "Shell"}, {"json", "JSON"}, {"yaml", "YAML"}, {"toml", "TOML"},
        {"markdown", "Markdown"}, {"cmake", "CMake"}, {"lua", "Lua"}, {"sql", "SQL"}, {"css", "CSS"}, {"html", "HTML/XML"},
        {"make", "Make"}, {"ini", "INI"}};
    return names.value(language, "Plain text");
}

Highlighter::Highlighter(const QString &language) : m_lang(language) {
    const QString &l = language;
    if (l == "c" || l == "cpp") {
        m_lineComments = {"//"}; m_blockStart = "/*"; m_blockEnd = "*/"; m_hashPreproc = true; m_singleQuoteChars = true;
        m_keywords = words(l == "c" ? kCKeywords : kCppKeywords);
        m_types = words(kCTypes);
        m_constants = words("true false nullptr NULL TRUE FALSE");
        m_capsAreTypes = l == "cpp";
    } else if (l == "rust") {
        m_lineComments = {"//"}; m_blockStart = "/*"; m_blockEnd = "*/"; m_capsAreTypes = true;
        m_keywords = words("as async await break const continue crate dyn else enum extern fn for if impl in let loop match mod move "
                           "mut pub ref return self Self static struct super trait type unsafe use where while yield macro_rules "
                           "comptime pub fn const var");
        m_types = words("i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str String Vec Option Result Box");
        m_constants = words("true false None Some Ok Err");
    } else if (l == "python") {
        m_lineComments = {"#"}; m_triple = true;
        m_keywords = words("and as assert async await break class continue def del elif else except finally for from global if "
                           "import in is lambda nonlocal not or pass raise return try while with yield match case print self cls");
        m_types = words("int float str bytes bool list dict set tuple object type");
        m_constants = words("True False None");
        m_capsAreTypes = true;
    } else if (l == "javascript" || l == "typescript" || l == "qml") {
        m_lineComments = {"//"}; m_blockStart = "/*"; m_blockEnd = "*/"; m_template = l != "qml"; m_capsAreTypes = true;
        m_keywords = words("async await break case catch class const continue debugger default delete do else export extends "
                           "finally for from function if import in instanceof let new of return static super switch this throw "
                           "try typeof var void while with yield interface type enum implements namespace declare readonly "
                           "public private protected abstract as keyof satisfies property signal readonly required alias "
                           "component pragma");
        m_types = words("string number boolean any unknown never object symbol bigint int real double var bool list url color");
        m_constants = words("true false null undefined NaN Infinity");
    } else if (l == "go") {
        m_lineComments = {"//"}; m_blockStart = "/*"; m_blockEnd = "*/";
        m_keywords = words("break case chan const continue default defer else fallthrough for func go goto if import interface "
                           "map package range return select struct switch type var");
        m_types = words("bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string uint uint8 "
                        "uint16 uint32 uint64 uintptr any");
        m_constants = words("true false nil iota");
    } else if (l == "java") {
        m_lineComments = {"//"}; m_blockStart = "/*"; m_blockEnd = "*/"; m_capsAreTypes = true; m_singleQuoteChars = true;
        m_keywords = words("abstract assert break case catch class const continue default do else enum extends final finally for "
                           "goto if implements import instanceof interface native new package private protected public return "
                           "static strictfp super switch synchronized this throw throws transient try volatile while var fun val "
                           "when object companion data sealed override open internal namespace using");
        m_types = words("boolean byte char double float int long short void String");
        m_constants = words("true false null");
    } else if (l == "shell") {
        m_lineComments = {"#"}; m_shellVars = true;
        m_keywords = words("if then else elif fi for while until do done case esac in function return local export readonly set "
                           "unset source alias exit break continue select end begin and or not switch");
        m_types = words("echo printf cd test read exec eval trap shift");
        m_constants = words("true false");
    } else if (l == "json") {
        m_lineComments = {"//"}; m_blockStart = "/*"; m_blockEnd = "*/"; m_keysBeforeColon = true;
        m_constants = words("true false null");
    } else if (l == "yaml") {
        m_lineComments = {"#"}; m_keysBeforeColon = true;
        m_constants = words("true false null yes no on off ~");
    } else if (l == "toml" || l == "ini") {
        m_lineComments = l == "ini" ? QStringList{"#", ";"} : QStringList{"#"}; m_sections = true; m_keysBeforeColon = true;
        m_constants = words("true false");
    } else if (l == "cmake") {
        m_lineComments = {"#"}; m_shellVars = true; m_caseInsensitive = true;
        m_keywords = words("if elseif else endif foreach endforeach while endwhile function endfunction macro endmacro return set "
                           "unset option project add_executable add_library target_link_libraries target_include_directories "
                           "target_sources target_compile_options find_package include install add_test enable_testing message");
        m_constants = words("ON OFF TRUE FALSE PRIVATE PUBLIC INTERFACE REQUIRED COMPONENTS");
    } else if (l == "lua") {
        m_lineComments = {"--"}; m_blockStart = "--[["; m_blockEnd = "]]";
        m_keywords = words("and break do else elseif end for function goto if in local not or repeat return then until while");
        m_constants = words("true false nil");
    } else if (l == "sql") {
        m_lineComments = {"--"}; m_blockStart = "/*"; m_blockEnd = "*/"; m_caseInsensitive = true;
        m_keywords = words("select from where insert into values update set delete create table index view drop alter add column "
                           "primary key foreign references join left right inner outer on as and or not null is in group by order "
                           "having limit offset union all distinct case when then else end begin commit rollback with returning");
        m_types = words("integer int bigint text varchar char boolean real double float numeric date timestamp blob serial");
        m_constants = words("true false null");
    } else if (l == "css") {
        m_blockStart = "/*"; m_blockEnd = "*/"; m_keysBeforeColon = true;
    } else if (l == "html") {
        m_blockStart = "<!--"; m_blockEnd = "-->"; m_tags = true;
    } else if (l == "make") {
        m_lineComments = {"#"}; m_shellVars = true;
        m_keywords = words("ifeq ifneq ifdef ifndef else endif include export set");
    }
    if (m_caseInsensitive) {
        auto lower = [](const QSet<QString> &s) { QSet<QString> o; for (const QString &w : s) o.insert(w.toLower()); return o; };
        m_keywords = lower(m_keywords); m_types = lower(m_types); m_constants = lower(m_constants);
    }
}

int Highlighter::highlight(const QString &line, int state, QVector<Span> *out) const {
    out->clear();
    if (m_lang == "markdown") return highlightMarkdown(line, state, out);
    if (m_lang.isEmpty()) return 0;
    const int n = int(line.size());
    auto add = [&](int start, int len, Kind k) { if (len > 0) out->append({start, len, k}); };
    int i = 0;

    // continue what the previous line left open
    if (state == BlockComment) {
        const int e = int(line.indexOf(m_blockEnd));
        if (e < 0) { add(0, n, Comment); return BlockComment; }
        add(0, e + int(m_blockEnd.size()), Comment);
        i = e + int(m_blockEnd.size());
        state = Normal;
    } else if (state == Triple2 || state == Triple1 || state == Template) {
        const QString close = state == Triple2 ? "\"\"\"" : state == Triple1 ? "'''" : "`";
        int e = int(line.indexOf(close));
        while (e > 0 && line[e - 1] == '\\' && state == Template) e = int(line.indexOf(close, e + 1));
        if (e < 0) { add(0, n, String); return state; }
        add(0, e + int(close.size()), String);
        i = e + int(close.size());
        state = Normal;
    }

    const QString trimmed = line.trimmed();
    if (m_hashPreproc && trimmed.startsWith('#') && i == 0) {
        // the directive itself; a trailing comment keeps its colour
        const int c = int(line.indexOf("//"));
        add(0, c < 0 ? n : c, Preproc);
        if (c >= 0) add(c, n - c, Comment);
        return Normal;
    }
    if (m_sections && trimmed.startsWith('[') && i == 0) { add(0, n, Heading); return Normal; }

    bool lineStart = true;   // only whitespace so far (YAML/TOML keys, shell comments)
    while (i < n) {
        const QChar c = line[i];
        if (c.isSpace()) { ++i; continue; }
        // comments
        bool isComment = false;
        for (const QString &lc : m_lineComments)
            if (line.mid(i, lc.size()) == lc && !(m_blockStart.size() && line.mid(i, m_blockStart.size()) == m_blockStart)) {
                // a shell # is a comment only at a word start ("a#b" is not)
                if (lc == "#" && m_shellVars && i > 0 && !line[i - 1].isSpace()) break;
                isComment = true;
            }
        if (isComment) { add(i, n - i, Comment); return Normal; }
        if (!m_blockStart.isEmpty() && line.mid(i, m_blockStart.size()) == m_blockStart) {
            const int e = int(line.indexOf(m_blockEnd, i + m_blockStart.size()));
            if (e < 0) { add(i, n - i, Comment); return BlockComment; }
            add(i, e + int(m_blockEnd.size()) - i, Comment);
            i = e + int(m_blockEnd.size());
            continue;
        }
        // markup tags
        if (m_tags && c == '<') {
            int j = i + 1;
            if (j < n && (line[j] == '/' || line[j] == '!' || line[j] == '?')) ++j;
            const int s = j;
            while (j < n && (line[j].isLetterOrNumber() || line[j] == '-' || line[j] == ':' || line[j] == '_' || line[j] == '.')) ++j;
            add(i, j - i, Tag);
            i = j;
            // attributes until '>'
            while (i < n && line[i] != '>') {
                if (line[i] == '"' || line[i] == '\'') {
                    const QChar q = line[i];
                    int e = int(line.indexOf(q, i + 1));
                    e = e < 0 ? n : e + 1;
                    add(i, e - i, String);
                    i = e;
                } else if (line[i].isLetter()) {
                    int e = i;
                    while (e < n && (line[e].isLetterOrNumber() || line[e] == '-' || line[e] == ':' || line[e] == '_')) ++e;
                    add(i, e - i, Type);
                    i = e;
                } else ++i;
            }
            if (i < n) { add(i, 1, Tag); ++i; }
            (void)s;
            continue;
        }
        // strings
        if (c == '"' || c == '\'' || (c == '`' && m_template)) {
            if (m_triple && line.mid(i, 3) == QString(3, c)) {
                const int e = int(line.indexOf(QString(3, c), i + 3));
                if (e < 0) { add(i, n - i, String); return c == '"' ? Triple2 : Triple1; }
                add(i, e + 3 - i, String);
                i = e + 3;
                continue;
            }
            // a Rust lifetime or a generic quote that is not a char literal
            if (c == '\'' && (m_lang == "rust" || m_lang == "make")) {
                const bool charLit = (i + 2 < n && line[i + 2] == '\'') || (i + 3 < n && line[i + 1] == '\\' && line.indexOf('\'', i + 2) <= i + 8 && line.indexOf('\'', i + 2) > 0);
                if (!charLit) { ++i; continue; }
            }
            if (c == '\'' && m_shellVars == false && m_lang == "yaml" && !lineStart && i > 0 && line[i - 1].isLetterOrNumber()) { ++i; continue; }
            int j = i + 1;
            while (j < n && line[j] != c) j += line[j] == '\\' && m_lang != "shell" ? 2 : 1;
            if (j >= n && c == '`') { add(i, n - i, String); return Template; }
            j = qMin(j + 1, n);
            // a quoted key ("key": ...) reads as a key
            int k = j;
            while (k < n && line[k].isSpace()) ++k;
            add(i, j - i, m_keysBeforeColon && k < n && line[k] == ':' ? Keyword : String);
            i = j;
            lineStart = false;
            continue;
        }
        // numbers
        if (c.isDigit() || (c == '.' && i + 1 < n && line[i + 1].isDigit())) {
            if (i > 0 && (line[i - 1].isLetter() || line[i - 1] == '_')) { ++i; continue; }
            int j = i + 1;
            while (j < n && (line[j].isLetterOrNumber() || line[j] == '.' || line[j] == '_' ||
                             ((line[j] == '+' || line[j] == '-') && (line[j - 1] == 'e' || line[j - 1] == 'E'))))
                ++j;
            add(i, j - i, Number);
            i = j;
            lineStart = false;
            continue;
        }
        // $VARIABLES and ${...}
        if (m_shellVars && c == '$' && i + 1 < n) {
            int j = i + 1;
            if (line[j] == '{' || line[j] == '(') { const int e = int(line.indexOf(line[j] == '{' ? '}' : ')', j)); j = e < 0 ? n : e + 1; }
            else while (j < n && (line[j].isLetterOrNumber() || line[j] == '_' || ((j == i + 1) && QStringLiteral("@*#?$!-0").contains(line[j])))) ++j;
            add(i, j - i, Constant);
            i = j;
            continue;
        }
        // words
        if (c.isLetter() || c == '_' || c == '@') {
            int j = i + 1;
            while (j < n && (line[j].isLetterOrNumber() || line[j] == '_' || (m_lang == "css" && line[j] == '-') || (m_lang == "cmake" && line[j] == '_'))) ++j;
            const QString w = line.mid(i, j - i);
            const QString key = m_caseInsensitive ? w.toLower() : w;
            int k = j;
            while (k < n && line[k].isSpace()) ++k;
            // f<T>(...) is a call too: look past a generic argument list
            int call = k;
            if (call < n && line[call] == '<' && m_lang != "html") {
                int depth = 0, g = call;
                for (; g < n; ++g) {
                    if (line[g] == '<') ++depth;
                    else if (line[g] == '>' && --depth == 0) break;
                    else if (line[g] == ';' || line[g] == '{' || line[g] == '=') break;
                }
                if (g < n && line[g] == '>') call = g + 1;
            }
            Kind kind = Plain;
            if (m_keysBeforeColon && lineStart && k < n && (line[k] == ':' || (m_sections && line[k] == '='))) {
                // a YAML/TOML/CSS key: extend over dots and dashes
                kind = Keyword;
            } else if (m_keywords.contains(key)) kind = Keyword;
            else if (m_types.contains(key)) kind = Type;
            else if (m_constants.contains(key)) kind = Constant;
            else if (w.startsWith('@')) kind = Preproc;   // decorators, annotations, QML/CSS at-rules
            else if ((k < n && line[k] == '(') || (call != k && call < n && line[call] == '(')) kind = Function;
            else if (m_lang == "rust" && k < n && line[k] == '!') kind = Preproc;   // macros
            else if (m_capsAreTypes && w[0].isUpper() && w.size() > 1 && !(w == w.toUpper() && w.size() > 2)) kind = Type;
            else if (w.size() > 2 && w == w.toUpper() && w[0].isLetter() && m_lang != "sql") kind = Constant;
            add(i, j - i, kind);
            i = j;
            lineStart = false;
            continue;
        }
        if (m_lang == "yaml" && lineStart && c == '-') { ++i; continue; }
        lineStart = false;
        ++i;
    }
    return Normal;
}

// Markdown, and .neb view documents: headings, emphasis, links, inline code, fenced blocks (nebula: components
// included) and YAML front matter.
int Highlighter::highlightMarkdown(const QString &line, int state, QVector<Span> *out) const {
    const int n = int(line.size());
    auto add = [&](int start, int len, Kind k) { if (len > 0) out->append({start, len, k}); };
    const QString t = line.trimmed();
    if (state == -1) {
        if (t == "---") { add(0, n, Preproc); return FrontMatter; }
        state = Normal;
    }
    if (state == FrontMatter) {
        if (t == "---" || t == "...") { add(0, n, Preproc); return Normal; }
        const int colon = int(line.indexOf(':'));
        if (colon > 0) { add(0, colon, Keyword); add(colon + 1, n - colon - 1, String); }
        else add(0, n, String);
        return FrontMatter;
    }
    if (state == Fence) {
        if (t.startsWith("```") || t.startsWith("~~~")) { add(0, n, Code); return Normal; }
        // a component's YAML body: keys stand out
        const int colon = int(line.indexOf(':'));
        if (colon > 0 && !line.left(colon).contains(' ') ) { add(0, colon, Keyword); add(colon, n - colon, Code); }
        else add(0, n, Code);
        return Fence;
    }
    if (t.startsWith("```") || t.startsWith("~~~")) { add(0, n, Code); return Fence; }
    if (t.startsWith('#')) { add(0, n, Heading); return Normal; }
    if (t.startsWith('>')) { add(0, n, Comment); return Normal; }
    if (t == "---" || t == "***" || t == "___") { add(0, n, Preproc); return Normal; }
    int i = 0;
    // list markers
    int s = 0;
    while (s < n && line[s].isSpace()) ++s;
    if (s < n && (line[s] == '-' || line[s] == '*' || line[s] == '+') && s + 1 < n && line[s + 1] == ' ') { add(s, 1, Keyword); i = s + 2; }
    while (i < n) {
        const QChar c = line[i];
        if (c == '`') {
            const int e = int(line.indexOf('`', i + 1));
            if (e < 0) { ++i; continue; }
            add(i, e + 1 - i, Code);
            i = e + 1;
        } else if ((c == '*' || c == '_') && i + 1 < n) {
            const bool strong = line[i + 1] == c;
            const QString mark = strong ? QString(2, c) : QString(c);
            const int e = int(line.indexOf(mark, i + mark.size()));
            if (e < 0 || e == i + int(mark.size())) { ++i; continue; }
            add(i, e + int(mark.size()) - i, Emphasis);
            i = e + int(mark.size());
        } else if (c == '[') {
            const int close = int(line.indexOf(']', i + 1));
            if (close > 0 && close + 1 < n && line[close + 1] == '(') {
                const int e = int(line.indexOf(')', close + 2));
                if (e > 0) { add(i, close + 1 - i, Link); add(close + 1, e - close, Comment); i = e + 1; continue; }
            }
            if (close > 0 && line.mid(i, 2) == "[@") { add(i, close + 1 - i, Link); i = close + 1; continue; }   // citations
            ++i;
        } else if (c == '$') {
            const int e = int(line.indexOf('$', i + 1));
            if (e < 0) { ++i; continue; }
            add(i, e + 1 - i, Constant);
            i = e + 1;
        } else ++i;
    }
    return Normal;
}
