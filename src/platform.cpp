#include "platform.h"
#include "settings.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <cstring>

namespace Platform {

namespace {

#ifdef Q_OS_WIN

// One argument on a Windows command line, the way CommandLineToArgvW and the C runtime read it back.
QString windowsArg(const QString &a) {
    if (!a.isEmpty() && !a.contains(' ') && !a.contains('\t') && !a.contains('"')) return a;
    QString out = "\"";
    int backslashes = 0;
    for (const QChar c : a) {
        if (c == '\\') { ++backslashes; continue; }
        if (c == '"') out += QString(backslashes * 2 + 1, '\\') + '"';
        else out += QString(backslashes, '\\') + c;
        backslashes = 0;
    }
    return out + QString(backslashes * 2, '\\') + '"';
}

// Runs after the user's profile (so it wraps whatever prompt the profile set up, oh-my-posh and the like) and puts
// the current directory in front of every prompt as the OSC 9;9 sequence that Windows Terminal and ConEmu use.
const char *kPowerShellHook = R"PS(
$global:__nebulaPrompt = $function:prompt
function global:prompt {
  $loc = $executionContext.SessionState.Path.CurrentLocation
  $mark = ''
  if ($loc.Provider.Name -eq 'FileSystem') { $mark = "$([char]27)]9;9;`"$($loc.ProviderPath)`"$([char]27)\" }
  $mark + (& $global:__nebulaPrompt)
}
)PS";

QString encodedCommand(const QString &script) {
    const QByteArray utf16(reinterpret_cast<const char *>(script.utf16()), qsizetype(script.size() * 2));
    return QString::fromLatin1(utf16.toBase64());
}

#endif

QString baseName(const QString &program) {
    QString n = QFileInfo(program).fileName().toLower();
    for (const char *ext : {".exe", ".cmd", ".bat"})
        if (n.endsWith(ext)) n.chop(int(strlen(ext)));
    return n;
}

} // namespace

QString defaultShell() {
#ifdef Q_OS_WIN
    for (const char *name : {"pwsh", "powershell"}) {
        const QString exe = QStandardPaths::findExecutable(name);
        if (!exe.isEmpty()) return exe;
    }
    return qEnvironmentVariable("COMSPEC", "cmd.exe");
#else
    const QString sh = qEnvironmentVariable("SHELL");
    return sh.isEmpty() ? QString("/bin/sh") : sh;
#endif
}

ShellKind shellKind(const QString &shell) {
    const QString n = baseName(shell.isEmpty() ? defaultShell() : shell);
    if (n == "pwsh" || n == "powershell") return ShellKind::PowerShell;
    if (n == "cmd") return ShellKind::Cmd;
    return ShellKind::Posix;
}

ShellKind paneShellKind() {
    return shellKind(Settings::instance() ? Settings::instance()->shell() : QString());
}

QString shellCommandLine(const QString &shell) {
#ifdef Q_OS_WIN
    const QString wanted = shell.isEmpty() ? defaultShell() : shell;
    // the setting is a path (which may hold spaces) or a command with arguments ("pwsh -NoProfile")
    QStringList parts = QFileInfo(wanted).isFile() ? QStringList{wanted} : QProcess::splitCommand(wanted);
    if (parts.isEmpty()) parts << defaultShell();
    parts[0] = QDir::toNativeSeparators(program(parts[0]));
    if (shellKind(parts[0]) == ShellKind::PowerShell && parts.size() == 1)
        parts << "-NoLogo" << "-NoExit" << "-EncodedCommand" << encodedCommand(QString::fromLatin1(kPowerShellHook));
    QStringList quoted;
    for (const QString &p : std::as_const(parts)) quoted << windowsArg(p);
    return quoted.join(' ');
#else
    return shell.isEmpty() ? defaultShell() : shell;
#endif
}

QString quote(const QString &arg, ShellKind kind) {
    QString r = arg;
    switch (kind) {
    case ShellKind::Cmd:
        r.replace('"', "\"\"");
        return "\"" + r + "\"";
    case ShellKind::PowerShell:
        r.replace('\'', "''");
        return "'" + r + "'";
    case ShellKind::Posix:
        break;
    }
    r.replace('\'', "'\\''");
    return "'" + r + "'";
}

QString quote(const QString &arg) { return quote(arg, paneShellKind()); }

QString program(const QString &name) {
#ifdef Q_OS_WIN
    if (name.isEmpty() || name.contains('/') || name.contains('\\') || name.contains(':')) return name;
    const QString found = QStandardPaths::findExecutable(name);   // follows PATHEXT: claude.exe, claude.cmd, ...
    return found.isEmpty() ? name : found;
#else
    return name;
#endif
}

void notify(const QString &title, const QString &body) {
#ifdef Q_OS_WIN
    // A toast through Windows PowerShell 5.1, which can reach the Windows Runtime (PowerShell 7 cannot). The text
    // travels in the environment, so nothing needs escaping. The app id is PowerShell's, which Windows knows.
    static const char *script = R"PS(
[Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime] | Out-Null
$xml = [Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent([Windows.UI.Notifications.ToastTemplateType]::ToastText02)
$text = $xml.GetElementsByTagName('text')
$text.Item(0).AppendChild($xml.CreateTextNode($env:NEBULA_NOTIFY_TITLE)) | Out-Null
$text.Item(1).AppendChild($xml.CreateTextNode($env:NEBULA_NOTIFY_BODY)) | Out-Null
$app = '{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\WindowsPowerShell\v1.0\powershell.exe'
[Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier($app).Show([Windows.UI.Notifications.ToastNotification]::new($xml))
)PS";
    const QString ps = QStandardPaths::findExecutable("powershell");
    if (ps.isEmpty()) return;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("NEBULA_NOTIFY_TITLE", title);
    env.insert("NEBULA_NOTIFY_BODY", body);
    QProcess p;
    p.setProgram(ps);
    p.setArguments({"-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden", "-EncodedCommand", encodedCommand(QString::fromLatin1(script))});
    p.setProcessEnvironment(env);
    p.startDetached();
#else
    QProcess::startDetached("notify-send", {"-a", "nebula", "-i", "utilities-terminal", title, body});
#endif
}

} // namespace Platform
