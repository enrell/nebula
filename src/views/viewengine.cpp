#include "viewengine.h"
#include "viewfiles.h"
#include <QFile>
#include <QJSEngine>
#include <QJsonDocument>
#include <QLoggingCategory>

// The host object core.js calls back into for files. Paths are always resolved by ViewFiles.
class ViewHost : public QObject {
    Q_OBJECT
public:
    QString baseDir;
    Q_INVOKABLE QString readText(const QString &path) const { return ViewFiles::readTextJson(baseDir, path); }
    Q_INVOKABLE QString stat(const QString &path) const { return ViewFiles::statJson(baseDir, path); }
};

ViewEngine &ViewEngine::instance() {
    static ViewEngine e;
    return e;
}

ViewEngine::ViewEngine() : m_host(std::make_unique<ViewHost>()) {}
ViewEngine::~ViewEngine() = default;

bool ViewEngine::load(QString *error) {
    if (m_loaded) {
        if (!m_loadError.isEmpty()) *error = m_loadError;
        return m_loadError.isEmpty();
    }
    m_loaded = true;
    QFile f(":/nebula/renderer/core.js");
    if (!f.open(QIODevice::ReadOnly)) m_loadError = "view checker missing from this build";
    else {
        m_js = std::make_unique<QJSEngine>();
        // V4's compiler lints bundled library code ("used before its declaration"); that is not ours to fix
        // and would drown real QML warnings, so silence exactly that category while the bundle compiles.
        static QLoggingCategory::CategoryFilter previous = nullptr;
        previous = QLoggingCategory::installFilter([](QLoggingCategory *c) {
            if (qstrcmp(c->categoryName(), "qt.qml.compiler") == 0) c->setEnabled(QtWarningMsg, false);
            else if (previous) previous(c);
        });
        const QJSValue r = m_js->evaluate(QString::fromUtf8(f.readAll()), "core.js");
        QLoggingCategory::installFilter(previous);
        if (r.isError()) m_loadError = QString("view checker failed to load: %1 (line %2)").arg(r.toString()).arg(r.property("lineNumber").toInt());
        else m_js->globalObject().setProperty("nebulaHost", m_js->newQObject(m_host.get()));
    }
    if (!m_loadError.isEmpty()) *error = m_loadError;
    return m_loadError.isEmpty();
}

QString ViewEngine::call(const char *fn, const QList<QVariant> &args, QString *error) {
    if (!load(error)) return {};
    QJSValueList jsArgs;
    for (const QVariant &a : args) jsArgs << m_js->toScriptValue(a);
    const QJSValue f = m_js->globalObject().property("NebulaCore").property(QString::fromLatin1(fn));
    const QJSValue r = f.call(jsArgs);
    if (r.isError()) { *error = QString("view checker error: %1 (line %2)").arg(r.toString()).arg(r.property("lineNumber").toInt()); return {}; }
    return r.toString();
}

QJsonObject ViewEngine::check(const QString &source, const QString &baseDir) {
    QString error;
    m_host->baseDir = baseDir;
    if (!load(&error)) return {{"fatal", error}};
    const QJSValue host = m_js->globalObject().property("nebulaHost");
    const QJSValue r = m_js->globalObject().property("NebulaCore").property("checkJson").call({QJSValue(source), host});
    if (r.isError()) return {{"fatal", QString("view checker error: %1 (line %2)").arg(r.toString()).arg(r.property("lineNumber").toInt())}};
    return QJsonDocument::fromJson(r.toString().toUtf8()).object();
}

QJsonArray ViewEngine::components() {
    QString error;
    return QJsonDocument::fromJson(call("componentsJson", {}, &error).toUtf8()).array();
}

QJsonObject ViewEngine::describe(const QString &name) {
    QString error;
    return QJsonDocument::fromJson(call("describeJson", {name}, &error).toUtf8()).object();
}

#include "viewengine.moc"
