#include "viewfiles.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

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

QString hashJson(const QString &baseDir, const QString &relative) {
    const Resolved r = resolve(baseDir, relative);
    if (!r.error.isEmpty()) return fail(r.error);
    QFile f(r.path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!f.open(QIODevice::ReadOnly) || !hash.addData(&f)) return fail(QString("cannot read %1").arg(relative));
    const QString modified = QFileInfo(r.path).lastModified().toUTC().toString(Qt::ISODate);
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"ok", true}, {"sha256", QString::fromLatin1(hash.result().toHex())}, {"size", r.size},
                                                       {"modified", modified}}).toJson(QJsonDocument::Compact));
}

static QByteArray readSmall(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.read(1 << 20).trimmed() : QByteArray();
}

QString gitJson(const QString &baseDir) {
    // the nearest .git upwards: a directory, or a file "gitdir: <path>" (worktrees, submodules)
    QString gitDir;
    for (QDir d(QFileInfo(baseDir).canonicalFilePath()); !d.path().isEmpty(); ) {
        const QFileInfo g(d.filePath(".git"));
        if (g.isDir()) { gitDir = g.filePath(); break; }
        if (g.isFile()) {
            const QByteArray line = readSmall(g.filePath());
            if (line.startsWith("gitdir:")) gitDir = QDir(d).absoluteFilePath(QString::fromUtf8(line.mid(7).trimmed()));
            break;
        }
        if (!d.cdUp()) break;
    }
    if (gitDir.isEmpty()) return "{}";
    // a linked worktree keeps its refs in the common directory
    QString common = gitDir;
    if (const QByteArray c = readSmall(gitDir + "/commondir"); !c.isEmpty()) common = QDir(gitDir).absoluteFilePath(QString::fromUtf8(c));
    const QByteArray head = readSmall(gitDir + "/HEAD");
    QJsonObject o;
    static const QRegularExpression sha("^[0-9a-f]{40}([0-9a-f]{24})?$");
    if (head.startsWith("ref: ")) {
        const QString ref = QString::fromUtf8(head.mid(5).trimmed());
        o["branch"] = QString(ref).remove(QRegularExpression("^refs/heads/"));
        QByteArray commit = readSmall(gitDir + '/' + ref);
        if (commit.isEmpty()) commit = readSmall(common + '/' + ref);
        if (commit.isEmpty())
            for (const QByteArray &line : readSmall(common + "/packed-refs").split('\n'))
                if (line.endsWith(' ' + ref.toUtf8())) { commit = line.left(line.indexOf(' ')); break; }
        if (sha.match(QString::fromLatin1(commit)).hasMatch()) o["commit"] = QString::fromLatin1(commit);
    } else if (sha.match(QString::fromLatin1(head)).hasMatch()) {
        o["commit"] = QString::fromLatin1(head);
    }
    if (!o.contains("commit")) return "{}";   // an unborn branch has no commit yet
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

} // namespace ViewFiles
