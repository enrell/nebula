#include "filetree.h"
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <algorithm>

FileTree::FileTree(QObject *parent) : QObject(parent) {
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &FileTree::changed);
}

QVariantList FileTree::list(const QString &dir, bool showHidden) const {
    QDir d(dir);
    QDir::Filters f = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
    if (showHidden) f |= QDir::Hidden;
    QFileInfoList entries = d.entryInfoList(f, QDir::NoSort);
    entries.erase(std::remove_if(entries.begin(), entries.end(), [](const QFileInfo &i) { return i.fileName() == ".git"; }), entries.end());
    std::sort(entries.begin(), entries.end(), [](const QFileInfo &a, const QFileInfo &b) {
        if (a.isDir() != b.isDir()) return a.isDir();
        return QString::compare(a.fileName(), b.fileName(), Qt::CaseInsensitive) < 0;
    });
    // what git ignores in this folder (one process for the whole folder)
    QSet<QString> ignored;
    if (!entries.isEmpty()) {
        QProcess git;
        git.setWorkingDirectory(dir);
        git.start("git", {"check-ignore", "--stdin", "-z"});
        if (git.waitForStarted(1000)) {
            QByteArray in;
            for (const QFileInfo &i : std::as_const(entries)) in += i.fileName().toUtf8() + (i.isDir() ? "/" : "") + '\0';
            git.write(in);
            git.closeWriteChannel();
            if (git.waitForFinished(2000))
                for (const QByteArray &n : git.readAllStandardOutput().split('\0'))
                    if (!n.isEmpty()) ignored.insert(QString::fromUtf8(n).chopped(n.endsWith('/') ? 1 : 0));
        }
    }
    QVariantList out;
    for (const QFileInfo &i : std::as_const(entries))
        out << QVariantMap{{"name", i.fileName()}, {"path", i.absoluteFilePath()}, {"dir", i.isDir()}, {"ignored", ignored.contains(i.fileName())}};
    return out;
}

void FileTree::watch(const QStringList &dirs) {
    const QStringList now = m_watcher.directories();
    QStringList add, remove;
    for (const QString &d : dirs) if (!now.contains(d) && QFileInfo(d).isDir()) add << d;
    for (const QString &d : now) if (!dirs.contains(d)) remove << d;
    if (!remove.isEmpty()) m_watcher.removePaths(remove);
    if (!add.isEmpty()) m_watcher.addPaths(add);
}

QString FileTree::projectRoot(const QString &dir) const {
    QProcess git;
    git.setWorkingDirectory(dir);
    git.start("git", {"rev-parse", "--show-toplevel"});
    if (git.waitForFinished(1500) && git.exitCode() == 0) {
        const QString top = QString::fromUtf8(git.readAllStandardOutput()).trimmed();
        if (!top.isEmpty()) return top;
    }
    return dir;
}

QString FileTree::checkName(const QString &dir, const QString &name) const {
    const QString n = name.trimmed();
    if (n.isEmpty()) return "the name is empty";
    if (n == "." || n == ".." || n.contains('\0')) return "not a valid name";
    if (QFileInfo::exists(QDir(dir).filePath(n))) return "\"" + n + "\" already exists";
    return {};
}

QString FileTree::createFile(const QString &dir, const QString &name) {
    if (QString e = checkName(dir, name); !e.isEmpty()) return e;
    const QString path = QDir(dir).filePath(name.trimmed());   // "src/new.rs" creates the folder too
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return f.errorString();
    return {};
}

QString FileTree::createDir(const QString &dir, const QString &name) {
    if (QString e = checkName(dir, name); !e.isEmpty()) return e;
    return QDir(dir).mkpath(name.trimmed()) ? QString() : QString("cannot create the folder");
}

QString FileTree::rename(const QString &path, const QString &newName) {
    const QFileInfo fi(path);
    if (newName.trimmed() == fi.fileName()) return {};
    if (QString e = checkName(fi.absolutePath(), newName); !e.isEmpty()) return e;
    return QFile::rename(path, QDir(fi.absolutePath()).filePath(newName.trimmed())) ? QString() : QString("cannot rename");
}

QString FileTree::trash(const QString &path) {
    if (!QFileInfo::exists(path)) return "already gone";
    return QFile::moveToTrash(path) ? QString() : QString("cannot move to the trash");
}

QString FileTree::parentDir(const QString &path) const { return QFileInfo(path).absolutePath(); }
QString FileTree::join(const QString &dir, const QString &name) const { return QDir(dir).filePath(name); }
bool FileTree::isDir(const QString &path) const { return QFileInfo(path).isDir(); }
void FileTree::copyPath(const QString &path) const { QGuiApplication::clipboard()->setText(path); }
