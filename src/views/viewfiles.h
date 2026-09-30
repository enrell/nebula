#pragma once
#include <QString>

// The only way view documents reach the file system. A document may reference files by path relative to its
// base directory (the directory of the document file, or the agent's working directory for inline content);
// absolute paths, `..` escapes and symlinks leading outside the base directory are refused.
namespace ViewFiles {

struct Resolved {
    QString path;    // canonical absolute path, empty on error
    QString error;   // message for the document author
    qint64 size = 0;
};

Resolved resolve(const QString &baseDir, const QString &relative);

constexpr qint64 kMaxTextBytes = 5 * 1024 * 1024;

// JSON strings handed to the checker: {"ok":true,"text":...} / {"ok":true,"size":N} / {"ok":false,"error":...}
QString readTextJson(const QString &baseDir, const QString &relative);
QString statJson(const QString &baseDir, const QString &relative);

} // namespace ViewFiles
