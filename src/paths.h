#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace Paths {
inline QString socket() {
    QByteArray env = qgetenv("NEBULA_SOCKET");
    if (env.isEmpty()) env = qgetenv("AINEBULA_SOCKET");   // pre-rename compat
    if (!env.isEmpty()) return QString::fromLocal8Bit(env);
    QString dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (dir.isEmpty()) dir = QDir::tempPath();
    return dir + "/nebula.sock";
}
inline QString hostSocket() { return socket() + ".host"; }
inline QString stateDir() {
    QByteArray env = qgetenv("NEBULA_STATE_DIR");
    if (env.isEmpty()) env = qgetenv("AINEBULA_STATE_DIR");
    QString dir = env.isEmpty() ? QDir::homePath() + "/.local/state/nebula" : QString::fromLocal8Bit(env);
    QDir().mkpath(dir);
    return dir;
}
// The executable to re-launch for `--host`, `mcp` and hook commands.
inline QString selfExe() { return QCoreApplication::applicationFilePath(); }
inline QString configDir() { return QDir::homePath() + "/.config/nebula"; }

// One-time upgrade: move ~/.config|state/ainebula -> nebula when the new dir does not exist yet.
inline void migrateOldDirs() {
    QDir home(QDir::homePath());
    if (!QDir(home.absoluteFilePath(".config/nebula")).exists() && QDir(home.absoluteFilePath(".config/ainebula")).exists())
        home.rename(".config/ainebula", ".config/nebula");
    if (!QDir(home.absoluteFilePath(".local/state/nebula")).exists() && QDir(home.absoluteFilePath(".local/state/ainebula")).exists()) {
        QDir().mkpath(home.absoluteFilePath(".local/state"));
        QDir(home.absoluteFilePath(".local/state")).rename("ainebula", "nebula");
    }
}
} // namespace Paths
