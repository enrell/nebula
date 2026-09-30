#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <memory>

class QJSEngine;
class ViewHost;

// Runs the document checker (renderer/dist/core.js, compiled into the binary) in a QJSEngine. It is the one
// place a view document is parsed and validated; the page only draws what check() returns. Loaded lazily on
// first use, lives on the thread that created it.
class ViewEngine {
public:
    static ViewEngine &instance();
    ~ViewEngine();

    // {title, blocks, diagnostics:[{severity,line,component,path,message,hint}], refs:[relative paths]}
    // or {fatal: message} if the checker itself failed.
    QJsonObject check(const QString &source, const QString &baseDir);
    QJsonArray components();                    // [{name, summary}]
    QJsonObject describe(const QString &name);  // {name, summary, schema, shorthand, example}, empty if unknown

private:
    ViewEngine();
    bool load(QString *error);
    QString call(const char *fn, const QList<QVariant> &args, QString *error);

    std::unique_ptr<QJSEngine> m_js;
    std::unique_ptr<ViewHost> m_host;
    QString m_loadError;
    bool m_loaded = false;
};
