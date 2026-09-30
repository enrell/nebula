#pragma once
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

class Theme;

// A pane that shows a view document (Markdown + nebula components) instead of a terminal.
// It owns the source (a file it watches, or inline content kept in the state dir), the checked document and
// both kinds of feedback: diagnostics from the checker and issues the page reports while drawing.
// The page talks to it over QWebChannel (the `document`, `theme`, `fileBase` properties and the slots).
class ViewPane : public QObject {
    Q_OBJECT
    Q_PROPERTY(int id READ id CONSTANT)
    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(QString label READ title NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString agentState READ agentState CONSTANT)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY changed)
    Q_PROPERTY(QJsonObject document READ document NOTIFY documentChanged)
    Q_PROPERTY(QJsonObject theme READ themeJson NOTIFY themeChanged)
    Q_PROPERTY(QString fileBase READ fileBase CONSTANT)
    Q_PROPERTY(QString sandboxBase READ sandboxBase CONSTANT)
    Q_PROPERTY(QString pageUrl READ pageUrl CONSTANT)
public:
    struct Source {
        QString file;       // a document on disk (watched), or empty for inline content
        QString content;    // inline content (stored in the state dir so the view survives restarts)
        QString baseDir;    // relative references resolve here
        QString title;      // pane title; the document's front matter title is used when empty
    };

    ViewPane(int id, QObject *parent = nullptr);
    ~ViewPane() override;
    static ViewPane *byId(int id);
    static void setTheme(Theme *theme);

    // Replaces the source and re-checks it. Returns the report (see report()).
    QJsonObject setSource(const Source &src);
    QJsonObject report() const;          // {view, title, source, ok, blocks, errors, warnings, rendered, renderIssues}
    QJsonObject toJson() const;          // for session.json
    static ViewPane *restore(int id, const QJsonObject &o, QObject *parent);
    void discard();                      // the pane is closed for good: drop stored inline content

    int id() const { return m_id; }
    QString kind() const { return "view"; }
    QString title() const;
    QString agentState() const { return {}; }
    int errorCount() const;
    QJsonObject document() const { return m_doc; }
    QJsonObject themeJson() const;
    QString fileBase() const;
    QString sandboxBase() const;
    QByteArray sandboxDocument(int blockIndex) const;   // the page an `html` block runs in (see ViewWeb)
    QString pageUrl() const;
    QString baseDir() const { return m_src.baseDir; }
    QString paneTitle() const { return m_src.title; }
    int anchor() const { return m_anchor; }          // the pane that asked for the view (docking goes next to it)
    void setAnchor(int pane) { m_anchor = pane; }
    bool allowsFile(const QString &relative) const { return m_refs.contains(relative); }
    int generation() const { return m_generation; }
    bool pageAttached() const { return m_pageAttached; }

    Q_INVOKABLE void pageDetached();              // called by QML when its view item goes away

public slots:
    // page -> nebula (QWebChannel)
    void rendered(const QString &reportJson);
    void reportIssue(const QString &message);
    void openLink(const QString &url);

signals:
    void changed();
    void documentChanged();
    void themeChanged();
    void renderFinished(int generation);

private:
    void reload();
    void rewatch();
    QString inlinePath() const;

    int m_id;
    Source m_src;
    QJsonObject m_doc;            // what the page draws: {title, blocks}
    QJsonArray m_diagnostics;
    QString m_fatal;
    QSet<QString> m_refs;
    QStringList m_renderIssues;
    int m_generation = 0, m_renderedGeneration = -1;
    int m_anchor = -1;
    bool m_pageAttached = false;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
};
