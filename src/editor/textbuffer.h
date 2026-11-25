#pragma once
#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

// The text of an editor: the file's lines without their terminators (a final newline is an empty last line), plus
// what the file looked like on disk (BOM, CRLF) so saving writes back exactly the same bytes. Columns are UTF-16 code units.
// Every change goes through insert/remove and is recorded for undo; consecutive typing merges into one step.
// QtCore only, unit-tested in tests/test_editor.cpp.
class TextBuffer {
public:
    struct Pos {
        int line = 0, col = 0;
        bool operator==(const Pos &o) const { return line == o.line && col == o.col; }
        bool operator!=(const Pos &o) const { return !(*this == o); }
        bool operator<(const Pos &o) const { return line < o.line || (line == o.line && col < o.col); }
        bool operator<=(const Pos &o) const { return !(o < *this); }
    };
    struct Range {
        Pos a, b;   // a <= b once normalized
        Range normalized() const { return b < a ? Range{b, a} : *this; }
        bool empty() const { return a == b; }
    };

    TextBuffer();

    // Loading refuses what an editor cannot round-trip: a NUL byte or bytes that are not UTF-8 mean "binary".
    bool load(const QString &path, QString *error = nullptr);
    bool loadBytes(const QByteArray &data, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr);   // atomic (QSaveFile), keeps the file's permissions
    QByteArray bytes() const;                                   // what save writes
    void setText(const QString &text);                          // replaces everything, as one undo step

    int lineCount() const { return int(m_lines.size()); }
    const QString &line(int i) const { return m_lines[i]; }
    QString text() const;
    QString text(Range r) const;
    Pos end() const { return {lineCount() - 1, int(m_lines.last().size())}; }
    Pos clamp(Pos p) const;

    // Edits return where the inserted text ends. `cursorBefore`/`cursorAfter` are restored by undo/redo.
    Pos insert(Pos at, const QString &text);
    void remove(Range r);
    Pos replace(Range r, const QString &text);

    // Several edits that undo as one (indent a selection, replace all). Nested groups join the outer one.
    void beginGroup();
    void endGroup();
    void breakMerge() { m_mergeOpen = false; }   // the next edit starts a new undo step

    bool canUndo() const { return m_undo > 0; }
    bool canRedo() const { return m_undo < m_history.size(); }
    // return where the cursor goes (the selection the step was made from), or false when there is nothing to do
    bool undo(Range *selection);
    bool redo(Range *selection);
    // the editor's selection around an edit: what undo returns to, and what redo returns to
    void setSelectionBefore(Range r) { m_hintBefore = r; }
    void setSelectionAfter(Range r);

    bool modified() const { return m_undo != m_savedAt; }
    void markSaved() { m_savedAt = m_undo; m_mergeOpen = false; }
    quint64 revision() const { return m_revision; }

    // Searching. A zero-length match is never returned. `wrap` continues from the other end.
    struct FindOptions { bool caseSensitive = false, wholeWord = false, regex = false, backward = false, wrap = true; };
    bool find(const QString &query, Pos from, const FindOptions &o, Range *found) const;
    int replaceAll(const QString &query, const QString &with, const FindOptions &o);
    int countMatches(const QString &query, const FindOptions &o) const;

    bool crlf() const { return m_crlf; }
    bool bom() const { return m_bom; }
    bool finalNewline() const { return m_lines.size() > 1 && m_lines.last().isEmpty(); }

    // Called after every change with the first line touched, how many lines were there and how many are now, so
    // caches (the highlighter's per-line state) can be updated instead of rebuilt.
    std::function<void(int first, int removed, int added)> onChanged;

private:
    struct Edit {
        Pos at;
        QString removed, inserted;
    };
    struct Step {
        QVector<Edit> edits;
        Range before, after;
    };
    Pos rawInsert(Pos at, const QString &text);
    void rawRemove(Range r);
    void record(const Edit &e);
    void notify(int first, int removed, int added);

    QStringList m_lines{QString()};
    QVector<Step> m_history;
    int m_undo = 0, m_savedAt = 0, m_group = 0;
    bool m_mergeOpen = false, m_crlf = false, m_bom = false;
    QElapsedTimer m_lastEdit;
    Range m_hintBefore;
    quint64 m_revision = 0;
};
