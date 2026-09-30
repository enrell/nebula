#include "apiserver.h"
#include "integrations.h"
#include "launcher.h"
#include "llm.h"
#include "operator.h"
#include "profiles.h"
#include "settings.h"
#include "terminalsession.h"
#include "workspace.h"
#include "paneids.h"
#include "views/viewengine.h"
#include "views/viewpane.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QGuiApplication>
#include <QLocalSocket>
#include <QPointer>
#include <memory>
#include <utility>
#include <QQuickWindow>
#include <stdexcept>

namespace {
struct ApiError : std::runtime_error { using std::runtime_error::runtime_error; };

struct KeySpec { int key; int mods; QString text; };

KeySpec parseKey(const QString &spec) {
    static const QHash<QString, int> names = {
        {"enter", Qt::Key_Return}, {"return", Qt::Key_Return}, {"esc", Qt::Key_Escape}, {"escape", Qt::Key_Escape},
        {"tab", Qt::Key_Tab}, {"space", Qt::Key_Space}, {"bs", Qt::Key_Backspace}, {"backspace", Qt::Key_Backspace},
        {"up", Qt::Key_Up}, {"down", Qt::Key_Down}, {"left", Qt::Key_Left}, {"right", Qt::Key_Right},
        {"home", Qt::Key_Home}, {"end", Qt::Key_End}, {"pgup", Qt::Key_PageUp}, {"pageup", Qt::Key_PageUp},
        {"pgdown", Qt::Key_PageDown}, {"pagedown", Qt::Key_PageDown}, {"delete", Qt::Key_Delete}, {"del", Qt::Key_Delete},
        {"insert", Qt::Key_Insert}};
    int mods = 0;
    QStringList parts = spec.split('+');
    QString last = parts.takeLast();
    if (last.isEmpty() && !parts.isEmpty()) { last = "+"; parts.removeLast(); }
    for (const QString &m : std::as_const(parts)) {
        const QString l = m.toLower();
        if (l == "ctrl" || l == "control") mods |= Qt::ControlModifier;
        else if (l == "alt") mods |= Qt::AltModifier;
        else if (l == "shift") mods |= Qt::ShiftModifier;
        else throw ApiError("unknown modifier in key '" + spec.toStdString() + "'");
    }
    const QString l = last.toLower();
    if (names.contains(l)) return {names[l], mods, l == "space" ? " " : QString()};
    if (l.size() >= 2 && l[0] == 'f' && l.mid(1).toInt() >= 1 && l.mid(1).toInt() <= 24) return {Qt::Key_F1 + l.mid(1).toInt() - 1, mods, {}};
    if (last.size() == 1) {
        const QChar c = last[0];
        const int key = c.toUpper().unicode();
        return {key, mods, (mods & (Qt::ControlModifier | Qt::AltModifier)) ? QString() : QString(c)};
    }
    throw ApiError("unknown key '" + spec.toStdString() + "'");
}
} // namespace

ApiServer::ApiServer(Workspace *ws, Launcher *launcher, QObject *parent) : QObject(parent), m_ws(ws), m_launcher(launcher) {
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, &ApiServer::onConnection);
    connect(ws, &Workspace::agentEvent, this, &ApiServer::broadcast);
}

ApiServer::~ApiServer() {
    for (QLocalSocket *c : m_clients.keys()) c->disconnect(this);
}

bool ApiServer::listen(const QString &path) {
    QLocalServer::removeServer(path);
    return m_server.listen(path);
}

void ApiServer::onConnection() {
    while (QLocalSocket *sock = m_server.nextPendingConnection()) {
        m_clients.insert(sock, {});
        connect(sock, &QLocalSocket::readyRead, this, [this, sock] { onData(sock); });
        connect(sock, &QLocalSocket::disconnected, this, [this, sock] { m_clients.remove(sock); sock->deleteLater(); });
    }
}

void ApiServer::onData(QLocalSocket *sock) {
    auto it = m_clients.find(sock);
    if (it == m_clients.end()) return;
    it->buf += sock->readAll();
    int nl;
    while ((nl = it->buf.indexOf('\n')) >= 0) {
        const QByteArray line = it->buf.left(nl).trimmed();
        it->buf.remove(0, nl + 1);
        if (line.isEmpty()) continue;
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        QJsonObject resp;
        if (err.error != QJsonParseError::NoError || !doc.isObject()) resp = {{"ok", false}, {"error", "invalid JSON request"}};
        else resp = handle(doc.object(), sock);
        if (resp.contains("__deferred")) { it = m_clients.find(sock); if (it == m_clients.end()) return; continue; }
        sock->write(QJsonDocument(resp).toJson(QJsonDocument::Compact) + '\n');
        it = m_clients.find(sock);
        if (it == m_clients.end()) return;
    }
}

void ApiServer::broadcast(const QJsonObject &ev) {
    const QByteArray data = QJsonDocument(ev).toJson(QJsonDocument::Compact) + '\n';
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it)
        if (it->subscribed) it.key()->write(data);
}

QJsonObject ApiServer::handle(const QJsonObject &req, QLocalSocket *sock) {
    QJsonObject resp;
    if (req.contains("id")) resp["id"] = req["id"];
    const QString m = req["method"].toString();
    if (m == "llm.ask" || m == "profile.test" || m == "operator.ask") {
        startAsync(req, sock);
        return {{"__deferred", true}};
    }
    try {
        if (m == "view.show") {
            ViewPane *v = nullptr;
            const QJsonObject r = showView(req["params"].toObject(), &v);
            if (v && waitForRender(v, req, sock, r)) return {{"__deferred", true}};
            resp["result"] = r;
        } else {
            resp["result"] = call(req["method"].toString(), req["params"].toObject(), sock);
        }
        resp["ok"] = true;
    } catch (const std::exception &e) {
        resp["ok"] = false;
        resp["error"] = QString::fromUtf8(e.what());
    }
    return resp;
}

void ApiServer::startAsync(const QJsonObject &req, QLocalSocket *sock) {
    const QJsonObject p = req["params"].toObject();
    const QString m = req["method"].toString();
    if (m == "operator.ask") {
        const QString a = p["approve"].toString("none");
        const int oid = m_operator->ask(p["prompt"].toString(), a == "all" ? Operator::ApproveAll : Operator::DenyAll);
        QPointer<QLocalSocket> guard(sock);
        const QJsonValue rid = req["id"];
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(m_operator, &Operator::finished, this, [guard, rid, oid, conn](int id, const QString &text, const QString &error) {
            if (id != oid) return;
            QObject::disconnect(*conn);
            if (!guard) return;
            QJsonObject r;
            if (!rid.isUndefined()) r["id"] = rid;
            r["ok"] = error.isEmpty();
            if (error.isEmpty()) r["result"] = text; else r["error"] = error;
            guard->write(QJsonDocument(r).toJson(QJsonDocument::Compact) + '\n');
        });
        return;
    }
    QVariantMap opts{{"profile", p["profile"].toString()}, {"model", p["model"].toString()}};
    int id;
    if (m == "profile.test") {
        opts["profile"] = p["name"].toString();
        id = Llm::instance()->ask("test", "Reply with the single word OK.", "ping", opts);
    } else {
        id = Llm::instance()->ask("api", p["system"].toString("You are a helpful assistant."), p["prompt"].toString(), opts);
    }
    replyLater(sock, req["id"], id, m == "llm.ask");
}

void ApiServer::replyLater(QLocalSocket *sock, const QJsonValue &reqId, int llmId, bool wantText) {
    QPointer<QLocalSocket> guard(sock);
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(Llm::instance(), &Llm::finished, this, [guard, reqId, llmId, wantText, conn](int id, const QString &, const QString &text, const QString &error) {
        if (id != llmId) return;
        QObject::disconnect(*conn);
        if (!guard) return;
        QJsonObject r;
        if (!reqId.isUndefined()) r["id"] = reqId;
        r["ok"] = error.isEmpty();
        if (error.isEmpty()) r["result"] = wantText ? QJsonValue(text) : QJsonValue(true);
        else r["error"] = error;
        guard->write(QJsonDocument(r).toJson(QJsonDocument::Compact) + '\n');
    });
}

// view.show: create a view (or replace the source of an existing one) and return its checker report.
QJsonObject ApiServer::showView(const QJsonObject &p, ViewPane **out) {
    const bool hasFile = p.contains("file"), hasContent = p.contains("content");
    if (hasFile == hasContent) throw ApiError("give exactly one of file= or content=");
    // the caller's pane: where the view goes and whose directory relative paths start from
    int anchor = p["pane"].toInt(-1);
    if (anchor > 0 && !m_ws->tabOfPane(anchor)) throw ApiError("no such pane");
    if (anchor <= 0) {
        Space *s = m_ws->currentSpace();
        Tab *t = s ? s->currentTab() : nullptr;
        if (!t) throw ApiError("no focused pane");
        anchor = t->focusedId();
    }
    QString cwd = p["cwd"].toString();
    if (cwd.isEmpty()) {
        if (TerminalSession *t = m_ws->findPane(anchor)) cwd = t->cwd();
        else if (ViewPane *av = m_ws->findView(anchor)) cwd = av->baseDir();
        if (cwd.isEmpty()) cwd = QDir::homePath();
    }
    if (!QFileInfo(cwd).isDir()) throw ApiError("cwd is not a directory");

    ViewPane::Source src;
    src.title = p["title"].toString();
    if (hasFile) {
        const QFileInfo fi(QDir(cwd).absoluteFilePath(p["file"].toString()));
        if (!fi.isFile()) throw ApiError(("file not found: " + p["file"].toString()).toStdString());
        src.file = fi.canonicalFilePath();
        src.baseDir = fi.canonicalPath();
    } else {
        src.content = p["content"].toString();
        src.baseDir = QFileInfo(cwd).canonicalFilePath();
    }

    ViewPane *v = nullptr;
    if (p.contains("view")) {
        v = m_ws->findView(p["view"].toInt());
        if (!v) throw ApiError("no such view (it may have been closed; omit view= to open a new one)");
        if (!p.contains("title")) src.title = v->paneTitle();
        const QJsonObject r = v->setSource(src);
        *out = v;
        return r;
    }
    const QString where = p["where"].toString(Settings::instance()->viewPlacement());
    if (!Settings::viewPlacements().contains(where)) throw ApiError("where must be modal, right, down or tab");
    v = new ViewPane(PaneIds::next());
    const QJsonObject r = v->setSource(src);
    if (where == "modal") m_ws->showModal(v, anchor);
    else if (!m_ws->placeView(v, anchor, where)) { delete v; throw ApiError("cannot place the view"); }
    *out = v;
    return r;
}

// A view that is on screen reports back after drawing; include that in the reply (page-side issues such as an
// image that failed to decode). Returns false when there is nothing to wait for.
bool ApiServer::waitForRender(ViewPane *v, const QJsonObject &req, QLocalSocket *sock, const QJsonObject &report) {
    int si = -1, ti = -1;
    Tab *t = nullptr;
    m_ws->findView(v->id(), &si, &ti, &t);
    const bool onScreen = m_ws->isModal(v) ? m_ws->modalVisible() && m_ws->modalView() == v
                                           : si == m_ws->currentIndex() && t && t == m_ws->currentSpace()->currentTab();
    if (!v->pageAttached() && !onScreen) return false;
    QPointer<QLocalSocket> guard(sock);
    QPointer<ViewPane> view(v);
    const QJsonValue rid = req["id"];
    const int gen = v->generation();
    auto *wait = new QObject(this);   // owns the connections and the timeout; gone once the reply is sent
    auto sent = std::make_shared<bool>(false);
    auto finish = [guard, view, rid, report, wait, sent] {
        if (std::exchange(*sent, true)) return;
        if (guard) {
            QJsonObject r;
            if (!rid.isUndefined()) r["id"] = rid;
            r["ok"] = true;
            r["result"] = view ? view->report() : report;
            guard->write(QJsonDocument(r).toJson(QJsonDocument::Compact) + '\n');
        }
        wait->deleteLater();
    };
    connect(v, &ViewPane::renderFinished, wait, [finish, gen](int g) { if (g >= gen) finish(); });
    connect(v, &QObject::destroyed, wait, finish);
    // the first page of a session starts the web engine; after that a render takes milliseconds
    QTimer::singleShot(v->pageAttached() ? 2000 : 8000, wait, finish);
    return true;
}

TerminalSession *ApiServer::pane(const QJsonObject &p) const {
    TerminalSession *s = p.contains("pane") ? m_ws->findPane(p["pane"].toInt()) : m_ws->focusedPane();
    if (!s) throw ApiError(p.contains("pane") ? "no such pane" : "no focused pane");
    return s;
}

QJsonObject ApiServer::paneInfo(TerminalSession *s) const {
    int si = -1, ti = -1;
    Tab *tab = nullptr;
    m_ws->findPane(s->id(), &si, &ti, &tab);
    return {{"id", s->id()}, {"space", si}, {"tab", ti}, {"label", s->label()}, {"title", s->title()}, {"cwd", s->cwd()},
            {"branch", s->branch()}, {"agent", s->agent()}, {"state", s->agentState()},
            {"focused", tab && tab->focusedId() == s->id()}, {"active", s->active()}, {"profile", s->profile()}, {"summary", s->summary()}};
}

QJsonValue ApiServer::call(const QString &method, const QJsonObject &p, QLocalSocket *sock) {
    const auto &spaces = m_ws->spaceList();
    auto spaceAt = [&](const QJsonObject &o) -> Space * {
        const int i = o.contains("space") ? o["space"].toInt() : m_ws->currentIndex();
        if (i < 0 || i >= spaces.size()) throw ApiError("no such space");
        return spaces[i];
    };

    if (method == "ping") return "pong";
    if (method == "events.subscribe") { m_clients[sock].subscribed = true; return true; }
    if (method == "status") {
        int panes = 0;
        for (Space *s : spaces) for (Tab *t : s->tabList()) panes += int(t->sessions().size());
        return QJsonObject{{"version", QCoreApplication::applicationVersion()}, {"socket", path()}, {"spaces", int(spaces.size())},
                           {"panes", panes}, {"current_space", m_ws->currentIndex()}};
    }
    if (method == "settings.get") return Settings::instance()->toJson();
    if (method == "settings.set") {
        if (!Settings::instance()->set(p["key"].toString(), p["value"].toVariant())) throw ApiError("unknown setting or invalid value");
        return true;
    }
    if (method == "window.resize") {
        for (QWindow *tw : QGuiApplication::topLevelWindows())
            if (qobject_cast<QQuickWindow *>(tw)) { tw->resize(p["width"].toInt(1280), p["height"].toInt(760)); return true; }
        throw ApiError("no window");
    }
    if (method == "window.screenshot") {
        QQuickWindow *w = nullptr;
        for (QWindow *tw : QGuiApplication::topLevelWindows())
            if ((w = qobject_cast<QQuickWindow *>(tw))) break;
        if (!w) throw ApiError("no window");
        const QString file = p["path"].toString("/tmp/nebula.png");
        if (!w->grabWindow().save(file)) throw ApiError("cannot write " + file.toStdString());
        return file;
    }
    if (method == "profile.list") {
        QJsonArray a;
        for (const QString &n : Profiles::instance()->names()) a << Profiles::instance()->describe(n);
        return a;
    }
    if (method == "profile.set") {
        if (!Profiles::instance()->save(p["name"].toString(), p["provider"].toString(), p["key"].toString(), p["env"].toObject().toVariantMap()))
            throw ApiError("invalid profile (name/provider) or the secret store refused the key");
        return true;
    }
    if (method == "profile.remove") { Profiles::instance()->remove(p["name"].toString()); return true; }
    if (method == "profile.default") { Profiles::instance()->setDefault(p["name"].toString()); return true; }
    if (method == "space.set_profile") { spaceAt(p); m_ws->setSpaceProfile(p.contains("space") ? p["space"].toInt() : m_ws->currentIndex(), p["profile"].toString()); return true; }
    if (method == "agent.presets") return QJsonArray::fromVariantList(m_launcher->presets());
    if (method == "agent.launch") {
        const QVariantMap r = m_launcher->launch(p.toVariantMap());
        if (!r["ok"].toBool()) throw ApiError(r["error"].toString().toStdString());
        return QJsonObject::fromVariantMap(r);
    }
    if (method == "agent.broadcast") {
        return QJsonObject{{"sent", m_launcher->broadcast(p["panes"].toArray().toVariantList(), p["text"].toString(), p["enter"].toBool(true))}};
    }
    if (method == "integration.list") return QJsonArray::fromVariantList(Integrations::instance()->list());
    if (method == "integration.install" || method == "integration.remove") {
        const QString e = method == "integration.install" ? Integrations::instance()->install(p["agent"].toString(), p["kind"].toString("hooks"))
                                                          : Integrations::instance()->remove(p["agent"].toString(), p["kind"].toString("hooks"));
        if (!e.isEmpty()) throw ApiError(e.toStdString());
        return true;
    }
    if (method == "overlay.show") { m_ws->showOverlay(p["name"].toString(), p["data"].toObject().toVariantMap()); return true; }
    if (method == "action.run") {
        if (!m_ws->runAction(p["action"].toString())) throw ApiError("unknown or inapplicable action");
        return true;
    }
    if (method == "space.list") {
        QJsonArray out;
        for (int i = 0; i < spaces.size(); ++i) {
            QJsonArray tabs;
            for (int ti = 0; ti < spaces[i]->tabList().size(); ++ti) {
                Tab *t = spaces[i]->tabList()[ti];
                QJsonArray ids;
                for (auto *s : t->sessions()) ids << s->id();
                tabs << QJsonObject{{"index", ti}, {"title", t->title()}, {"current", ti == spaces[i]->currentIndex()},
                                    {"focused_pane", t->focusedId()}, {"panes", ids}};
            }
            out << QJsonObject{{"index", i}, {"name", spaces[i]->name()}, {"cwd", spaces[i]->cwd()}, {"branch", spaces[i]->branch()},
                               {"state", spaces[i]->state()}, {"profile", spaces[i]->profile()}, {"current", i == m_ws->currentIndex()}, {"tabs", tabs}};
        }
        return out;
    }
    if (method == "space.create") return QJsonObject{{"space", m_ws->newSpace(p["cwd"].toString(), p["name"].toString())}};
    if (method == "space.rename") { spaceAt(p); m_ws->renameSpace(p.contains("space") ? p["space"].toInt() : m_ws->currentIndex(), p["name"].toString()); return true; }
    if (method == "space.focus") { spaceAt(p); m_ws->setCurrent(p["space"].toInt()); return true; }
    if (method == "space.close") { spaceAt(p); m_ws->closeSpace(p.contains("space") ? p["space"].toInt() : m_ws->currentIndex()); return true; }
    if (method == "tab.create") {
        Space *s = spaceAt(p);
        s->newTab();
        return QJsonObject{{"tab", s->currentIndex()}, {"pane", s->currentTab()->focusedId()}};
    }
    if (method == "tab.focus") { spaceAt(p)->setCurrent(p["tab"].toInt()); return true; }
    if (method == "tab.close") { spaceAt(p)->closeTab(p.contains("tab") ? p["tab"].toInt() : spaceAt(p)->currentIndex()); return true; }

    if (method == "pane.list" || method == "agent.list") {
        QJsonArray out;
        for (Space *s : spaces)
            for (Tab *t : s->tabList())
                for (auto *ts : t->sessions())
                    if (method == "pane.list" || !ts->agent().isEmpty()) out << paneInfo(ts);
        return out;
    }
    if (method == "pane.get") return paneInfo(pane(p));
    if (method == "pane.focus") {
        int si, ti;
        const int id = p.contains("pane") ? p["pane"].toInt() : pane(p)->id();
        if (!m_ws->tabOfPane(id, &si, &ti)) throw ApiError("no such pane");
        m_ws->focusAgent(si, ti, id);
        return true;
    }
    if (method == "pane.split") {
        int si, ti;
        Tab *t;
        TerminalSession *s = pane(p);
        m_ws->findPane(s->id(), &si, &ti, &t);
        t->focusPane(s->id());
        const QString dir = p["direction"].toString("right");
        if (dir != "right" && dir != "down") throw ApiError("direction must be 'right' or 'down'");
        return QJsonObject{{"pane", t->split(dir == "right")}};
    }
    if (method == "view.close") {
        if (!m_ws->closeView(p["view"].toInt())) throw ApiError("no such view");
        return true;
    }
    if (method == "view.dock") {   // move a view between the modal and the layout
        ViewPane *v = m_ws->findView(p["view"].toInt());
        if (!v) throw ApiError("no such view");
        const QString where = p["where"].toString("right");
        if (!Settings::viewPlacements().contains(where)) throw ApiError("where must be modal, right, down or tab");
        if (where == "modal") { if (!m_ws->isModal(v)) m_ws->popOutView(v->id()); return true; }
        if (m_ws->isModal(v)) {
            m_ws->showModal(v, v->anchor());   // bring it to the top of the stack, then dock the top
            if (!m_ws->dockModal(where)) throw ApiError("cannot dock the view");
            return true;
        }
        throw ApiError("the view is already docked; close it or pop it out first");
    }
    if (method == "pane.close") {
        const int id = p.contains("pane") ? p["pane"].toInt() : pane(p)->id();
        if (m_ws->closeView(id)) return true;
        Tab *t = m_ws->tabOfPane(id);
        if (!t) throw ApiError("no such pane");
        t->closePane(id);
        return true;
    }
    if (method == "view.show") {   // in-process callers (the operator) get the checker report without waiting for the page
        ViewPane *v = nullptr;
        return showView(p, &v);
    }
    if (method == "view.toggle") {   // hide / show the modal views
        if (!m_ws->modalCount()) throw ApiError("no modal views");
        m_ws->setModalHidden(m_ws->modalVisible());
        return m_ws->modalVisible() ? "shown" : "hidden";
    }
    if (method == "view.get") {
        ViewPane *v = m_ws->findView(p["view"].toInt());
        if (!v) throw ApiError("no such view");
        return v->report();
    }
    if (method == "view.list") {
        QJsonArray out;
        const auto add = [&out](ViewPane *v, const QString &where, int si, int ti) {
            const QJsonObject r = v->report();
            QJsonObject o{{"view", v->id()}, {"title", v->title()}, {"source", r["source"]}, {"where", where},
                          {"errors", r["errors"].toArray().size()}, {"warnings", r["warnings"].toArray().size()}};
            if (si >= 0) { o["space"] = si; o["tab"] = ti; }
            out << o;
        };
        for (int si = 0; si < spaces.size(); ++si)
            for (int ti = 0; ti < spaces[si]->tabList().size(); ++ti)
                for (ViewPane *v : spaces[si]->tabList()[ti]->views()) add(v, "docked", si, ti);
        for (ViewPane *v : m_ws->modals()) add(v, "modal", -1, -1);
        return out;
    }
    if (method == "view.components") {
        if (p["name"].toString().isEmpty()) return ViewEngine::instance().components();
        const QJsonObject d = ViewEngine::instance().describe(p["name"].toString().remove(QRegularExpression("^nebula:")));
        if (d.isEmpty()) throw ApiError("unknown component; list them with view.components");
        return d;
    }
    if (method == "pane.send_text") {
        TerminalSession *s = pane(p);
        const QString text = p["text"].toString();
        if (p["paste"].toBool()) s->paste(text);
        else {
            const QStringList lines = text.split('\n');
            for (int i = 0; i < lines.size(); ++i) {
                s->sendText(lines[i]);
                if (i + 1 < lines.size()) s->injectKey(Qt::Key_Return, 0, "\r");
            }
        }
        if (p["enter"].toBool()) s->injectKey(Qt::Key_Return, 0, "\r");
        return true;
    }
    if (method == "pane.send_keys") {
        TerminalSession *s = pane(p);
        const QJsonValue kv = p["keys"];
        QStringList keys;
        if (kv.isArray()) for (const QJsonValue &v : kv.toArray()) keys << v.toString();
        else keys << kv.toString();
        std::vector<KeySpec> parsed;
        for (const QString &k : std::as_const(keys)) parsed.push_back(parseKey(k));
        for (const KeySpec &k : parsed) s->injectKey(k.key, k.mods, k.text);
        return true;
    }
    if (method == "pane.read") return pane(p)->readText(p["lines"].toInt(0), p["scrollback"].toBool());
    if (method == "pane.report_state") {
        const QString st = p["state"].toString();
        static const QStringList ok = {"working", "blocked", "done", "idle", ""};
        if (!ok.contains(st)) throw ApiError("state must be working|blocked|done|idle (or empty to clear)");
        pane(p)->reportState(st, p["ttl"].toInt(300));
        return true;
    }
    throw ApiError("unknown method '" + method.toStdString() + "'");
}
