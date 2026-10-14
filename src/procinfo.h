#pragma once
#include <QString>
#include <QStringList>

// What nebula reads about other processes: /proc on Linux, the process environment block on Windows.
namespace ProcInfo {

QString cwd(qint64 pid);              // the working directory (forward slashes); empty when unknown
QStringList arguments(qint64 pid);    // argv, the program first
QString name(qint64 pid);             // the program's name: comm on Linux, the executable without ".exe" on Windows

} // namespace ProcInfo
