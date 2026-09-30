#include "viewfiles.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace ViewFiles {

Resolved resolve(const QString &baseDir, const QString &relative) {
    Resolved r;
    if (relative.isEmpty()) { r.error = "empty path"; return r; }
    if (QDir::isAbsolutePath(relative) || relative.startsWith('~')) {
        r.error = QString("path must be relative to the document's directory (got %1)").arg(relative);
        return r;
    }
    const QString base = QFileInfo(baseDir).canonicalFilePath();
    if (base.isEmpty()) { r.error = QString("the document's directory %1 does not exist").arg(baseDir); return r; }
    const QString joined = QDir::cleanPath(base + '/' + relative);
    if (!joined.startsWith(base + '/')) { r.error = QString("%1 is outside the document's directory").arg(relative); return r; }
    const QFileInfo fi(joined);
    if (!fi.exists()) { r.error = QString("file not found: %1").arg(relative); return r; }
    const QString canonical = fi.canonicalFilePath();
    if (!canonical.startsWith(base + '/')) { r.error = QString("%1 links outside the document's directory").arg(relative); return r; }
    if (!fi.isFile()) { r.error = QString("%1 is not a file").arg(relative); return r; }
    if (!fi.isReadable()) { r.error = QString("%1 is not readable").arg(relative); return r; }
    r.path = canonical;
    r.size = fi.size();
    return r;
}

static QString fail(const QString &error) {
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"ok", false}, {"error", error}}).toJson(QJsonDocument::Compact));
}

QString readTextJson(const QString &baseDir, const QString &relative) {
    const Resolved r = resolve(baseDir, relative);
    if (!r.error.isEmpty()) return fail(r.error);
    if (r.size > kMaxTextBytes) return fail(QString("%1 is too large (%2 MB, max %3 MB)").arg(relative).arg(r.size / 1048576).arg(kMaxTextBytes / 1048576));
    QFile f(r.path);
    if (!f.open(QIODevice::ReadOnly)) return fail(QString("cannot read %1").arg(relative));
    const QByteArray data = f.readAll();
    if (data.contains('\0')) return fail(QString("%1 is a binary file").arg(relative));
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"ok", true}, {"text", QString::fromUtf8(data)}}).toJson(QJsonDocument::Compact));
}

QString readBase64Json(const QString &baseDir, const QString &relative) {
    const Resolved r = resolve(baseDir, relative);
    if (!r.error.isEmpty()) return fail(r.error);
    if (r.size > kMaxBinaryBytes) return fail(QString("%1 is too large (%2 MB, max %3 MB)").arg(relative).arg(r.size / 1048576).arg(kMaxBinaryBytes / 1048576));
    QFile f(r.path);
    if (!f.open(QIODevice::ReadOnly)) return fail(QString("cannot read %1").arg(relative));
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"ok", true}, {"base64", QString::fromLatin1(f.readAll().toBase64())}}).toJson(QJsonDocument::Compact));
}

QString statJson(const QString &baseDir, const QString &relative) {
    const Resolved r = resolve(baseDir, relative);
    if (!r.error.isEmpty()) return fail(r.error);
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"ok", true}, {"size", r.size}}).toJson(QJsonDocument::Compact));
}

} // namespace ViewFiles
