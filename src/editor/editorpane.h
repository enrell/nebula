#pragma once
#include "editor/highlighter.h"
#include "editor/textbuffer.h"
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

class QKeyEvent;

// A pane that edits a text file. It owns the text, the cursor and selection, and every editing command, so the
// keyboard (EditorView) and the automation API (pane.send_keys, editor.*) go through the same code. Unsaved text is
// kept with the session: closing the window, a reboot or Kill all & quit never lose it.
class EditorPane : public QObject {
    Q_OBJECT
    Q_PROPERTY(int id READ id CONSTANT)
    Q_PROPERTY(QString label READ label NOTIFY changed)          // the pane title (file name, • when modified)
    Q_PROPERTY(QString agentState READ agentState CONSTANT)      // PaneFrame asks every pane; editors have none
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString fileName READ fileName NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY changed)
    Q_PROPERTY(bool previewable READ previewable NOTIFY changed)   // Markdown / .neb: can be shown as a view
    Q_PROPERTY(QString languageName READ languageName NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY cursorChanged)    // "Ln 3, Col 5   UTF-8   LF   C++"
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool changedOnDisk READ changedOnDisk NOTIFY messageChanged)
    Q_PROPERTY(bool closeRequested READ closeRequested NOTIFY closeRequestedChanged)
    Q_PROPERTY(int cursorLine READ cursorLine NOTIFY cursorChanged)
    Q_PROPERTY(int cursorColumn READ cursorColumn NOTIFY cursorChanged)
    Q_PROPERTY(QString selectedText READ selectedText NOTIFY cursorChanged)
public:
    using Pos = TextBuffer::Pos;
    using Range = TextBuffer::Range;

    explicit EditorPane(int id, QObject *parent = nullptr);
    ~EditorPane() override;
    bool open(const QString &path, QString *error = nullptr);
    static EditorPane *restore(int id, const QJsonObject &o, QObject *parent);
    QJsonObject toJson() const;   // also writes unsaved text next to the session
    void discard();               // forget unsaved text kept for this pane (it is being closed for good)
    QString baseDir() const;

    int id() const { return m_id; }
    QString label() const;
    QString agentState() const { return {}; }
    QString path() const { return m_path; }
    QString fileName() const;
    bool dirty() const { return m_buf.modified(); }
    bool readOnly() const { return m_readOnly; }
    bool previewable() const;
    QString languageName() const { return Highlighter::displayName(m_hl.language()); }
    QString language() const { return m_hl.language(); }
    QString status() const;
    QString message() const { return m_message; }
    bool changedOnDisk() const { return m_changedOnDisk; }
    bool closeRequested() const { return m_closeRequested; }
    int cursorLine() const { return m_cursor.line + 1; }
    int cursorColumn() const { return visualColumn(m_cursor.line, m_cursor.col) + 1; }
    QString selectedText() const { return m_buf.text({m_anchor, m_cursor}); }

    TextBuffer &buffer() { return m_buf; }
    const TextBuffer &buffer() const { return m_buf; }
    Pos cursor() const { return m_cursor; }
    Pos anchor() const { return m_anchor; }
    Range selection() const { return Range{m_anchor, m_cursor}.normalized(); }
    bool hasSelection() const { return m_anchor != m_cursor; }
    void setCursor(Pos p, bool keepAnchor);
    void setSelection(Pos anchor, Pos cursor);
    void selectWordAt(Pos p);
    void selectLine(int line);

    // what the view needs to draw a line: colours (computed incrementally), tab stops, wide characters
    QVector<Highlighter::Span> spans(int line);
    int tabWidth() const { return m_tabWidth; }
    int visualColumn(int line, int col) const;     // display column of a code unit, tabs expanded
    int columnAt(int line, int visual) const;      // the code unit at a display column (nearest boundary)
    static int charWidth(char32_t c);              // 0, 1 or 2 display columns

    // where the view is scrolled (kept here so the pane can move between layouts without jumping)
    int scrollLine = 0, scrollColumn = 0;

    // Editing. keyPress handles everything an editor does with a key (`pageLines` is the visible height).
    bool keyPress(QKeyEvent *e, int pageLines = 20);
    void injectKey(int key, int qtMods, const QString &text);   // pane.send_keys
    void insertText(const QString &text);                       // typing, IME, pane.send_text
    Q_INVOKABLE void paste(const QString &text);
    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString &path);
    Q_INVOKABLE void reload();                   // from disk, dropping unsaved changes
    Q_INVOKABLE void keepMine();                 // the file changed on disk, but keep this text
    Q_INVOKABLE void dismissMessage() { if (!m_changedOnDisk) setMessage({}); }
    Q_INVOKABLE void gotoLine(int line, int column = 1);
    Q_INVOKABLE void requestClose();             // asks the user (when modified) before closing
    Q_INVOKABLE void cancelClose();
    Q_INVOKABLE void resolveClose(bool saveFirst);   // the answer to requestClose: save then close, or discard

    // find & replace (the find bar)
    Q_INVOKABLE bool find(const QString &query, bool backward, bool caseSensitive, bool wholeWord, bool regex);
    Q_INVOKABLE bool replaceNext(const QString &query, const QString &with, bool caseSensitive, bool wholeWord, bool regex);
    Q_INVOKABLE int replaceAll(const QString &query, const QString &with, bool caseSensitive, bool wholeWord, bool regex);
    Q_INVOKABLE int countMatches(const QString &query, bool caseSensitive, bool wholeWord, bool regex) const;

signals:
    void changed();             // title, dirty, path, language
    void textChanged();         // any edit (the view repaints, a linked preview refreshes)
    void cursorChanged();
    void messageChanged();
    void closeRequestedChanged();
    void ensureCursorVisible();
    void findRequested(const QString &mode, const QString &text);   // "find" | "replace" | "goto"
    void closeConfirmed();      // the user chose save or discard: the workspace closes the pane
    void saved();

private:
    void edit(const std::function<void()> &f);   // records the selection around an edit for undo
    void moveCursor(Pos p, bool select);
    Pos wordLeft(Pos p) const;
    Pos wordRight(Pos p) const;
    void newline();
    void backspace(bool word);
    void deleteForward(bool word);
    void indent(bool outdent);
    void toggleComment();
    void moveLines(int delta);
    void duplicate();
    void deleteLines();
    QString indentUnit() const;
    void detectIndent();
    void setMessage(const QString &m);
    void watch();
    void onFileChanged();
    void bufferChanged(int first, int removed, int added);
    QString unsavedPath() const;
    bool m_wasDirty = false;

    int m_id;
    QString m_path, m_message;
    TextBuffer m_buf;
    Highlighter m_hl;
    QVector<int> m_states;   // the highlighter state at the start of each line
    int m_statesValid = 0;   // m_states is right for lines [0, m_statesValid]
    Pos m_cursor, m_anchor;
    int m_wantVisual = -1;   // the column up/down keep to across short lines
    int m_tabWidth = 4;
    bool m_useTabs = false, m_readOnly = false, m_changedOnDisk = false, m_closeRequested = false;
    QFileSystemWatcher m_watcher;
    QTimer m_rewatch;
    quint64 m_lastRevision = 0;
};
