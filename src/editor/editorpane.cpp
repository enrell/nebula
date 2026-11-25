#include "editor/editorpane.h"
#include "paths.h"
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonObject>
#include <QKeyEvent>
#include <QRegularExpression>

namespace {
constexpr int kMaxHighlightedLine = 20000;   // longer lines (minified files) are drawn plain
constexpr qint64 kMaxFileSize = 64ll << 20;

bool isWord(QChar c) { return c.isLetterOrNumber() || c == '_'; }

QString leadingSpace(const QString &l) {
    int i = 0;
    while (i < l.size() && (l[i] == ' ' || l[i] == '\t')) ++i;
    return l.left(i);
}

QChar closerOf(QChar c) {
    switch (c.unicode()) {
    case '(': return ')';
    case '[': return ']';
    case '{': return '}';
    case '"': return '"';
    case '\'': return '\'';
    case '`': return '`';
    default: return {};
    }
}
} // namespace

EditorPane::EditorPane(int id, QObject *parent) : QObject(parent), m_id(id) {
    m_buf.onChanged = [this](int first, int removed, int added) { bufferChanged(first, removed, added); };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &EditorPane::onFileChanged);
    // saving replaces the file (a rename), which drops the watch: add it again once the new file is there
    m_rewatch.setSingleShot(true);
    m_rewatch.setInterval(150);
    connect(&m_rewatch, &QTimer::timeout, this, &EditorPane::watch);
}

EditorPane::~EditorPane() { m_buf.onChanged = nullptr; }

QString EditorPane::unsavedPath() const { return Paths::stateDir() + QString("/editors/%1.txt").arg(m_id); }

bool EditorPane::open(const QString &path, QString *error) {
    const QFileInfo fi(path);
    if (fi.exists() && !fi.isFile()) { if (error) *error = "not a file"; return false; }
    if (fi.exists() && fi.size() > kMaxFileSize) { if (error) *error = "file is larger than 64 MB"; return false; }
    if (fi.exists()) {
        QString err;
        if (!m_buf.load(fi.absoluteFilePath(), &err)) { if (error) *error = err; return false; }
        m_readOnly = !fi.isWritable();
    } else {
        m_buf.loadBytes({});   // a new file: created on the first save
    }
    m_path = fi.exists() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    m_hl = Highlighter(Highlighter::languageFor(m_path, m_buf.line(0)));
    m_states = {m_hl.initialState()};
    m_statesValid = 0;
    detectIndent();
    m_cursor = m_anchor = {};
    m_wasDirty = false;
    watch();
    emit changed();
    emit cursorChanged();
    emit textChanged();
    return true;
}

QString EditorPane::fileName() const { return m_path.isEmpty() ? QString("untitled") : QFileInfo(m_path).fileName(); }
QString EditorPane::label() const { return fileName() + (dirty() ? "  •" : ""); }
QString EditorPane::baseDir() const { return m_path.isEmpty() ? QDir::homePath() : QFileInfo(m_path).absolutePath(); }

bool EditorPane::previewable() const {
    const QString ext = QFileInfo(m_path).suffix().toLower();
    return ext == "neb" || ext == "md" || ext == "markdown";
}

QString EditorPane::status() const {
    QString s = QString("Ln %1, Col %2").arg(cursorLine()).arg(cursorColumn());
    if (hasSelection()) s += QString(" (%1 selected)").arg(selectedText().size());
    s += QString("   %1   %2   %3").arg(m_buf.bom() ? "UTF-8 BOM" : "UTF-8", m_buf.crlf() ? "CRLF" : "LF", languageName());
    s += m_useTabs ? "   Tabs" : QString("   Spaces: %1").arg(m_tabWidth);
    if (m_readOnly) s += "   read-only";
    return s;
}

void EditorPane::setMessage(const QString &m) {
    if (m == m_message) return;
    m_message = m;
    emit messageChanged();
}

// ---- persistence

QJsonObject EditorPane::toJson() const {
    QJsonObject o{{"file", m_path}, {"line", m_cursor.line}, {"col", m_cursor.col}, {"scroll", scrollLine}};
    if (dirty()) {
        QDir().mkpath(QFileInfo(unsavedPath()).absolutePath());
        QFile f(unsavedPath());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(m_buf.text().toUtf8());
        o["unsaved"] = true;
    } else {
        QFile::remove(unsavedPath());
    }
    return o;
}

EditorPane *EditorPane::restore(int id, const QJsonObject &o, QObject *parent) {
    auto *e = new EditorPane(id, parent);
    QString err;
    if (!e->open(o["file"].toString(), &err)) e->setMessage("cannot open: " + err);
    if (o["unsaved"].toBool()) {
        QFile f(e->unsavedPath());
        if (f.open(QIODevice::ReadOnly)) {
            const QString text = QString::fromUtf8(f.readAll());
            if (text != e->m_buf.text()) {
                e->m_buf.setText(text);
                e->setMessage("restored unsaved changes");
            }
        }
    }
    e->setCursor(e->m_buf.clamp({o["line"].toInt(), o["col"].toInt()}), false);
    e->scrollLine = o["scroll"].toInt();
    e->m_wasDirty = e->dirty();
    emit e->changed();
    return e;
}

void EditorPane::discard() { QFile::remove(unsavedPath()); }

// ---- file watching

void EditorPane::watch() {
    if (!m_watcher.files().isEmpty()) m_watcher.removePaths(m_watcher.files());
    if (!m_path.isEmpty() && QFileInfo::exists(m_path)) m_watcher.addPath(m_path);
}

void EditorPane::onFileChanged() {
    m_rewatch.start();
    QFile f(m_path);
    if (!f.exists()) { setMessage("the file was deleted on disk; saving creates it again"); return; }
    if (!f.open(QIODevice::ReadOnly)) return;
    const QByteArray disk = f.readAll();
    if (disk == m_buf.bytes()) return;   // our own save, or a change that ends up identical
    if (!dirty()) {
        const Pos c = m_cursor;
        reload();
        setCursor(m_buf.clamp(c), false);
        return;
    }
    m_changedOnDisk = true;
    setMessage("the file changed on disk");
}

void EditorPane::reload() {
    TextBuffer fresh;
    QString err;
    if (!fresh.load(m_path, &err)) { setMessage("cannot reload: " + err); return; }
    m_buf.setSelectionBefore({m_anchor, m_cursor});
    m_buf.setText(fresh.text());   // undoable: the user can get their version back
    m_buf.markSaved();
    m_changedOnDisk = false;
    setMessage({});
    setCursor(m_buf.clamp(m_cursor), false);
    emit changed();
}

void EditorPane::keepMine() {
    m_changedOnDisk = false;
    setMessage({});
}

// ---- highlighting cache

void EditorPane::bufferChanged(int first, int removed, int added) {
    Q_UNUSED(removed);
    Q_UNUSED(added);
    // states up to `first` are still right; the rest is recomputed when a line below is drawn
    if (first < 0 || removed < 0) { m_states = {m_hl.initialState()}; m_statesValid = 0; }
    else if (m_states.size() > first + 1) m_states.resize(first + 1);
    emit textChanged();
    if (dirty() != m_wasDirty) { m_wasDirty = dirty(); emit changed(); }
}

QVector<Highlighter::Span> EditorPane::spans(int line) {
    QVector<Highlighter::Span> out;
    if (line < 0 || line >= m_buf.lineCount() || m_hl.language().isEmpty()) return out;
    if (m_states.isEmpty()) m_states = {m_hl.initialState()};
    while (m_states.size() <= line) {
        const int l = int(m_states.size()) - 1;
        const QString &text = m_buf.line(l);
        m_states << (text.size() > kMaxHighlightedLine ? m_states.last() : m_hl.highlight(text, m_states.last(), &out));
    }
    const QString &text = m_buf.line(line);
    if (text.size() > kMaxHighlightedLine) return {};
    m_hl.highlight(text, m_states[line], &out);
    return out;
}

// ---- display columns

int EditorPane::charWidth(char32_t c) {
    if (c == 0x200B || (c >= 0x300 && c <= 0x36F) || (c >= 0xFE00 && c <= 0xFE0F) || c == 0x200D) return 0;   // combining, ZW
    if ((c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF && c != 0x303F) || (c >= 0xAC00 && c <= 0xD7A3) ||
        (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60) || (c >= 0xFFE0 && c <= 0xFFE6) ||
        (c >= 0x1F300 && c <= 0x1F64F) || (c >= 0x1F900 && c <= 0x1F9FF) || (c >= 0x20000 && c <= 0x3FFFD))
        return 2;
    return 1;
}

int EditorPane::visualColumn(int line, int col) const {
    if (line < 0 || line >= m_buf.lineCount()) return 0;
    const QString &l = m_buf.line(line);
    col = qMin(col, int(l.size()));
    int v = 0;
    for (int i = 0; i < col; ++i) {
        const QChar c = l[i];
        if (c == '\t') v = (v / m_tabWidth + 1) * m_tabWidth;
        else if (c.isHighSurrogate() && i + 1 < l.size()) { v += charWidth(QChar::surrogateToUcs4(c, l[i + 1])); ++i; }
        else v += charWidth(c.unicode());
    }
    return v;
}

int EditorPane::columnAt(int line, int visual) const {
    if (line < 0 || line >= m_buf.lineCount()) return 0;
    const QString &l = m_buf.line(line);
    int v = 0;
    for (int i = 0; i < l.size(); ++i) {
        const QChar c = l[i];
        int w, units = 1;
        if (c == '\t') w = (v / m_tabWidth + 1) * m_tabWidth - v;
        else if (c.isHighSurrogate() && i + 1 < l.size()) { w = charWidth(QChar::surrogateToUcs4(c, l[i + 1])); units = 2; }
        else w = charWidth(c.unicode());
        if (visual < v + (w + 1) / 2) return i;
        v += w;
        i += units - 1;
    }
    return int(l.size());
}

// ---- cursor

void EditorPane::setCursor(Pos p, bool keepAnchor) {
    p = m_buf.clamp(p);
    // never land between the halves of a surrogate pair
    if (p.col > 0 && p.col < m_buf.line(p.line).size() && m_buf.line(p.line)[p.col].isLowSurrogate()) --p.col;
    m_cursor = p;
    if (!keepAnchor) m_anchor = p;
    m_wantVisual = -1;
    if (hasSelection() && QGuiApplication::clipboard() && QGuiApplication::clipboard()->supportsSelection())
        QGuiApplication::clipboard()->setText(selectedText(), QClipboard::Selection);
    emit cursorChanged();
    emit ensureCursorVisible();
}

void EditorPane::setSelection(Pos anchor, Pos cursor) {
    setCursor(anchor, false);
    setCursor(cursor, true);
}

void EditorPane::moveCursor(Pos p, bool select) {
    const int keep = m_wantVisual;
    setCursor(p, select);
    m_wantVisual = keep;
}

void EditorPane::selectWordAt(Pos p) {
    p = m_buf.clamp(p);
    const QString &l = m_buf.line(p.line);
    int a = p.col, b = p.col;
    if (a < l.size() && isWord(l[a])) {
        while (a > 0 && isWord(l[a - 1])) --a;
        while (b < l.size() && isWord(l[b])) ++b;
    } else if (a > 0 && isWord(l[a - 1])) {
        while (a > 0 && isWord(l[a - 1])) --a;
    } else if (b < l.size()) ++b;
    setSelection({p.line, a}, {p.line, b});
}

void EditorPane::selectLine(int line) {
    line = qBound(0, line, m_buf.lineCount() - 1);
    setSelection({line, 0}, line + 1 < m_buf.lineCount() ? Pos{line + 1, 0} : Pos{line, int(m_buf.line(line).size())});
}

EditorPane::Pos EditorPane::wordLeft(Pos p) const {
    if (p.col == 0) return p.line > 0 ? Pos{p.line - 1, int(m_buf.line(p.line - 1).size())} : p;
    const QString &l = m_buf.line(p.line);
    int i = p.col;
    while (i > 0 && l[i - 1].isSpace()) --i;
    if (i > 0 && isWord(l[i - 1])) while (i > 0 && isWord(l[i - 1])) --i;
    else if (i > 0) while (i > 0 && !isWord(l[i - 1]) && !l[i - 1].isSpace()) --i;
    return {p.line, i};
}

EditorPane::Pos EditorPane::wordRight(Pos p) const {
    const QString &l = m_buf.line(p.line);
    if (p.col >= l.size()) return p.line + 1 < m_buf.lineCount() ? Pos{p.line + 1, 0} : p;
    int i = p.col;
    if (isWord(l[i])) while (i < l.size() && isWord(l[i])) ++i;
    else if (!l[i].isSpace()) while (i < l.size() && !isWord(l[i]) && !l[i].isSpace()) ++i;
    while (i < l.size() && l[i].isSpace()) ++i;
    return {p.line, i};
}

// ---- editing

void EditorPane::edit(const std::function<void()> &f) {
    if (m_readOnly) { setMessage("the file is read-only"); return; }
    m_buf.setSelectionBefore({m_anchor, m_cursor});
    f();
    m_buf.setSelectionAfter({m_anchor, m_cursor});
}

void EditorPane::detectIndent() {
    int tabs = 0, two = 0, four = 0, spaced = 0;
    for (int i = 0; i < qMin(m_buf.lineCount(), 2000); ++i) {
        const QString &l = m_buf.line(i);
        if (l.startsWith('\t')) ++tabs;
        else if (l.startsWith(' ')) {
            const int n = int(leadingSpace(l).size());
            ++spaced;
            if (n % 4 == 0) ++four;
            else if (n % 2 == 0) ++two;
        }
    }
    m_useTabs = tabs > spaced;
    const bool twoWide = (!m_useTabs && two > 0 && two > four / 2) || m_hl.language() == "yaml" || (m_hl.language() == "json" && two > 0);
    m_tabWidth = twoWide ? 2 : 4;
    if (m_hl.language() == "make") m_useTabs = true;
}

QString EditorPane::indentUnit() const { return m_useTabs ? QString("\t") : QString(m_tabWidth, ' '); }

void EditorPane::insertText(const QString &text) {
    if (text.isEmpty()) return;
    edit([&] {
        const Pos e = m_buf.replace(selection(), text);
        setCursor(e, false);
    });
}

void EditorPane::paste(const QString &text) {
    m_buf.breakMerge();
    insertText(text);
    m_buf.breakMerge();
}

void EditorPane::copy() {
    QString t = selectedText();
    if (t.isEmpty()) t = m_buf.line(m_cursor.line) + '\n';   // no selection: the whole line
    QGuiApplication::clipboard()->setText(t);
}

void EditorPane::cut() {
    copy();
    edit([&] {
        if (hasSelection()) { m_buf.remove(selection()); setCursor(selection().a, false); }
        else { deleteLines(); }
    });
}

void EditorPane::undo() {
    Range r;
    if (m_buf.undo(&r)) setSelection(r.a, r.b);
}

void EditorPane::redo() {
    Range r;
    if (m_buf.redo(&r)) setSelection(r.a, r.b);
}

void EditorPane::selectAll() { setSelection({0, 0}, m_buf.end()); }

void EditorPane::newline() {
    edit([&] {
        const Range sel = selection();
        const QString &l = m_buf.line(sel.a.line);
        QString ind = leadingSpace(l);
        if (ind.size() > sel.a.col) ind = ind.left(sel.a.col);
        const QString before = l.left(sel.a.col).trimmed();
        const QChar last = before.isEmpty() ? QChar() : before.back();
        const bool opens = last == '{' || last == '[' || last == '(' ||
                           (last == ':' && (m_hl.language() == "python" || m_hl.language() == "yaml"));
        const QString after = sel.b.line == sel.a.line ? l.mid(sel.b.col) : m_buf.line(sel.b.line).mid(sel.b.col);
        const QChar next = after.trimmed().isEmpty() ? QChar() : after.trimmed().front();
        const bool closes = opens && closerOf(last) == next && next != QChar();
        m_buf.remove(sel);
        // a line that is only indentation is left empty
        if (l.left(sel.a.col).trimmed().isEmpty() && sel.a.col > 0 && l.mid(sel.b.line == sel.a.line ? sel.b.col : l.size()).trimmed().isEmpty()) {
            m_buf.remove({{sel.a.line, 0}, {sel.a.line, sel.a.col}});
            setCursor(m_buf.insert({sel.a.line, 0}, "\n" + ind), false);
            return;
        }
        // trailing spaces before the cursor are dropped
        int strip = sel.a.col;
        while (strip > int(ind.size()) && l[strip - 1] == ' ') --strip;
        if (strip < sel.a.col) m_buf.remove({{sel.a.line, strip}, {sel.a.line, sel.a.col}});
        Pos at{sel.a.line, strip};
        if (closes) {
            const Pos mid = m_buf.insert(at, "\n" + ind + indentUnit());
            m_buf.insert(mid, "\n" + ind);
            setCursor(mid, false);
        } else {
            setCursor(m_buf.insert(at, "\n" + ind + (opens ? indentUnit() : QString())), false);
        }
    });
}

void EditorPane::backspace(bool word) {
    if (hasSelection()) { edit([&] { const Range s = selection(); m_buf.remove(s); setCursor(s.a, false); }); return; }
    const Pos c = m_cursor;
    if (c.line == 0 && c.col == 0) return;
    edit([&] {
        Pos from;
        const QString &l = m_buf.line(c.line);
        if (word) from = wordLeft(c);
        else if (c.col == 0) from = {c.line - 1, int(m_buf.line(c.line - 1).size())};
        else if (!m_useTabs && l.left(c.col).trimmed().isEmpty() && l.left(c.col).endsWith(' ')) {
            // in the indentation: back to the previous indent stop
            const int v = c.col, stop = (v - 1) / m_tabWidth * m_tabWidth;
            from = {c.line, stop};
        } else {
            from = {c.line, c.col - (c.col > 1 && l[c.col - 1].isLowSurrogate() ? 2 : 1)};
            // an empty pair "()" goes away together
            if (c.col < l.size() && closerOf(l[c.col - 1]) == l[c.col] && !closerOf(l[c.col - 1]).isNull()) {
                m_buf.remove({from, {c.line, c.col + 1}});
                setCursor(from, false);
                return;
            }
        }
        m_buf.remove({from, c});
        setCursor(from, false);
    });
}

void EditorPane::deleteForward(bool word) {
    if (hasSelection()) { edit([&] { const Range s = selection(); m_buf.remove(s); setCursor(s.a, false); }); return; }
    const Pos c = m_cursor;
    const QString &l = m_buf.line(c.line);
    Pos to;
    if (word) to = wordRight(c);
    else if (c.col >= l.size()) { if (c.line + 1 >= m_buf.lineCount()) return; to = {c.line + 1, 0}; }
    else to = {c.line, c.col + (l[c.col].isHighSurrogate() ? 2 : 1)};
    edit([&] { m_buf.remove({c, to}); setCursor(c, false); });
}

void EditorPane::indent(bool outdent) {
    const Range s = selection();
    const bool lines = s.a.line != s.b.line || outdent;
    if (!lines) {
        // insert up to the next tab stop
        edit([&] {
            const QString unit = m_useTabs ? QString("\t") : QString(m_tabWidth - visualColumn(m_cursor.line, s.a.col) % m_tabWidth, ' ');
            m_buf.remove(s);
            setCursor(m_buf.insert(s.a, unit), false);
        });
        return;
    }
    const int last = s.b.col == 0 && s.b.line > s.a.line ? s.b.line - 1 : s.b.line;
    edit([&] {
        m_buf.beginGroup();
        Pos a = m_anchor, c = m_cursor;
        for (int i = s.a.line; i <= last; ++i) {
            const QString &l = m_buf.line(i);
            int delta = 0;
            if (outdent) {
                int n = 0;
                if (l.startsWith('\t')) n = 1;
                else while (n < m_tabWidth && n < l.size() && l[n] == ' ') ++n;
                if (n) m_buf.remove({{i, 0}, {i, n}});
                delta = -n;
            } else if (!l.isEmpty()) {
                m_buf.insert({i, 0}, indentUnit());
                delta = int(indentUnit().size());
            }
            if (a.line == i) a.col = qMax(0, a.col + delta);
            if (c.line == i) c.col = qMax(0, c.col + delta);
        }
        m_buf.endGroup();
        setSelection(a, c);
    });
}

void EditorPane::toggleComment() {
    const QString lc = m_hl.lineComment();
    if (lc.isEmpty()) return;
    const Range s = selection();
    const int last = s.b.col == 0 && s.b.line > s.a.line ? s.b.line - 1 : s.b.line;
    bool all = true;
    int minIndent = INT_MAX;
    for (int i = s.a.line; i <= last; ++i) {
        const QString &l = m_buf.line(i);
        if (l.trimmed().isEmpty()) continue;
        minIndent = qMin(minIndent, int(leadingSpace(l).size()));
        if (!l.trimmed().startsWith(lc)) all = false;
    }
    if (minIndent == INT_MAX) return;
    edit([&] {
        m_buf.beginGroup();
        for (int i = s.a.line; i <= last; ++i) {
            const QString &l = m_buf.line(i);
            if (l.trimmed().isEmpty()) continue;
            if (all) {
                const int at = int(l.indexOf(lc));
                const int n = int(lc.size()) + (at + lc.size() < l.size() && l[at + lc.size()] == ' ' ? 1 : 0);
                m_buf.remove({{i, at}, {i, at + n}});
            } else {
                m_buf.insert({i, minIndent}, lc + ' ');
            }
        }
        m_buf.endGroup();
        setSelection(m_buf.clamp(m_anchor), m_buf.clamp(m_cursor));
    });
}

void EditorPane::moveLines(int delta) {
    const Range s = selection();
    const int first = s.a.line, last = s.b.col == 0 && s.b.line > s.a.line ? s.b.line - 1 : s.b.line;
    if ((delta < 0 && first == 0) || (delta > 0 && last + 1 >= m_buf.lineCount())) return;
    edit([&] {
        const int a = delta < 0 ? first - 1 : first, b = delta < 0 ? last : last + 1;
        QStringList lines;
        for (int i = a; i <= b; ++i) lines << m_buf.line(i);
        if (delta < 0) lines.move(0, int(lines.size()) - 1);
        else lines.move(int(lines.size()) - 1, 0);
        m_buf.replace({{a, 0}, {b, int(m_buf.line(b).size())}}, lines.join('\n'));
        setSelection({m_anchor.line + delta, m_anchor.col}, {m_cursor.line + delta, m_cursor.col});
    });
}

void EditorPane::duplicate() {
    edit([&] {
        if (hasSelection()) {
            const Range s = selection();
            const QString t = m_buf.text(s);
            const Pos e = m_buf.insert(s.b, t);
            setSelection(s.b, e);
            return;
        }
        const int l = m_cursor.line;
        m_buf.insert({l, int(m_buf.line(l).size())}, "\n" + m_buf.line(l));
        setCursor({l + 1, m_cursor.col}, false);
    });
}

void EditorPane::deleteLines() {
    const Range s = selection();
    const int first = s.a.line, last = s.b.col == 0 && s.b.line > s.a.line ? s.b.line - 1 : s.b.line;
    edit([&] {
        Range r{{first, 0}, last + 1 < m_buf.lineCount() ? Pos{last + 1, 0} : Pos{last, int(m_buf.line(last).size())}};
        if (last + 1 >= m_buf.lineCount() && first > 0) r.a = {first - 1, int(m_buf.line(first - 1).size())};
        m_buf.remove(r);
        setCursor(m_buf.clamp({qMin(first, m_buf.lineCount() - 1), m_cursor.col}), false);
    });
}

bool EditorPane::keyPress(QKeyEvent *e, int pageLines) {
    if (m_closeRequested) {   // "save changes?" is open: Enter saves, Esc cancels, nothing else edits
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) resolveClose(true);
        else if (e->key() == Qt::Key_Escape) cancelClose();
        return true;
    }
    const auto mods = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    const bool ctrl = mods & Qt::ControlModifier, shift = mods & Qt::ShiftModifier, alt = mods & Qt::AltModifier;
    const int k = e->key();
    const QString text = e->text();

    if (ctrl && !alt) {
        switch (k) {
        case Qt::Key_S: if (shift) emit findRequested("saveas", m_path); else save(); return true;
        case Qt::Key_Z: if (shift) redo(); else undo(); return true;
        case Qt::Key_Y: redo(); return true;
        case Qt::Key_A: selectAll(); return true;
        case Qt::Key_C: case Qt::Key_Insert: copy(); return true;
        case Qt::Key_X: cut(); return true;
        case Qt::Key_V: paste(QGuiApplication::clipboard()->text()); return true;
        case Qt::Key_F: emit findRequested("find", selectedText().contains('\n') ? QString() : selectedText()); return true;
        case Qt::Key_H: case Qt::Key_R: emit findRequested("replace", selectedText().contains('\n') ? QString() : selectedText()); return true;
        case Qt::Key_G: emit findRequested("goto", {}); return true;
        case Qt::Key_Slash: toggleComment(); return true;
        case Qt::Key_D: duplicate(); return true;
        case Qt::Key_L: selectLine(m_cursor.line); return true;
        case Qt::Key_K: if (shift) { deleteLines(); return true; } break;
        case Qt::Key_Backspace: backspace(true); return true;
        case Qt::Key_Delete: deleteForward(true); return true;
        case Qt::Key_Left: moveCursor(wordLeft(m_cursor), shift); return true;
        case Qt::Key_Right: moveCursor(wordRight(m_cursor), shift); return true;
        case Qt::Key_Home: moveCursor({0, 0}, shift); return true;
        case Qt::Key_End: moveCursor(m_buf.end(), shift); return true;
        case Qt::Key_Up: scrollLine = qMax(0, scrollLine - 1); emit cursorChanged(); return true;
        case Qt::Key_Down: scrollLine = qMin(m_buf.lineCount() - 1, scrollLine + 1); emit cursorChanged(); return true;
        default: break;
        }
    }
    if (alt && !ctrl && (k == Qt::Key_Up || k == Qt::Key_Down)) { moveLines(k == Qt::Key_Up ? -1 : 1); return true; }
    if (shift && !ctrl && k == Qt::Key_Insert) { paste(QGuiApplication::clipboard()->text()); return true; }
    if (shift && !ctrl && k == Qt::Key_Delete) { cut(); return true; }

    auto vertical = [&](int lines) {
        if (m_wantVisual < 0) m_wantVisual = visualColumn(m_cursor.line, m_cursor.col);
        const int want = m_wantVisual;
        const int line = qBound(0, m_cursor.line + lines, m_buf.lineCount() - 1);
        Pos p{line, columnAt(line, want)};
        if (m_cursor.line + lines < 0) p = {0, 0};
        if (m_cursor.line + lines >= m_buf.lineCount()) p = m_buf.end();
        setCursor(p, shift);
        m_wantVisual = want;
    };
    switch (k) {
    case Qt::Key_Left:
        if (hasSelection() && !shift) { setCursor(selection().a, false); return true; }
        if (m_cursor.col > 0) {
            const int step = m_cursor.col > 1 && m_buf.line(m_cursor.line)[m_cursor.col - 1].isLowSurrogate() ? 2 : 1;
            setCursor({m_cursor.line, m_cursor.col - step}, shift);
        } else if (m_cursor.line > 0) setCursor({m_cursor.line - 1, int(m_buf.line(m_cursor.line - 1).size())}, shift);
        return true;
    case Qt::Key_Right:
        if (hasSelection() && !shift) { setCursor(selection().b, false); return true; }
        if (m_cursor.col < m_buf.line(m_cursor.line).size()) {
            const int step = m_buf.line(m_cursor.line)[m_cursor.col].isHighSurrogate() ? 2 : 1;
            setCursor({m_cursor.line, m_cursor.col + step}, shift);
        } else if (m_cursor.line + 1 < m_buf.lineCount()) setCursor({m_cursor.line + 1, 0}, shift);
        return true;
    case Qt::Key_Up: vertical(-1); return true;
    case Qt::Key_Down: vertical(1); return true;
    case Qt::Key_PageUp: vertical(-qMax(1, pageLines - 1)); scrollLine = qMax(0, scrollLine - qMax(1, pageLines - 1)); return true;
    case Qt::Key_PageDown: vertical(qMax(1, pageLines - 1)); scrollLine = qMin(qMax(0, m_buf.lineCount() - 1), scrollLine + qMax(1, pageLines - 1)); return true;
    case Qt::Key_Home: {
        // first press: to the first non-blank character; again: to column 0
        const int ind = int(leadingSpace(m_buf.line(m_cursor.line)).size());
        setCursor({m_cursor.line, m_cursor.col == ind ? 0 : ind}, shift);
        return true;
    }
    case Qt::Key_End: setCursor({m_cursor.line, int(m_buf.line(m_cursor.line).size())}, shift); return true;
    case Qt::Key_Return: case Qt::Key_Enter: if (!ctrl) { newline(); return true; } break;
    case Qt::Key_Backspace: backspace(false); return true;
    case Qt::Key_Delete: deleteForward(false); return true;
    case Qt::Key_Tab: indent(false); return true;
    case Qt::Key_Backtab: indent(true); return true;
    case Qt::Key_Escape: if (hasSelection()) { setCursor(m_cursor, false); return true; } return false;
    case Qt::Key_F3: emit findRequested(shift ? "previous" : "next", {}); return true;
    default: break;
    }
    if (ctrl || text.isEmpty() || !text[0].isPrint()) return false;

    // typing, with brackets and quotes closed as you open them
    const QChar c = text.size() == 1 ? text[0] : QChar();
    const QString &line = m_buf.line(m_cursor.line);
    const QChar next = m_cursor.col < line.size() ? line[m_cursor.col] : QChar();
    if (!hasSelection() && !c.isNull() && (c == ')' || c == ']' || c == '}' || c == '"' || c == '\'' || c == '`') && next == c) {
        setCursor({m_cursor.line, m_cursor.col + 1}, false);   // typed over the closer that was inserted for it
        return true;
    }
    const QChar closer = closerOf(c);
    if (!closer.isNull() && m_hl.language() != "" && !m_readOnly) {
        const QChar prev = m_cursor.col > 0 ? line[m_cursor.col - 1] : QChar();
        const bool quote = c == '"' || c == '\'' || c == '`';
        const bool nextFree = next.isNull() || next.isSpace() || QStringLiteral(")]},;").contains(next);
        if (hasSelection()) {   // wrap the selection
            edit([&] {
                const Range s = selection();
                const QString inner = m_buf.text(s);
                m_buf.replace(s, QString(c) + inner + closer);
                const Pos a{s.a.line, s.a.col + 1};
                setSelection(a, s.a.line == s.b.line ? Pos{s.b.line, s.b.col + 1} : s.b);
            });
            return true;
        }
        if (nextFree && !(quote && (isWord(prev) || (c == '\'' && m_hl.language() == "markdown")))) {
            edit([&] {
                m_buf.insert(m_cursor, QString(c) + closer);
                setCursor({m_cursor.line, m_cursor.col + 1}, false);
            });
            return true;
        }
    }
    insertText(text);
    return true;
}

void EditorPane::injectKey(int key, int qtMods, const QString &text) {
    QKeyEvent e(QEvent::KeyPress, key, Qt::KeyboardModifiers(qtMods), text);
    keyPress(&e);
}

// ---- files

bool EditorPane::save() {
    if (m_path.isEmpty()) { emit findRequested("saveas", {}); return false; }
    QString err;
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    if (!m_buf.save(m_path, &err)) { setMessage("cannot save: " + err); return false; }
    m_changedOnDisk = false;
    m_readOnly = false;
    setMessage({});
    QFile::remove(unsavedPath());
    m_wasDirty = false;
    m_rewatch.start();
    emit changed();
    emit cursorChanged();
    emit saved();
    return true;
}

bool EditorPane::saveAs(const QString &path) {
    if (path.trimmed().isEmpty()) return false;
    const QString abs = QDir(baseDir()).absoluteFilePath(path.trimmed());
    m_path = abs;
    m_hl = Highlighter(Highlighter::languageFor(m_path, m_buf.line(0)));
    m_states = {m_hl.initialState()};
    m_statesValid = 0;
    const bool ok = save();
    if (ok) m_path = QFileInfo(abs).canonicalFilePath();
    emit changed();
    emit textChanged();
    return ok;
}

void EditorPane::gotoLine(int line, int column) {
    const int l = qBound(1, line, m_buf.lineCount()) - 1;
    setCursor({l, columnAt(l, qMax(1, column) - 1)}, false);
}

void EditorPane::requestClose() {
    if (!dirty()) { emit closeConfirmed(); return; }
    m_closeRequested = true;
    emit closeRequestedChanged();
}

void EditorPane::cancelClose() {
    if (!m_closeRequested) return;
    m_closeRequested = false;
    emit closeRequestedChanged();
}

void EditorPane::resolveClose(bool saveFirst) {
    m_closeRequested = false;
    emit closeRequestedChanged();
    if (saveFirst && !save()) return;   // a failed save keeps the pane open with the error shown
    discard();
    emit closeConfirmed();
}

// ---- find & replace

bool EditorPane::find(const QString &query, bool backward, bool caseSensitive, bool wholeWord, bool regex) {
    TextBuffer::FindOptions o;
    o.caseSensitive = caseSensitive; o.wholeWord = wholeWord; o.regex = regex; o.backward = backward;
    Range r;
    const Pos from = backward ? selection().a : selection().b;
    if (!m_buf.find(query, from, o, &r)) return false;
    setSelection(r.a, r.b);
    return true;
}

bool EditorPane::replaceNext(const QString &query, const QString &with, bool caseSensitive, bool wholeWord, bool regex) {
    TextBuffer::FindOptions o;
    o.caseSensitive = caseSensitive; o.wholeWord = wholeWord; o.regex = regex;
    Range r;
    // replace the current match if the selection is one, then move to the next
    if (hasSelection() && m_buf.find(query, selection().a, o, &r) && r.a == selection().a && r.b == selection().b) {
        QString rep = with;
        if (regex) {
            QRegularExpression::PatternOptions po = caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption;
            rep = m_buf.text(r);
            rep.replace(QRegularExpression(query, po), with);
        }
        edit([&] { const Pos e = m_buf.replace(r, rep); setCursor(e, false); });
    }
    return find(query, false, caseSensitive, wholeWord, regex);
}

int EditorPane::replaceAll(const QString &query, const QString &with, bool caseSensitive, bool wholeWord, bool regex) {
    TextBuffer::FindOptions o;
    o.caseSensitive = caseSensitive; o.wholeWord = wholeWord; o.regex = regex;
    int n = 0;
    edit([&] { n = m_buf.replaceAll(query, with, o); setCursor(m_buf.clamp(m_cursor), false); });
    return n;
}

int EditorPane::countMatches(const QString &query, bool caseSensitive, bool wholeWord, bool regex) const {
    TextBuffer::FindOptions o;
    o.caseSensitive = caseSensitive; o.wholeWord = wholeWord; o.regex = regex;
    return m_buf.countMatches(query, o);
}
