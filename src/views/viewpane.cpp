#include "viewpane.h"
#include "paths.h"
#include "theme.h"
#include "viewengine.h"
#include "viewfiles.h"
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <cmath>

static QHash<int, ViewPane *> &registry() { static QHash<int, ViewPane *> r; return r; }
static QPointer<Theme> s_theme;

static QString viewsDir() {
    const QString d = Paths::stateDir() + "/views";
    QDir().mkpath(d);
    return d;
}

ViewPane::ViewPane(int id, QObject *parent) : QObject(parent), m_id(id) {
    registry().insert(id, this);
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(120);
    connect(&m_reloadTimer, &QTimer::timeout, this, &ViewPane::reload);
    // editors replace files on save, which drops them from the watcher: reload, then watch again
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_reloadTimer, qOverload<>(&QTimer::start));
    if (s_theme) connect(s_theme, &Theme::changed, this, &ViewPane::themeChanged);
}

ViewPane::~ViewPane() { registry().remove(m_id); }

ViewPane *ViewPane::byId(int id) { return registry().value(id); }

void ViewPane::setTheme(Theme *theme) { s_theme = theme; }

QString ViewPane::inlinePath() const { return viewsDir() + QString("/%1.nebula.md").arg(m_id); }

QJsonObject ViewPane::setSource(const Source &src) {
    m_src = src;
    if (m_src.file.isEmpty()) {
        QSaveFile f(inlinePath());
        if (f.open(QIODevice::WriteOnly)) { f.write(m_src.content.toUtf8()); f.commit(); }
    } else {
        m_src.content.clear();
    }
    reload();
    return report();
}

void ViewPane::reload() {
    QJsonObject r;
    QString source = m_src.content;
    if (!m_src.file.isEmpty()) {
        QFile f(m_src.file);
        if (f.open(QIODevice::ReadOnly)) source = QString::fromUtf8(f.readAll());
        else r = {{"fatal", QString("cannot read %1").arg(m_src.file)}};
    }
    if (r.isEmpty()) r = ViewEngine::instance().check(source, m_src.baseDir);
    m_fatal = r["fatal"].toString();
    m_diagnostics = r["diagnostics"].toArray();
    m_refs.clear();
    for (const QJsonValue &v : r["refs"].toArray()) m_refs.insert(v.toString());
    m_inputs.clear();
    for (const QJsonValue &v : r["inputs"].toArray()) m_inputs << v.toString();
    QJsonArray blocks = r["blocks"].toArray();
    if (!m_fatal.isEmpty())
        blocks = {QJsonObject{{"index", 0}, {"type", "markdown"}, {"line", 1}, {"ok", false},
                              {"errors", QJsonArray{QJsonObject{{"line", 1}, {"message", m_fatal}}}}}};
    // the page heading comes from the document's front matter only; Source::title names the pane
    m_doc = {{"title", r["title"]}, {"blocks", blocks}, {"refs", r["refs"]}, {"bibliography", r["bibliography"]}};
    m_renderIssues.clear();
    ++m_generation;
    rewatch();
    emit documentChanged();
    emit changed();
}

void ViewPane::rewatch() {
    if (!m_watcher.files().isEmpty()) m_watcher.removePaths(m_watcher.files());
    if (m_src.file.isEmpty()) return;
    // everything the checker read (data files, bibliographies, scripts), not only what the page loads
    QStringList paths{m_src.file};
    for (const QString &rel : std::as_const(m_inputs)) {
        const ViewFiles::Resolved r = ViewFiles::resolve(m_src.baseDir, rel);
        if (r.error.isEmpty()) paths << r.path;
    }
    for (const QString &p : std::as_const(paths))
        if (QFileInfo::exists(p)) m_watcher.addPath(p);
}

QString ViewPane::title() const {
    if (!m_src.title.isEmpty()) return m_src.title;
    const QString t = m_doc["title"].toString();
    if (!t.isEmpty()) return t;
    return m_src.file.isEmpty() ? QString("view") : QFileInfo(m_src.file).fileName();
}

int ViewPane::errorCount() const {
    int n = m_fatal.isEmpty() ? 0 : 1;
    for (const QJsonValue &d : m_diagnostics) n += d["severity"].toString() == "error";
    return n;
}

QJsonObject ViewPane::report() const {
    QJsonArray errors, warnings;
    if (!m_fatal.isEmpty()) errors << QJsonObject{{"line", 1}, {"message", m_fatal}};
    for (const QJsonValue &d : m_diagnostics) {
        QJsonObject o = d.toObject();
        const bool isError = o.take("severity").toString() == "error";
        o.remove("block");
        (isError ? errors : warnings) << o;
    }
    return {{"view", m_id}, {"title", title()}, {"source", m_src.file.isEmpty() ? QString("inline") : m_src.file},
            {"ok", errors.isEmpty()}, {"blocks", m_doc["blocks"].toArray().size()}, {"errors", errors}, {"warnings", warnings},
            {"rendered", m_renderedGeneration == m_generation}, {"renderIssues", QJsonArray::fromStringList(m_renderIssues)}};
}

QJsonObject ViewPane::toJson() const {
    QJsonObject o{{"baseDir", m_src.baseDir}, {"title", m_src.title}};
    if (m_src.file.isEmpty()) o["inline"] = true;
    else o["file"] = m_src.file;
    return o;
}

ViewPane *ViewPane::restore(int id, const QJsonObject &o, QObject *parent) {
    auto *v = new ViewPane(id, parent);
    Source src{o["file"].toString(), {}, o["baseDir"].toString(), o["title"].toString()};
    if (o["inline"].toBool()) {
        QFile f(v->inlinePath());
        if (f.open(QIODevice::ReadOnly)) src.content = QString::fromUtf8(f.readAll());
    }
    v->m_src = src;
    v->reload();
    return v;
}

void ViewPane::discard() {
    if (m_src.file.isEmpty()) QFile::remove(inlinePath());
}

QJsonObject ViewPane::themeJson() const {
    if (!s_theme) return {};
    const auto c = [](const char *prop) { return s_theme->property(prop).value<QColor>().name(); };
    QJsonObject colors;
    for (const char *k : {"bg", "fg", "panel", "border", "muted", "accent", "red", "green", "yellow", "blue", "selection"}) colors[k] = c(k);
    // same pixel size the QML chrome uses for text (theme.fontSize * 1.33)
    return {{"colors", colors}, {"fontFamily", s_theme->fontFamily()}, {"fontSize", std::round(s_theme->fontSize() * 1.33)}};
}

QString ViewPane::fileBase() const { return QString("nebula-view://app/files/%1/").arg(m_id); }
QString ViewPane::sandboxBase() const { return QString("nebula-view://app/sandbox/%1/").arg(m_id); }

// An `html` block runs in its own document, loaded from nebula-view:// into an iframe with sandbox="allow-scripts"
// (opaque origin: no access to the view page, the bridge, storage or cookies). It cannot be srcdoc: that would
// inherit the view page's strict CSP. Its own CSP allows inline code but no network source at all.
QByteArray ViewPane::sandboxDocument(int blockIndex) const {
    QString html;
    bool found = false;
    for (const QJsonValue &b : m_doc["blocks"].toArray())
        if (b["index"].toInt() == blockIndex && b["component"].toString() == "html" && b["ok"].toBool()) {
            html = b["props"]["html"].toString();
            found = true;
        }
    if (!found) return {};
    static const QString csp = "default-src 'none'; script-src 'unsafe-inline' 'unsafe-eval' blob:; style-src 'unsafe-inline'; "
                               "img-src data: blob:; font-src data:; media-src data: blob:; worker-src blob:; connect-src 'none'; form-action 'none'";
    const QJsonObject t = themeJson();
    QString vars;
    const QJsonObject colors = t["colors"].toObject();
    for (auto it = colors.begin(); it != colors.end(); ++it) vars += QString("--%1:%2;").arg(it.key(), it.value().toString());
    vars += QString("--font:\"%1\", monospace;--font-size:%2px;").arg(t["fontFamily"].toString()).arg(t["fontSize"].toDouble());
    const QString head = QString("<meta charset=\"utf-8\"><meta http-equiv=\"Content-Security-Policy\" content=\"%1\">"
                                 "<style>:root{%2color-scheme:dark}html,body{margin:0;background:var(--bg);color:var(--fg);font:var(--font-size)/1.5 var(--font)}canvas,svg{display:block}</style>"
                                 "<script>addEventListener('error',e=>parent.postMessage({nebulaIssue:e.message+(e.lineno?' (line '+e.lineno+')':'')},'*'));"
                                 "addEventListener('unhandledrejection',e=>parent.postMessage({nebulaIssue:'unhandled rejection: '+((e.reason&&e.reason.message)||e.reason)},'*'));</script>")
                             .arg(csp, vars);
    static const QRegularExpression headTag("<head(\\s[^>]*)?>", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = headTag.match(html);
    const QString doc = m.hasMatch() ? QString(html).insert(m.capturedEnd(), head)
                                     : "<!doctype html><html><head>" + head + "</head><body>" + html + "</body></html>";
    return doc.toUtf8();
}

QString ViewPane::pageUrl() const { return "nebula-view://app/page.html"; }

void ViewPane::rendered(const QString &reportJson) {
    m_pageAttached = true;
    const QJsonObject r = QJsonDocument::fromJson(reportJson.toUtf8()).object();
    for (const QJsonValue &v : r["issues"].toArray())
        if (!m_renderIssues.contains(v.toString())) m_renderIssues << v.toString();
    m_renderedGeneration = m_generation;
    emit renderFinished(m_generation);
}

void ViewPane::reportIssue(const QString &message) {
    if (!m_renderIssues.contains(message) && m_renderIssues.size() < 50) m_renderIssues << message;
}

void ViewPane::openLink(const QString &url) {
    const QUrl u(url);
    if (u.scheme() == "http" || u.scheme() == "https" || u.scheme() == "mailto") QDesktopServices::openUrl(u);
}

void ViewPane::pageDetached() { m_pageAttached = false; }

int ViewPane::startExport(const QString &format, const QString &path) {
    const int request = m_nextRequest++;
    m_exportPaths.insert(request, path);
    emit exportRequested(request, format, path);
    return request;
}

void ViewPane::exportHtml(int request, const QString &html) {
    const QString path = m_exportPaths.take(request);
    if (path.isEmpty()) return;
    QSaveFile f(path);
    const bool ok = f.open(QIODevice::WriteOnly) && f.write(html.toUtf8()) >= 0 && f.commit();
    emit taskFinished(request, ok ? QJsonObject{{"ok", true}, {"path", path}, {"bytes", QFileInfo(path).size()}}
                                  : QJsonObject{{"ok", false}, {"error", QString("cannot write %1").arg(path)}});
}

void ViewPane::readyToPrint(int request) {
    if (m_exportPaths.contains(request)) emit printRequested(request, m_exportPaths.value(request));
}

void ViewPane::pdfFinished(int request, bool ok) {
    emit printFinished();
    const QString path = m_exportPaths.take(request);
    if (path.isEmpty()) return;
    emit taskFinished(request, ok ? QJsonObject{{"ok", true}, {"path", path}, {"bytes", QFileInfo(path).size()}}
                                  : QJsonObject{{"ok", false}, {"error", QString("printing to %1 failed").arg(path)}});
}

void ViewPane::exportFailed(int request, const QString &error) {
    if (m_exportPaths.remove(request)) emit printFinished();   // drop print stills if a PDF export was on its way
    emit taskFinished(request, {{"ok", false}, {"error", error}});
}

int ViewPane::startSnapshot(int block) {
    const int request = m_nextRequest++;
    emit snapshotRequested(request, block);
    return request;
}

void ViewPane::readyForSnapshot(int request) { emit grabRequested(request); }

void ViewPane::snapshotRegion(int request, QQuickWindow *window, const QRectF &rect) {
    QImage img;
    if (window) {
        const QImage whole = window->grabWindow();
        const qreal dpr = whole.devicePixelRatio();
        img = whole.copy(QRectF(rect.topLeft() * dpr, rect.size() * dpr).toAlignedRect().intersected(whole.rect()));
    }
    if (img.isNull()) { emit taskFinished(request, {{"ok", false}, {"error", "the view could not be captured"}}); return; }
    m_snapshots.insert(request, img);
    emit taskFinished(request, {{"ok", true}});
}
