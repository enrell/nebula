#pragma once
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

// Syntax colours for the editor, one line at a time: a line's spans depend only on its text and the state the
// previous line ended in (inside a block comment, a multi-line string, a Markdown code fence...), so the editor
// keeps one state per line and only re-colours from an edit down until the states agree again. QtCore only,
// unit-tested in tests/test_editor.cpp.
class Highlighter {
public:
    enum Kind : quint8 { Plain, Keyword, Type, String, Comment, Number, Function, Preproc, Heading, Emphasis, Link, Code, Constant, Tag };
    struct Span {
        int start, length;
        Kind kind;
    };

    // "cpp", "rust", "python", "markdown"... from the file name, else from a shebang; "" means plain text
    static QString languageFor(const QString &path, const QString &firstLine = QString());
    static QString displayName(const QString &language);

    explicit Highlighter(const QString &language = QString());
    QString language() const { return m_lang; }
    // The spans of `line` (plain text between them), given the state the previous line ended in. Returns this line's
    // end state. State 0 is "nothing open".
    int highlight(const QString &line, int state, QVector<Span> *out) const;
    // the state the first line starts in (Markdown: "document start", where --- opens front matter)
    int initialState() const { return m_lang == "markdown" ? -1 : 0; }
    // what a line comment starts with ("//", "#", "--"), for toggle comment; "" when the language has none
    QString lineComment() const { return m_lineComments.isEmpty() ? QString() : m_lineComments.first(); }

private:
    enum State { Normal = 0, BlockComment = 1, Triple2 = 2, Triple1 = 3, Template = 4, Fence = 5, FrontMatter = 6 };
    int highlightMarkdown(const QString &line, int state, QVector<Span> *out) const;
    QString m_lang;
    QStringList m_lineComments;
    QString m_blockStart, m_blockEnd;
    QSet<QString> m_keywords, m_types, m_constants;
    bool m_hashPreproc = false, m_triple = false, m_template = false, m_capsAreTypes = false, m_shellVars = false,
         m_keysBeforeColon = false, m_sections = false, m_tags = false, m_singleQuoteChars = false, m_caseInsensitive = false;
};
