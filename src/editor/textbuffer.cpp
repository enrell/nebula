#include "textbuffer.h"
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>

TextBuffer::TextBuffer() { m_lastEdit.start(); }

bool TextBuffer::load(const QString &path, QString *error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    return loadBytes(f.readAll(), error);
}

bool TextBuffer::loadBytes(const QByteArray &raw, QString *error) {
    QByteArray data = raw;
    const bool bom = data.startsWith("\xEF\xBB\xBF");
    if (bom) data.remove(0, 3);
    if (data.contains('\0')) {
        if (error) *error = "binary file";
        return false;
    }
    QStringDecoder dec(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString text = dec.decode(data);
    if (dec.hasError()) {
        if (error) *error = "not UTF-8 text";
        return false;
    }
    const qsizetype lf = text.count('\n'), crlf = text.count(QStringLiteral("\r\n"));
    m_bom = bom;
    m_crlf = lf > 0 && crlf * 2 >= lf;
    QStringList lines = text.split('\n');   // a final newline shows as an empty last line, as it is in the file
    if (m_crlf)
        for (QString &l : lines)
            if (l.endsWith('\r')) l.chop(1);
    m_lines = lines.isEmpty() ? QStringList{QString()} : lines;
    m_history.clear();
    m_undo = m_savedAt = 0;
    m_mergeOpen = false;
    ++m_revision;
    notify(0, -1, lineCount());
    return true;
}

QByteArray TextBuffer::bytes() const {
    const QString eol = m_crlf ? QStringLiteral("\r\n") : QStringLiteral("\n");
    QByteArray out = m_lines.join(eol).toUtf8();
    if (m_bom) out.prepend("\xEF\xBB\xBF");
    return out;
}

bool TextBuffer::save(const QString &path, QString *error) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    const QByteArray b = bytes();
    if (f.write(b) != b.size() || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    markSaved();
    return true;
}

void TextBuffer::setText(const QString &text) {
    beginGroup();
    replace({{0, 0}, end()}, text);
    endGroup();
}

QString TextBuffer::text() const { return m_lines.join('\n'); }

QString TextBuffer::text(Range r) const {
    r = r.normalized();
    r.a = clamp(r.a);
    r.b = clamp(r.b);
    if (r.a.line == r.b.line) return m_lines[r.a.line].mid(r.a.col, r.b.col - r.a.col);
    QString out = m_lines[r.a.line].mid(r.a.col);
    for (int i = r.a.line + 1; i < r.b.line; ++i) out += '\n' + m_lines[i];
    out += '\n' + m_lines[r.b.line].left(r.b.col);
    return out;
}

TextBuffer::Pos TextBuffer::clamp(Pos p) const {
    p.line = qBound(0, p.line, lineCount() - 1);
    p.col = qBound(0, p.col, int(m_lines[p.line].size()));
    return p;
}

void TextBuffer::notify(int first, int removed, int added) {
    if (onChanged) onChanged(first, removed, added);
}

TextBuffer::Pos TextBuffer::rawInsert(Pos at, const QString &text) {
    at = clamp(at);
    QStringList parts = text.split('\n');
    QString &l = m_lines[at.line];
    const QString tail = l.mid(at.col);
    l.truncate(at.col);
    l += parts[0];
    Pos endPos{at.line, int(l.size())};
    if (parts.size() > 1) {
        for (int i = 1; i < parts.size(); ++i) m_lines.insert(at.line + i, parts[i]);
        endPos = {at.line + int(parts.size()) - 1, int(parts.last().size())};
    }
    m_lines[endPos.line] += tail;
    ++m_revision;
    notify(at.line, 1, int(parts.size()));
    return endPos;
}

void TextBuffer::rawRemove(Range r) {
    r = r.normalized();
    r.a = clamp(r.a);
    r.b = clamp(r.b);
    if (r.empty()) return;
    const QString tail = m_lines[r.b.line].mid(r.b.col);
    m_lines[r.a.line].truncate(r.a.col);
    m_lines[r.a.line] += tail;
    if (r.b.line > r.a.line) m_lines.remove(r.a.line + 1, r.b.line - r.a.line);
    ++m_revision;
    notify(r.a.line, r.b.line - r.a.line + 1, 1);
}

void TextBuffer::record(const Edit &e) {
    m_history.resize(m_undo);   // a new edit drops what could have been redone
    if (m_savedAt > m_undo) m_savedAt = -1;   // the saved state is no longer reachable
    bool merge = m_group > 0 && !m_history.isEmpty() && m_mergeOpen;
    if (!merge && m_group == 0 && m_mergeOpen && !m_history.isEmpty() && m_lastEdit.elapsed() < 1500 && m_savedAt != m_undo) {
        const Edit &last = m_history.last().edits.last();
        const bool typing = e.removed.isEmpty() && last.removed.isEmpty() && !e.inserted.contains('\n') && !last.inserted.contains('\n')
                            && e.at.line == last.at.line && e.at.col == last.at.col + last.inserted.size()
                            // a word ends at a space: undo goes back word by word
                            && !(e.inserted == " " && !last.inserted.endsWith(' '));
        const bool backspacing = e.inserted.isEmpty() && last.inserted.isEmpty() && !e.removed.contains('\n')
                                 && e.at.line == last.at.line && e.at.col + e.removed.size() == last.at.col;
        const bool deleting = e.inserted.isEmpty() && last.inserted.isEmpty() && !e.removed.contains('\n')
                              && e.at == last.at;
        merge = typing || backspacing || deleting;
    }
    if (merge) {
        m_history.last().edits << e;
    } else {
        m_history << Step{{e}, m_hintBefore, m_hintBefore};
        m_undo = int(m_history.size());
    }
    m_mergeOpen = true;
    m_lastEdit.restart();
}

void TextBuffer::setSelectionAfter(Range r) {
    if (m_undo > 0 && m_undo == m_history.size()) m_history.last().after = r;
}

TextBuffer::Pos TextBuffer::insert(Pos at, const QString &text) {
    at = clamp(at);
    QString t = text;
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace('\r', '\n');
    if (t.isEmpty()) return at;
    record({at, {}, t});
    return rawInsert(at, t);
}

void TextBuffer::remove(Range r) {
    r = r.normalized();
    r.a = clamp(r.a);
    r.b = clamp(r.b);
    if (r.empty()) return;
    record({r.a, text(r), {}});
    rawRemove(r);
}

TextBuffer::Pos TextBuffer::replace(Range r, const QString &text) {
    r = r.normalized();
    if (r.empty()) return insert(r.a, text);   // plain typing: stays mergeable with the keys before it
    beginGroup();
    remove(r);
    const Pos e = insert(clamp(r.a), text);
    endGroup();
    return e;
}

void TextBuffer::beginGroup() {
    if (m_group++ == 0) m_mergeOpen = false;
}

void TextBuffer::endGroup() {
    if (m_group > 0 && --m_group == 0) m_mergeOpen = false;
}

bool TextBuffer::undo(Range *selection) {
    if (!canUndo()) return false;
    const Step &s = m_history[--m_undo];
    for (int i = int(s.edits.size()) - 1; i >= 0; --i) {
        const Edit &e = s.edits[i];
        if (!e.inserted.isEmpty()) {
            Pos endPos = e.at;
            const QStringList parts = e.inserted.split('\n');
            endPos = parts.size() == 1 ? Pos{e.at.line, e.at.col + int(parts[0].size())} : Pos{e.at.line + int(parts.size()) - 1, int(parts.last().size())};
            rawRemove({e.at, endPos});
        }
        if (!e.removed.isEmpty()) rawInsert(e.at, e.removed);
    }
    m_mergeOpen = false;
    if (selection) *selection = s.before;
    return true;
}

bool TextBuffer::redo(Range *selection) {
    if (!canRedo()) return false;
    const Step &s = m_history[m_undo++];
    for (const Edit &e : s.edits) {
        if (!e.removed.isEmpty()) {
            const QStringList parts = e.removed.split('\n');
            const Pos endPos = parts.size() == 1 ? Pos{e.at.line, e.at.col + int(parts[0].size())} : Pos{e.at.line + int(parts.size()) - 1, int(parts.last().size())};
            rawRemove({e.at, endPos});
        }
        if (!e.inserted.isEmpty()) rawInsert(e.at, e.inserted);
    }
    m_mergeOpen = false;
    if (selection) *selection = s.after;
    return true;
}

// ---- search

namespace {
QRegularExpression makeRegex(const QString &query, const TextBuffer::FindOptions &o) {
    QString pattern = o.regex ? query : QRegularExpression::escape(query);
    if (o.wholeWord) pattern = "\\b(?:" + pattern + ")\\b";
    QRegularExpression::PatternOptions opts = QRegularExpression::UseUnicodePropertiesOption;
    if (!o.caseSensitive) opts |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(pattern, opts);
}
} // namespace

bool TextBuffer::find(const QString &query, Pos from, const FindOptions &o, Range *found) const {
    if (query.isEmpty()) return false;
    from = clamp(from);
    // a query spanning lines is searched in the whole text
    if (query.contains('\n') && !o.regex) {
        const QString all = text();
        auto indexOf = [&](Pos p) { int i = 0; for (int l = 0; l < p.line; ++l) i += int(m_lines[l].size()) + 1; return i + p.col; };
        auto posOf = [&](int i) { int l = 0; while (l < lineCount() - 1 && i > m_lines[l].size()) { i -= int(m_lines[l].size()) + 1; ++l; } return Pos{l, i}; };
        const Qt::CaseSensitivity cs = o.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        int i = o.backward ? int(all.lastIndexOf(query, qMax(0, indexOf(from) - 1), cs)) : int(all.indexOf(query, indexOf(from), cs));
        if (i < 0 && o.wrap) i = o.backward ? int(all.lastIndexOf(query, -1, cs)) : int(all.indexOf(query, 0, cs));
        if (i < 0) return false;
        *found = {posOf(i), posOf(i + int(query.size()))};
        return true;
    }
    const QRegularExpression re = makeRegex(query, o);
    if (!re.isValid()) return false;
    const int n = lineCount();
    auto matchIn = [&](int line, int fromCol, int toCol, bool last, Range *r) -> bool {
        const QString &l = m_lines[line];
        bool ok = false;
        for (auto it = re.globalMatch(l); it.hasNext();) {
            const auto m = it.next();
            if (m.capturedLength() == 0) continue;
            if (m.capturedStart() < fromCol || m.capturedEnd() > toCol) { if (m.capturedStart() >= toCol) break; continue; }
            *r = {{line, int(m.capturedStart())}, {line, int(m.capturedEnd())}};
            ok = true;
            if (!last) return true;
        }
        return ok;
    };
    if (!o.backward) {
        for (int k = 0; k <= n; ++k) {
            const int line = (from.line + k) % n;
            if (k > 0 && line == 0 && !o.wrap) return false;
            if (k == n && line != from.line) break;
            const int start = k == 0 ? from.col : 0, stop = k == n ? from.col : int(m_lines[line].size());
            if (matchIn(line, start, stop, false, found)) return true;
            if (!o.wrap && line == n - 1) return false;
        }
    } else {
        for (int k = 0; k <= n; ++k) {
            const int line = ((from.line - k) % n + n) % n;
            if (k > 0 && line == n - 1 && !o.wrap) return false;
            const int start = k == n ? from.col : 0, stop = k == 0 ? from.col : int(m_lines[line].size());
            if (matchIn(line, start, stop, true, found)) return true;
            if (!o.wrap && line == 0) return false;
        }
    }
    return false;
}

int TextBuffer::countMatches(const QString &query, const FindOptions &o) const {
    if (query.isEmpty()) return 0;
    const QRegularExpression re = makeRegex(query, o);
    if (!re.isValid()) return 0;
    int n = 0;
    for (const QString &l : m_lines)
        for (auto it = re.globalMatch(l); it.hasNext();)
            if (it.next().capturedLength() > 0) ++n;
    return n;
}

int TextBuffer::replaceAll(const QString &query, const QString &with, const FindOptions &o) {
    if (query.isEmpty()) return 0;
    const QRegularExpression re = makeRegex(query, o);
    if (!re.isValid()) return 0;
    int count = 0;
    beginGroup();
    for (int i = 0; i < lineCount(); ++i) {
        const QString &l = m_lines[i];
        QString out;
        int last = 0, here = 0;
        for (auto it = re.globalMatch(l); it.hasNext();) {
            const auto m = it.next();
            if (m.capturedLength() == 0) continue;
            out += l.mid(last, m.capturedStart() - last);
            QString rep = with;
            if (o.regex)   // \1 .. \9 and $1 .. $9 refer to groups
                for (int g = 9; g >= 0; --g) rep.replace("\\" + QString::number(g), m.captured(g)).replace("$" + QString::number(g), m.captured(g));
            out += rep;
            last = int(m.capturedEnd());
            ++here;
        }
        if (!here) continue;
        out += l.mid(last);
        replace({{i, 0}, {i, int(l.size())}}, out);
        count += here;
    }
    endGroup();
    return count;
}
