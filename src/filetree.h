#pragma once
#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QVariantList>

// The file explorer's view of the disk: one directory at a time (the panel asks for the folders it shows open),
// what git ignores marked so it can be dimmed, and the few file operations an explorer needs. Watches the open
// folders and says which one changed, so the tree follows files created by agents, builds or the shell.
class FileTree : public QObject {
    Q_OBJECT
public:
    explicit FileTree(QObject *parent = nullptr);

    // [{name, path, dir, ignored}] - folders first, then files, case-insensitive; .git is never shown
    Q_INVOKABLE QVariantList list(const QString &dir, bool showHidden) const;
    Q_INVOKABLE void watch(const QStringList &dirs);   // the folders currently shown open
    Q_INVOKABLE QString projectRoot(const QString &dir) const;   // the git work tree containing dir, else dir

    // file operations; each returns "" or an error to show
    Q_INVOKABLE QString createFile(const QString &dir, const QString &name);
    Q_INVOKABLE QString createDir(const QString &dir, const QString &name);
    Q_INVOKABLE QString rename(const QString &path, const QString &newName);
    Q_INVOKABLE QString trash(const QString &path);      // to the desktop trash, so it can be undone
    Q_INVOKABLE QString parentDir(const QString &path) const;
    Q_INVOKABLE QString join(const QString &dir, const QString &name) const;
    Q_INVOKABLE bool isDir(const QString &path) const;
    Q_INVOKABLE void copyPath(const QString &path) const;

signals:
    void changed(const QString &dir);

private:
    QString checkName(const QString &dir, const QString &name) const;
    QFileSystemWatcher m_watcher;
};
