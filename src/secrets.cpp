#include "secrets.h"
#include "paths.h"
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#include "win/winsecret.h"
#endif

namespace {
QHash<QString, QString> &cache() {
    static QHash<QString, QString> c;
    return c;
}
QString key(const QString &p, const QString &v) { return p + "\x1f" + v; }

// the system's secret store: the Credential Manager on Windows, the Secret Service (secret-tool) elsewhere
bool useKeyring() {
#ifdef Q_OS_WIN
    static const bool ok = qEnvironmentVariable("NEBULA_SECRETS") != "file";
#else
    static const bool ok = qEnvironmentVariable("NEBULA_SECRETS") != "file" && !QStandardPaths::findExecutable("secret-tool").isEmpty();
#endif
    return ok;
}

#ifdef Q_OS_WIN
std::wstring target(const QString &p, const QString &v) { return QString("nebula/%1/%2").arg(p, v).toStdWString(); }
#endif

QString filePath() { return Paths::configDir() + "/secrets.json"; }

QJsonObject readFile() {
    QFile f(filePath());
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

void writeFile(const QJsonObject &o) {
    QDir().mkpath(Paths::configDir());
    QFile f(filePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

QStringList attrs(const QString &p, const QString &v) { return {"service", "nebula", "profile", p, "var", v}; }
} // namespace

namespace SecretStore {

QString backend() { return useKeyring() ? "keyring" : "file"; }   // on Windows the Credential Manager

bool set(const QString &profile, const QString &var, const QString &value) {
    bool ok = true;
#ifdef Q_OS_WIN
    if (useKeyring()) {
        ok = winsecret::set(target(profile, var), value.toUtf8().toStdString());
    } else
#endif
    if (useKeyring()) {
        QProcess p;
        p.start("secret-tool", QStringList{"store", "--label", "nebula " + profile + "/" + var} + attrs(profile, var));
        if (!p.waitForStarted(2000)) return false;
        p.write(value.toUtf8());
        p.closeWriteChannel();
        ok = p.waitForFinished(5000) && p.exitCode() == 0;
    } else {
        QJsonObject o = readFile();
        o[key(profile, var)] = value;
        writeFile(o);
    }
    if (ok) cache().insert(key(profile, var), value);
    return ok;
}

QString get(const QString &profile, const QString &var) {
    const QString k = key(profile, var);
    if (cache().contains(k)) return cache().value(k);
    QString v;
#ifdef Q_OS_WIN
    if (useKeyring()) {
        std::string raw;
        if (winsecret::get(target(profile, var), &raw)) v = QString::fromUtf8(raw.data(), qsizetype(raw.size()));
    } else
#endif
    if (useKeyring()) {
        QProcess p;
        p.start("secret-tool", QStringList{"lookup"} + attrs(profile, var));
        if (p.waitForFinished(4000) && p.exitCode() == 0) v = QString::fromUtf8(p.readAllStandardOutput());
        if (v.isEmpty()) {   // entries stored before the ainebula -> nebula rename
            QProcess old;
            old.start("secret-tool", QStringList{"lookup", "service", "ainebula", "profile", profile, "var", var});
            if (old.waitForFinished(4000) && old.exitCode() == 0) v = QString::fromUtf8(old.readAllStandardOutput());
        }
    } else {
        v = readFile().value(k).toString();
    }
    if (!v.isEmpty()) cache().insert(k, v);
    return v;
}

void remove(const QString &profile, const QString &var) {
    cache().remove(key(profile, var));
#ifdef Q_OS_WIN
    if (useKeyring()) {
        winsecret::remove(target(profile, var));
    } else
#endif
    if (useKeyring()) {
        QProcess p;
        p.start("secret-tool", QStringList{"clear"} + attrs(profile, var));
        p.waitForFinished(4000);
    } else {
        QJsonObject o = readFile();
        o.remove(key(profile, var));
        writeFile(o);
    }
}

bool has(const QString &profile, const QString &var) { return !get(profile, var).isEmpty(); }

} // namespace SecretStore
