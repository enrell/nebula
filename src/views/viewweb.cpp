#include "viewweb.h"
#include "viewfiles.h"
#include "viewpane.h"
#include <QBuffer>
#include <QFile>
#include <QMimeDatabase>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QtWebEngineQuick/QQuickWebEngineProfile>
#include <unistd.h>

namespace {

constexpr char kScheme[] = "nebula-view";

class SchemeHandler : public QWebEngineUrlSchemeHandler {
public:
    using QWebEngineUrlSchemeHandler::QWebEngineUrlSchemeHandler;
    void requestStarted(QWebEngineUrlRequestJob *job) override {
        const QUrl url = job->requestUrl();
        if (url.host() != "app" || job->requestMethod() != "GET") return job->fail(QWebEngineUrlRequestJob::RequestDenied);
        const QString path = url.path(QUrl::FullyDecoded);
        static const QHash<QString, QPair<QString, QByteArray>> assets = {
            {"/page.html", {":/nebula/renderer/page.html", "text/html"}},
            {"/page.js", {":/nebula/renderer/page.js", "text/javascript"}},
            {"/page.css", {":/nebula/renderer/page.css", "text/css"}},
            {"/qwebchannel.js", {":/qtwebchannel/qwebchannel.js", "text/javascript"}},
        };
        if (assets.contains(path)) return serve(job, assets[path].first, assets[path].second);
        // /sandbox/<view>/<block>.html: the document an `html` block runs in
        static const QString sandbox = "/sandbox/";
        if (path.startsWith(sandbox)) {
            const QStringList parts = path.mid(sandbox.size()).split('/');
            ViewPane *view = parts.size() == 2 ? ViewPane::byId(parts[0].toInt()) : nullptr;
            const QByteArray doc = view && parts[1].endsWith(".html") ? view->sandboxDocument(parts[1].chopped(5).toInt()) : QByteArray();
            if (doc.isEmpty()) return job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            auto *buf = new QBuffer(job);
            buf->setData(doc);
            buf->open(QIODevice::ReadOnly);
            return job->reply("text/html", buf);
        }
        // /files/<view>/<relative path>: only files the view's checked document declared
        static const QString prefix = "/files/";
        if (!path.startsWith(prefix)) return job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        const QString rest = path.mid(prefix.size());
        const int slash = rest.indexOf('/');
        ViewPane *view = slash > 0 ? ViewPane::byId(rest.left(slash).toInt()) : nullptr;
        const QString rel = slash > 0 ? rest.mid(slash + 1) : QString();
        if (!view || !view->allowsFile(rel)) return job->fail(QWebEngineUrlRequestJob::RequestDenied);
        const ViewFiles::Resolved r = ViewFiles::resolve(view->baseDir(), rel);
        if (!r.error.isEmpty()) return job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        serve(job, r.path, QMimeDatabase().mimeTypeForFile(r.path).name().toUtf8());
    }

private:
    // The engine reads the reply on its IO thread; a buffer owned by the job is safe there, a QFile is not.
    static void serve(QWebEngineUrlRequestJob *job, const QString &file, const QByteArray &mime) {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) return job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        auto *buf = new QBuffer(job);
        buf->setData(f.readAll());
        buf->open(QIODevice::ReadOnly);
        job->reply(mime, buf);
    }
};

class Interceptor : public QWebEngineUrlRequestInterceptor {
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        const QString s = info.requestUrl().scheme();
        if (s != kScheme && s != "data" && s != "blob") info.block(true);
    }
};

} // namespace

namespace ViewWeb {

void prepare() {
    // Chromium's sandbox cannot run as root and Chromium then refuses to start at all. View pages stay isolated
    // by their profile (no network, no files but the document's), so run them unsandboxed rather than not at all.
    if (::geteuid() == 0 && !qEnvironmentVariableIsSet("QTWEBENGINE_DISABLE_SANDBOX")) qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");

    QWebEngineUrlScheme scheme(kScheme);
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    // Not LocalScheme: Chromium then refuses the page's own subresources (scripts, styles) without a word.
    // Isolation comes from the profile instead: its interceptor only lets nebula-view:, data: and blob: through.
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::CorsEnabled);
    QWebEngineUrlScheme::registerScheme(scheme);
}

QObject *Provider::profile() const { return ViewWeb::profile(); }

QQuickWebEngineProfile *profile() {
    static QQuickWebEngineProfile *p = [] {
        auto *prof = new QQuickWebEngineProfile();   // no storage name: off the record, lives for the whole process
        prof->setHttpCacheType(QQuickWebEngineProfile::MemoryHttpCache);
        prof->setPersistentCookiesPolicy(QQuickWebEngineProfile::NoPersistentCookies);
        prof->installUrlSchemeHandler(kScheme, new SchemeHandler(prof));
        prof->setUrlRequestInterceptor(new Interceptor(prof));
        return prof;
    }();
    return p;
}

} // namespace ViewWeb
