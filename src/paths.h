#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace Paths {
// The address of the API socket. A file in the runtime directory on Linux; on Windows the name of a named pipe
// (\\.\pipe\<name>), which cannot hold slashes, so whatever NEBULA_SOCKET holds is made into a name.
inline QString socket() {
    QByteArray env = qgetenv("NEBULA_SOCKET");
    if (env.isEmpty()) env = qgetenv("AINEBULA_SOCKET");   // pre-rename compat
#ifdef Q_OS_WIN
    QString name = env.isEmpty() ? "nebula-" + qEnvironmentVariable("USERNAME", "user") : QString::fromLocal8Bit(env);
    for (QChar &c : name)
        if (!c.isLetterOrNumber() && c != '-' && c != '_' && c != '.') c = '_';
    return name;
#else
    if (!env.isEmpty()) return QString::fromLocal8Bit(env);
    QString dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (dir.isEmpty()) dir = QDir::tempPath();
    return dir + "/nebula.sock";
#endif
}
inline QString hostSocket() { return socket() + ".host"; }
inline QString stateDir() {
    QByteArray env = qgetenv("NEBULA_STATE_DIR");
    if (env.isEmpty()) env = qgetenv("AINEBULA_STATE_DIR");
#ifdef Q_OS_WIN
    const QString local = qEnvironmentVariable("LOCALAPPDATA", QDir::homePath() + "/AppData/Local");
    const QString fallback = QDir::fromNativeSeparators(local) + "/nebula/state";
#else
    const QString fallback = QDir::homePath() + "/.local/state/nebula";
#endif
    QString dir = env.isEmpty() ? fallback : QString::fromLocal8Bit(env);
    QDir().mkpath(dir);
    return dir;
}
// The executable to re-launch for `--host`, `mcp` and hook commands.
inline QString selfExe() { return QCoreApplication::applicationFilePath(); }
// settings.json, agents.conf: ~/.config/nebula, or %APPDATA%\nebula on Windows
inline QString configDir() {
#ifdef Q_OS_WIN
    return QDir::fromNativeSeparators(qEnvironmentVariable("APPDATA", QDir::homePath() + "/AppData/Roaming")) + "/nebula";
#else
    return QDir::homePath() + "/.config/nebula";
#endif
}

// One-time upgrade: move ~/.config|state/ainebula -> nebula when the new dir does not exist yet (Linux only).
inline void migrateOldDirs() {
#ifndef Q_OS_WIN
    QDir home(QDir::homePath());
    if (!QDir(home.absoluteFilePath(".config/nebula")).exists() && QDir(home.absoluteFilePath(".config/ainebula")).exists())
        home.rename(".config/ainebula", ".config/nebula");
    if (!QDir(home.absoluteFilePath(".local/state/nebula")).exists() && QDir(home.absoluteFilePath(".local/state/ainebula")).exists()) {
        QDir().mkpath(home.absoluteFilePath(".local/state"));
        QDir(home.absoluteFilePath(".local/state")).rename("ainebula", "nebula");
    }
#endif
}
} // namespace Paths
