#include "procinfo.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include "win/winproc.h"
#endif

namespace ProcInfo {

#ifdef Q_OS_WIN

QString cwd(qint64 pid) {
    const QString dir = QString::fromStdWString(winproc::currentDirectory(DWORD(pid)));
    return dir.isEmpty() ? QString() : QDir::fromNativeSeparators(dir);
}

QStringList arguments(qint64 pid) {
    QStringList out;
    for (const std::wstring &a : winproc::arguments(DWORD(pid))) out << QString::fromStdWString(a);
    return out;
}

QString name(qint64 pid) {
    QString n = QString::fromStdWString(winproc::exeName(DWORD(pid)));
    if (n.endsWith(".exe", Qt::CaseInsensitive)) n.chop(4);
    return n;
}

#else

QString cwd(qint64 pid) { return QFileInfo(QString("/proc/%1/cwd").arg(pid)).symLinkTarget(); }

QStringList arguments(qint64 pid) {
    QStringList args;
    QFile f(QString("/proc/%1/cmdline").arg(pid));
    if (f.open(QIODevice::ReadOnly))
        for (const QByteArray &a : f.readAll().split('\0'))
            if (!a.isEmpty()) args << QString::fromUtf8(a);
    return args;
}

QString name(qint64 pid) {
    QFile f(QString("/proc/%1/comm").arg(pid));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readLine()).trimmed() : QString();
}

#endif

} // namespace ProcInfo
