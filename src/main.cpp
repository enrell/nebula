#include "apiserver.h"
#include "cli.h"
#include "host.h"
#include "integrations.h"
#include "launcher.h"
#include "llm.h"
#include "mcp.h"
#include "operator.h"
#include "profiles.h"
#include "paths.h"
#include "settings.h"
#include "terminalview.h"
#include "theme.h"
#include "views/viewpane.h"
#include "views/viewweb.h"
#include "workspace.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QGuiApplication>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

// Desktops running fcitx5/ibus export QT_IM_MODULE for the system Qt, but our bundled Qt may not ship that
// plugin; Qt then silently drops to the bare compose context and dead keys / layout-aware input break.
// On Wayland the compositor relays text-input-v3 to the same input method, so use that instead.
static void fixInputMethod() {
    const QByteArray im = qgetenv("QT_IM_MODULE");
    if (im.isEmpty() || im == "wayland" || im == "compose" || !qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) return;
    const QByteArray qpa = qgetenv("QT_QPA_PLATFORM");
    if (!qpa.isEmpty() && !qpa.startsWith("wayland")) return;
    QStringList dirs = qEnvironmentVariable("QT_PLUGIN_PATH").split(':', Qt::SkipEmptyParts);
    dirs << QLibraryInfo::path(QLibraryInfo::PluginsPath)
         << QFileInfo(QFileInfo("/proc/self/exe").canonicalFilePath()).dir().filePath("../plugins");
    const QString name = QString::fromLocal8Bit(im).toLower();
    for (const QString &d : dirs)
        for (const QString &f : QDir(d + "/platforminputcontexts").entryList(QDir::Files))
            if (f.contains(name)) return;
    qputenv("QT_IM_MODULE", "wayland");
}

int main(int argc, char *argv[]) {
    if (argc > 1 && QByteArray(argv[1]) == "ctl") {
        QCoreApplication core(argc, argv);
        return runCtl(core.arguments().mid(2));
    }
    if (argc > 1 && QByteArray(argv[1]) == "--host") return runHost();
    if (argc > 1 && QByteArray(argv[1]) == "mcp") return runMcp();
    if (argc > 1 && (QByteArray(argv[1]) == "--help" || QByteArray(argv[1]) == "-h")) {
        std::puts("usage: nebula            start the app (shells survive closing the window)\n       nebula ctl ...    control a running instance (nebula ctl --help)\n       nebula --host     run the pty host daemon (started automatically)");
        return 0;
    }

    Paths::migrateOldDirs();

    // views: the scheme must be known before the web engine starts, and the engine before the application
    ViewWeb::prepare();
    QtWebEngineQuick::initialize();
    fixInputMethod();
    QGuiApplication app(argc, argv);
    app.setApplicationName("nebula");
    app.setApplicationVersion(NEBULA_VERSION);
    app.setDesktopFileName("nebula");

    {
        QLocalSocket probe;
        probe.connectToServer(Paths::socket());
        if (probe.waitForConnected(200)) {
            std::fprintf(stderr, "nebula: already running (%s). Set NEBULA_SOCKET to start a separate instance.\n", qPrintable(Paths::socket()));
            return 1;
        }
    }

    Settings settings;
    Theme theme(&settings);
    TerminalView::setTheme(&theme);
    ViewPane::setTheme(&theme);
    Profiles profiles;
    Llm llm;
    Integrations integrations;
    Workspace workspace(&theme);
    Launcher launcher(&workspace);
    ApiServer api(&workspace, &launcher);
    Operator op(&api);
    api.setOperator(&op);
    if (!api.listen(Paths::socket())) std::fprintf(stderr, "nebula: warning: cannot listen on %s\n", qPrintable(Paths::socket()));

    ViewWeb::Provider viewWeb;   // before the engine: QML bindings use it until the engine is gone
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("settings", &settings);
    engine.rootContext()->setContextProperty("app", &workspace);
    engine.rootContext()->setContextProperty("profiles", &profiles);
    engine.rootContext()->setContextProperty("llm", &llm);
    engine.rootContext()->setContextProperty("integrations", &integrations);
    engine.rootContext()->setContextProperty("launcher", &launcher);
    engine.rootContext()->setContextProperty("operatorAgent", &op);
    engine.rootContext()->setContextProperty("viewWeb", &viewWeb);
    // Dev mode: NEBULA_QML_DIR=/path/to/qml loads the UI from disk and reloads it whenever a .qml file changes,
    // so UI iteration needs no rebuild at all.
    const QString qmlDir = qEnvironmentVariable("NEBULA_QML_DIR");
    const bool devQml = !qmlDir.isEmpty();
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [devQml] { if (!devQml) QCoreApplication::exit(1); }, Qt::QueuedConnection);
    workspace.restore();
    if (workspace.spaceList().isEmpty()) return 1;
    if (!devQml) {
        engine.loadFromModule("Nebula", "Main");
        return app.exec();
    }
    const QUrl mainUrl = QUrl::fromLocalFile(QDir(qmlDir).absoluteFilePath("Main.qml"));
    QFileSystemWatcher watcher;
    QTimer debounce;
    debounce.setSingleShot(true);
    debounce.setInterval(120);
    const auto reload = [&] {
        for (QObject *o : engine.rootObjects()) o->deleteLater();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        engine.clearComponentCache();
        engine.load(mainUrl);
        // editors save via rename, which drops the watch: re-add every file
        for (const QString &f : QDir(qmlDir).entryList({"*.qml"}, QDir::Files)) watcher.addPath(QDir(qmlDir).absoluteFilePath(f));
        watcher.addPath(qmlDir);
        std::fprintf(stderr, "nebula: qml reloaded\n");
    };
    QObject::connect(&debounce, &QTimer::timeout, &app, reload);
    QObject::connect(&watcher, &QFileSystemWatcher::fileChanged, &debounce, qOverload<>(&QTimer::start));
    QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged, &debounce, qOverload<>(&QTimer::start));
    reload();
    return app.exec();
}
