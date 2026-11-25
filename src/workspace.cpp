#include "workspace.h"
#include "platform.h"
#include "paneids.h"
#include "paths.h"
#include "hostclient.h"
#include "llm.h"
#include "settings.h"
#include "theme.h"
#include "views/viewpane.h"
#include "editor/editorpane.h"
#include "cli.h"
#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPointer>
#include <QProcess>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <cmath>
#include <memory>

// ---------------- Tab ----------------

Tab::Tab(const QString &cwd, const QString &profile, QObject *parent) : QObject(parent), m_profile(profile) {
    m_root = new Node;
    m_root->id = m_nodeSeq++;
    m_root->pane = makeSession(cwd, 0, false, QString(), profile)->id();
    m_focus = m_root->pane;
}

Tab::~Tab() { delete m_root; }

TerminalSession *Tab::makeSession(const QString &cwd, int id, bool attach, const QString &prefill, const QString &profile, bool autorun) {
    auto *s = new TerminalSession(cwd, this, id, attach, prefill, profile, autorun);
    m_sessions.insert(s->id(), s);
    const int sid = s->id();
    connect(s, &TerminalSession::finished, this, [this, sid] { closePane(sid); });
    connect(s, &TerminalSession::metaChanged, this, &Tab::activity);
    connect(s, &TerminalSession::attention, this, [this, s](const QString &k) { emit attention(s, k); });
    return s;
}

void Tab::terminateAll() {
    for (TerminalSession *s : std::as_const(m_sessions)) s->terminate();
    for (ViewPane *v : std::as_const(m_views)) v->discard();
}

QList<ViewPane *> Tab::views() const {
    auto l = m_views.values();
    std::sort(l.begin(), l.end(), [](auto *a, auto *b) { return a->id() < b->id(); });
    return l;
}

QObject *Tab::view(int id) const { return m_views.value(id); }
QObject *Tab::editor(int id) const { return m_editors.value(id); }

QList<EditorPane *> Tab::editors() const {
    auto l = m_editors.values();
    std::sort(l.begin(), l.end(), [](auto *a, auto *b) { return a->id() < b->id(); });
    return l;
}

EditorPane *Tab::dirtyEditor() const {
    for (EditorPane *e : m_editors)
        if (e->dirty()) return e;
    return nullptr;
}

QString Tab::kindOf(int pane) const { return m_views.contains(pane) ? "view" : m_editors.contains(pane) ? "editor" : "terminal"; }

void Tab::adoptEditor(EditorPane *e) {
    e->setParent(this);
    m_editors.insert(e->id(), e);
    connect(e, &EditorPane::changed, this, &Tab::activity);
    connect(e, &EditorPane::textChanged, this, &Tab::activity);   // the session keeps unsaved text
    const int id = e->id();
    connect(e, &EditorPane::closeConfirmed, this, [this, id] { removePane(id, true); });
}

bool Tab::addEditor(EditorPane *e, int anchorPane, bool sideBySide) {
    adoptEditor(e);
    if (!addLeaf(e->id(), anchorPane, sideBySide)) { m_editors.remove(e->id()); e->disconnect(this); return false; }
    focusPane(e->id());
    return true;
}

Tab *Tab::withEditor(EditorPane *e, QObject *parent) {
    auto *t = new Tab(RestoreTag{}, parent);
    t->adoptEditor(e);
    t->m_root = new Node;
    t->m_root->id = t->m_nodeSeq++;
    t->m_root->pane = e->id();
    t->m_focus = e->id();
    return t;
}

void Tab::adoptView(ViewPane *v) {
    v->setParent(this);
    m_views.insert(v->id(), v);
    connect(v, &ViewPane::changed, this, &Tab::activity);
}

bool Tab::addView(ViewPane *v, int anchorPane, bool sideBySide) {
    if (!find(m_root, anchorPane)) return false;
    adoptView(v);
    return addLeaf(v->id(), anchorPane, sideBySide);
}

bool Tab::addLeaf(int pane, int anchorPane, bool sideBySide) {
    Node *n = find(m_root, anchorPane);
    if (!n) return false;
    auto *a = new Node, *b = new Node;
    a->id = m_nodeSeq++;
    a->pane = n->pane;
    a->parent = n;
    b->id = m_nodeSeq++;
    b->pane = pane;
    b->parent = n;
    n->pane = -1;
    n->horizontal = sideBySide;
    n->ratio = 0.5;
    n->a = a;
    n->b = b;
    m_zoom = false;
    emit layoutChanged();
    emit activity();
    return true;
}

Tab *Tab::withView(ViewPane *v, QObject *parent) {
    auto *t = new Tab(RestoreTag{}, parent);
    t->adoptView(v);
    t->m_root = new Node;
    t->m_root->id = t->m_nodeSeq++;
    t->m_root->pane = v->id();
    t->m_focus = v->id();
    return t;
}

QJsonObject Tab::toJson() const {
    QJsonObject panes;
    for (TerminalSession *s : m_sessions) {
        QJsonObject p{{"cwd", s->cwd()}, {"cmd", s->resumeCommand()}, {"profile", s->profile()}};
        if (!p["cmd"].toString().isEmpty()) p["run"] = s->resumeAutorun();
        if (!s->agentSession().isEmpty()) p["agentSession"] = s->agentSession();
        panes[QString::number(s->id())] = p;
    }
    QJsonObject views;
    for (ViewPane *v : m_views) views[QString::number(v->id())] = v->toJson();
    QJsonObject editors;
    for (EditorPane *e : m_editors) editors[QString::number(e->id())] = e->toJson();
    return {{"layout", m_root ? QJsonObject::fromVariantMap(toVariant(m_root)) : QJsonObject()},
            {"focus", m_focus}, {"zoom", m_zoom}, {"panes", panes}, {"views", views}, {"editors", editors}};
}

Tab *Tab::restore(const QJsonObject &o, const QSet<int> &live, QObject *parent) {
    auto *t = new Tab(RestoreTag{}, parent);
    const QJsonObject panes = o["panes"].toObject();
    const QJsonObject views = o["views"].toObject();
    const QJsonObject editors = o["editors"].toObject();
    std::function<Node *(const QJsonObject &)> build = [&](const QJsonObject &n) -> Node * {
        if (n.isEmpty()) return nullptr;
        auto *node = new Node;
        node->id = t->m_nodeSeq++;
        if (n["leaf"].toBool()) {
            const int id = n["pane"].toInt();
            if (id <= 0 || t->hasPane(id)) { delete node; return nullptr; }
            if (editors.contains(QString::number(id))) {
                PaneIds::reserve(id);
                t->adoptEditor(EditorPane::restore(id, editors[QString::number(id)].toObject(), t));
                node->pane = id;
                return node;
            }
            if (views.contains(QString::number(id))) {
                PaneIds::reserve(id);
                t->adoptView(ViewPane::restore(id, views[QString::number(id)].toObject(), t));
                node->pane = id;
                return node;
            }
            const QJsonObject info = panes[QString::number(id)].toObject();
            // sessions saved before "run" existed only stored agents' resume commands, which do run
            auto *ts = t->makeSession(info["cwd"].toString(), id, live.contains(id), info["cmd"].toString(), info["profile"].toString(),
                                      info["run"].toBool(true));
            ts->setAgentSession(info["agentSession"].toString());
            node->pane = id;
            return node;
        }
        node->horizontal = n["horizontal"].toBool(true);
        node->ratio = qBound(0.05, n["ratio"].toDouble(0.5), 0.95);
        node->a = build(n["a"].toObject());
        node->b = build(n["b"].toObject());
        if (!node->a || !node->b) {
            // half-broken node: keep whichever side survived
            Node *keep = node->a ? node->a : node->b;
            node->a = node->b = nullptr;
            delete node;
            return keep;
        }
        node->a->parent = node->b->parent = node;
        return node;
    };
    t->m_root = build(o["layout"].toObject());
    if (!t->m_root) { delete t; return nullptr; }
    t->m_root->parent = nullptr;
    QList<int> order;
    t->leaves(t->m_root, order);
    t->m_focus = t->hasPane(o["focus"].toInt()) ? o["focus"].toInt() : order.first();
    t->m_zoom = o["zoom"].toBool() && order.size() > 1;
    return t;
}

Tab::Node *Tab::find(Node *n, int pane) const {
    if (!n) return nullptr;
    if (n->leaf()) return n->pane == pane ? n : nullptr;
    if (Node *r = find(n->a, pane)) return r;
    return find(n->b, pane);
}

QVariantMap Tab::toVariant(const Node *n) const {
    QVariantMap m;
    if (n->leaf()) {
        m["leaf"] = true;
        m["pane"] = n->pane;
        m["kind"] = kindOf(n->pane);
        return m;
    }
    m["leaf"] = false;
    m["node"] = n->id;
    m["horizontal"] = n->horizontal;
    m["ratio"] = n->ratio;
    m["a"] = toVariant(n->a);
    m["b"] = toVariant(n->b);
    return m;
}

QVariantMap Tab::layout() const {
    if (!m_root) return {};
    if (m_zoom) return {{"leaf", true}, {"pane", m_focus}, {"kind", kindOf(m_focus)}};
    return toVariant(m_root);
}

QString Tab::title() const {
    if (ViewPane *v = m_views.value(m_focus)) return v->title();
    if (EditorPane *e = m_editors.value(m_focus)) return e->label();
    auto *s = focusedSession();
    if (!s) return "shell";
    if (!s->label().isEmpty()) return s->label();
    return QFileInfo(s->cwd()).fileName();
}

int Tab::split(bool sideBySide) { return splitWith(sideBySide, QString(), QString()); }

int Tab::splitWith(bool sideBySide, const QString &cwd, const QString &profile) {
    Node *n = find(m_root, m_focus);
    if (!n) return -1;
    auto *cur = focusedSession();
    QString dir = cwd;
    if (dir.isEmpty()) dir = cur ? cur->cwd() : m_views.contains(m_focus) ? m_views[m_focus]->baseDir()
                                              : m_editors.contains(m_focus) ? m_editors[m_focus]->baseDir() : QString();
    auto *s = makeSession(dir, 0, false, QString(),
                          profile.isEmpty() ? (cur ? cur->profile() : m_profile) : profile);
    auto *a = new Node, *b = new Node;
    a->id = m_nodeSeq++;
    a->pane = n->pane;
    a->parent = n;
    b->id = m_nodeSeq++;
    b->pane = s->id();
    b->parent = n;
    n->pane = -1;
    n->horizontal = sideBySide;
    n->ratio = 0.5;
    n->a = a;
    n->b = b;
    m_focus = s->id();
    m_zoom = false;
    emit layoutChanged();
    emit focusChanged();
    emit activity();
    return s->id();
}

void Tab::closePane(int id) {
    if (EditorPane *e = m_editors.value(id); e && e->dirty()) {
        focusPane(id);
        e->requestClose();   // closes through closeConfirmed once answered
        return;
    }
    removePane(id, true);
}

ViewPane *Tab::takeView(int id) { return m_views.contains(id) ? removePane(id, false) : nullptr; }

// Removes a leaf from the layout. Terminals are always killed; a view is either discarded or handed back
// (parentless) to the caller.
ViewPane *Tab::removePane(int id, bool destroy) {
    Node *n = find(m_root, id);
    if (!n || !hasPane(id)) return nullptr;
    QList<int> order;
    leaves(m_root, order);
    const int idx = order.indexOf(id);
    ViewPane *kept = nullptr;
    if (TerminalSession *s = m_sessions.take(id)) {
        s->disconnect(this);
        s->terminate();
        s->deleteLater();
    } else if (ViewPane *v = m_views.take(id)) {
        v->disconnect(this);
        if (destroy) { v->discard(); v->deleteLater(); }
        else { v->setParent(nullptr); kept = v; }
    } else if (EditorPane *e = m_editors.take(id)) {
        e->disconnect(this);
        e->discard();
        e->deleteLater();
    }

    if (!n->parent) {
        delete m_root;
        m_root = nullptr;
        emit empty();
        return kept;
    }
    Node *p = n->parent;
    Node *sib = p->a == n ? p->b : p->a;
    p->a = p->b = nullptr;
    delete n;
    p->pane = sib->pane;
    p->horizontal = sib->horizontal;
    p->ratio = sib->ratio;
    p->a = sib->a;
    p->b = sib->b;
    if (p->a) p->a->parent = p;
    if (p->b) p->b->parent = p;
    sib->a = sib->b = nullptr;
    delete sib;
    m_zoom = false;
    if (m_focus == id) {
        QList<int> now;
        leaves(m_root, now);
        m_focus = now.value(qBound(0, idx - 1, int(now.size()) - 1), now.value(0, -1));
        emit focusChanged();
    }
    emit layoutChanged();
    emit activity();
    return kept;
}

void Tab::focusPane(int id) {
    if (id == m_focus || !hasPane(id)) return;
    m_focus = id;
    emit focusChanged();
    emit activity();
}

void Tab::leaves(const Node *n, QList<int> &out) const {
    if (!n) return;
    if (n->leaf()) { out << n->pane; return; }
    leaves(n->a, out);
    leaves(n->b, out);
}

void Tab::rects(const Node *n, QRectF r, QHash<int, QRectF> &out) const {
    if (n->leaf()) { out.insert(n->pane, r); return; }
    if (n->horizontal) {
        const qreal w = r.width() * n->ratio;
        rects(n->a, QRectF(r.x(), r.y(), w, r.height()), out);
        rects(n->b, QRectF(r.x() + w, r.y(), r.width() - w, r.height()), out);
    } else {
        const qreal h = r.height() * n->ratio;
        rects(n->a, QRectF(r.x(), r.y(), r.width(), h), out);
        rects(n->b, QRectF(r.x(), r.y() + h, r.width(), r.height() - h), out);
    }
}

void Tab::focusDirection(int dx, int dy) {
    QHash<int, QRectF> rs;
    rects(m_root, QRectF(0, 0, 1, 1), rs);
    if (!rs.contains(m_focus)) return;
    const QRectF f = rs[m_focus];
    int best = -1;
    qreal bestScore = 1e9;
    for (auto it = rs.begin(); it != rs.end(); ++it) {
        if (it.key() == m_focus) continue;
        const QRectF r = it.value();
        qreal gap, overlap;
        if (dx) {
            gap = dx > 0 ? r.left() - f.right() : f.left() - r.right();
            overlap = std::min(r.bottom(), f.bottom()) - std::max(r.top(), f.top());
        } else {
            gap = dy > 0 ? r.top() - f.bottom() : f.top() - r.bottom();
            overlap = std::min(r.right(), f.right()) - std::max(r.left(), f.left());
        }
        if (gap < -1e-6 || overlap <= 1e-6) continue;
        const qreal score = gap - overlap * 0.01;
        if (score < bestScore) { bestScore = score; best = it.key(); }
    }
    if (best >= 0) focusPane(best);
}

void Tab::focusCycle(int delta) {
    QList<int> order;
    leaves(m_root, order);
    if (order.size() < 2) return;
    const int i = order.indexOf(m_focus);
    focusPane(order[(i + delta + order.size()) % order.size()]);
}

void Tab::setRatio(int nodeId, double ratio) {
    std::function<Node *(Node *)> f = [&](Node *n) -> Node * {
        if (!n || n->leaf()) return nullptr;
        if (n->id == nodeId) return n;
        if (Node *r = f(n->a)) return r;
        return f(n->b);
    };
    if (Node *n = f(m_root)) { n->ratio = qBound(0.05, ratio, 0.95); emit activity(); }
}

void Tab::toggleZoom() {
    QList<int> order;
    leaves(m_root, order);
    if (order.size() < 2 && !m_zoom) return;
    m_zoom = !m_zoom;
    emit layoutChanged();
    emit activity();
}

// ---------------- Space ----------------

Space::Space(const QString &cwd, QObject *parent) : QObject(parent) {
    m_cwd = cwd.isEmpty() ? QDir::homePath() : cwd;
    addTab(m_cwd);
}

Tab *Space::addTab(const QString &cwd) {
    auto *t = new Tab(cwd, m_profile, this);
    adopt(t);
    return t;
}

void Space::adopt(Tab *t) {
    m_tabs << t;
    connect(t, &Tab::activity, this, &Space::activity);
    connect(t, &Tab::attention, this, &Space::attention);
    connect(t, &Tab::empty, this, [this, t] { closeTab(int(m_tabs.indexOf(t))); });
}

void Space::terminateAll() {
    for (Tab *t : std::as_const(m_tabs)) t->terminateAll();
}

QJsonObject Space::toJson() const {
    QJsonArray tabs;
    for (Tab *t : m_tabs) tabs << t->toJson();
    return {{"name", m_custom}, {"cwd", cwd()}, {"profile", m_profile}, {"current", m_current}, {"tabs", tabs}};
}

Space *Space::restore(const QJsonObject &o, const QSet<int> &live, QObject *parent) {
    auto *s = new Space(RestoreTag{}, parent);
    s->m_cwd = o["cwd"].toString();
    if (s->m_cwd.isEmpty() || !QFileInfo(s->m_cwd).isDir()) s->m_cwd = QDir::homePath();
    s->m_custom = o["name"].toString();
    s->m_profile = o["profile"].toString();
    for (const QJsonValue &v : o["tabs"].toArray())
        if (Tab *t = Tab::restore(v.toObject(), live, s)) s->adopt(t);
    if (s->m_tabs.isEmpty()) { delete s; return nullptr; }
    s->m_current = qBound(0, o["current"].toInt(), int(s->m_tabs.size()) - 1);
    return s;
}

QString Space::name() const {
    if (!m_custom.isEmpty()) return m_custom;
    const QString dir = cwd();
    if (dir == QDir::homePath()) return "~";
    const QString n = QFileInfo(dir).fileName();
    return n.isEmpty() ? "/" : n;
}

void Space::rename(const QString &name) {
    m_custom = name.trimmed();
    emit activity();
}
QString Space::cwd() const {
    auto *t = currentTab();
    auto *s = t ? t->focusedSession() : nullptr;
    return s && !s->cwd().isEmpty() ? s->cwd() : m_cwd;
}
QString Space::branch() const {
    auto *t = currentTab();
    auto *s = t ? t->focusedSession() : nullptr;
    return s ? s->branch() : QString();
}

QString Space::state() const {
    static const QStringList prio = {"blocked", "working", "done", "idle"};
    int best = prio.size();
    for (Tab *t : m_tabs)
        for (TerminalSession *s : t->sessions()) {
            const int p = int(prio.indexOf(s->agentState()));
            if (p >= 0 && p < best) best = p;
        }
    return best < prio.size() ? prio[best] : QString();
}

QVariantList Space::tabs() const {
    QVariantList l;
    for (Tab *t : m_tabs) l << QVariant::fromValue<QObject *>(t);
    return l;
}

int Space::newTabWith(const QString &cwd, const QString &profile) {
    Tab *t = addTab(cwd);
    if (!profile.isEmpty()) {
        // addTab used the space profile; rebuild with the requested one
        m_tabs.removeOne(t);
        t->disconnect(this);
        t->terminateAll();
        t->deleteLater();
        t = new Tab(cwd, profile, this);
        adopt(t);
    }
    m_current = int(m_tabs.size()) - 1;
    emit tabsChanged();
    emit currentChanged();
    emit activity();
    return t->focusedId();
}

void Space::newTab() {
    auto *cur = currentTab() ? currentTab()->focusedSession() : nullptr;
    addTab(cur ? cur->cwd() : m_cwd);
    m_current = int(m_tabs.size()) - 1;
    emit tabsChanged();
    emit currentChanged();
    emit activity();
}

void Space::closeTab(int i) {
    if (i < 0 || i >= m_tabs.size()) return;
    // unsaved text is never dropped by closing around it: ask in the editor first
    if (EditorPane *e = m_tabs[i]->dirtyEditor()) {
        setCurrent(i);
        m_tabs[i]->closePane(e->id());
        return;
    }
    // the last tab of a space that held only files (Ctrl+W on the last open file): a shell takes its place, so
    // closing a file never closes the space, or nebula with it
    if (m_tabs.size() == 1 && m_tabs[i]->sessions().isEmpty()) {
        Tab *t = m_tabs[i];
        QString dir = m_cwd;
        if (!t->editors().isEmpty()) dir = t->editors().first()->baseDir();
        else if (!t->views().isEmpty()) dir = t->views().first()->baseDir();
        addTab(QFileInfo(dir).isDir() ? dir : m_cwd);
    }
    Tab *t = m_tabs.takeAt(i);
    t->disconnect(this);
    t->terminateAll();
    t->deleteLater();
    if (m_tabs.isEmpty()) { emit empty(); return; }
    if (m_current >= i) m_current = qMax(0, m_current - 1);
    emit tabsChanged();
    emit currentChanged();
    emit activity();
}

void Space::setCurrent(int i) {
    if (i < 0 || i >= m_tabs.size() || i == m_current) return;
    m_current = i;
    emit currentChanged();
    emit activity();
}

void Space::step(int delta) { setCurrent((m_current + delta + int(m_tabs.size())) % int(m_tabs.size())); }

// ---------------- Workspace ----------------

static QString statePath() { return Paths::stateDir() + "/session.json"; }

Workspace::Workspace(Theme *theme, QObject *parent) : QObject(parent), m_theme(theme) {
    loadBindings();
    qApp->installEventFilter(this);
}

QVariantList Workspace::spaces() const {
    QVariantList l;
    for (Space *s : m_spaces) l << QVariant::fromValue<QObject *>(s);
    return l;
}

TerminalSession *Workspace::findPane(int id, int *spaceIdx, int *tabIdx, Tab **tab) const {
    for (int si = 0; si < m_spaces.size(); ++si) {
        const auto &tabs = m_spaces[si]->tabList();
        for (int ti = 0; ti < tabs.size(); ++ti)
            if (TerminalSession *s = tabs[ti]->sessionById(id)) {
                if (spaceIdx) *spaceIdx = si;
                if (tabIdx) *tabIdx = ti;
                if (tab) *tab = tabs[ti];
                return s;
            }
    }
    return nullptr;
}

ViewPane *Workspace::findView(int id, int *spaceIdx, int *tabIdx, Tab **tab) const {
    Tab *t = tabOfPane(id, spaceIdx, tabIdx);
    if (tab) *tab = t;
    if (t) return t->viewById(id);
    for (ViewPane *v : m_modals)
        if (v->id() == id) {
            if (spaceIdx) *spaceIdx = -1;
            if (tabIdx) *tabIdx = -1;
            return v;
        }
    return nullptr;
}

QObject *Workspace::modalView() const { return m_modals.isEmpty() ? nullptr : m_modals.last(); }
QList<ViewPane *> Workspace::modals() const { return m_modals; }
bool Workspace::isModal(const ViewPane *v) const { return m_modals.contains(const_cast<ViewPane *>(v)); }

void Workspace::showModal(ViewPane *v, int anchorPane) {
    v->setParent(this);
    v->setAnchor(anchorPane);
    m_modals.removeAll(v);
    m_modals.append(v);
    m_modalHidden = false;
    emit modalChanged();
}

void Workspace::closeModal() {
    if (m_modals.isEmpty()) return;
    ViewPane *v = m_modals.takeLast();
    v->discard();
    v->deleteLater();
    if (m_modals.isEmpty()) m_modalHidden = false;
    emit modalChanged();
    if (m_modals.isEmpty()) emit focusRequested();
}

void Workspace::setModalHidden(bool hidden) {
    if (m_modals.isEmpty() || hidden == m_modalHidden) return;
    m_modalHidden = hidden;
    emit modalChanged();
    if (hidden) emit focusRequested();
}

bool Workspace::dockModal(const QString &where) {
    if (m_modals.isEmpty()) return false;
    ViewPane *v = m_modals.last();
    int anchor = v->anchor();
    if (!tabOfPane(anchor)) {
        Tab *t = currentSpace() ? currentSpace()->currentTab() : nullptr;
        if (!t) return false;
        anchor = t->focusedId();
    }
    m_modals.removeLast();
    if (!placeView(v, anchor, where)) { m_modals.append(v); return false; }
    if (m_modals.isEmpty()) m_modalHidden = false;
    emit modalChanged();
    emit focusRequested();
    return true;
}

void Workspace::popOutView(int viewId) {
    Tab *t = tabOfPane(viewId);
    ViewPane *v = t ? t->takeView(viewId) : nullptr;
    if (!v) return;
    showModal(v, tabOfPane(v->anchor()) ? v->anchor() : t->focusedId());
    scheduleSave();
}

bool Workspace::closeView(int id) {
    for (ViewPane *v : std::as_const(m_modals))
        if (v->id() == id) {
            m_modals.removeAll(v);
            v->discard();
            v->deleteLater();
            if (m_modals.isEmpty()) m_modalHidden = false;
            emit modalChanged();
            return true;
        }
    Tab *t = tabOfPane(id);
    if (!t || !t->viewById(id)) return false;
    t->closePane(id);
    return true;
}

Tab *Workspace::tabOfPane(int id, int *spaceIdx, int *tabIdx) const {
    for (int si = 0; si < m_spaces.size(); ++si) {
        const auto &tabs = m_spaces[si]->tabList();
        for (int ti = 0; ti < tabs.size(); ++ti)
            if (tabs[ti]->hasPane(id)) {
                if (spaceIdx) *spaceIdx = si;
                if (tabIdx) *tabIdx = ti;
                return tabs[ti];
            }
    }
    return nullptr;
}

bool Workspace::placeView(ViewPane *v, int anchorPane, const QString &where) {
    int si = -1;
    Tab *t = tabOfPane(anchorPane, &si);
    if (!t) return false;
    if (where == "tab") {
        Space *s = m_spaces[si];
        s->adopt(Tab::withView(v, s));
        emit s->tabsChanged();
        emit s->activity();
        scheduleSave();
        return true;
    }
    if (!t->addView(v, anchorPane, where != "down")) return false;
    scheduleSave();
    return true;
}

TerminalSession *Workspace::focusedPane() const {
    Space *s = currentSpace();
    Tab *t = s ? s->currentTab() : nullptr;
    return t ? t->focusedSession() : nullptr;
}

void Workspace::restore() {
    auto *host = HostClient::instance();
    if (!host->connectToHost()) {
        std::fprintf(stderr, "nebula: cannot start or reach the pty host daemon\n");
        QCoreApplication::exit(1);
        return;
    }
    QSet<int> live, all;
    for (const auto &p : host->list()) {
        PaneIds::reserve(p.id);
        all.insert(p.id);
        if (!p.exited) live.insert(p.id);
    }

    QJsonObject doc;
    QFile f(statePath());
    if (f.open(QIODevice::ReadOnly)) doc = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue &v : doc["spaces"].toArray())
        if (Space *s = Space::restore(v.toObject(), live, this)) adopt(s);

    // panes that survived in the host but are no longer in the saved layout: keep them reachable
    QSet<int> used;
    for (Space *s : std::as_const(m_spaces))
        for (Tab *t : s->tabList())
            for (auto *ts : t->sessions()) used.insert(ts->id());
    QJsonArray orphanTabs;
    for (int id : std::as_const(live))
        if (!used.contains(id))
            orphanTabs << QJsonObject{{"layout", QJsonObject{{"leaf", true}, {"pane", id}}}, {"focus", id}, {"panes", QJsonObject()}};
    if (!orphanTabs.isEmpty())
        if (Space *s = Space::restore({{"name", "recovered"}, {"tabs", orphanTabs}}, live, this)) adopt(s);

    // inline content of views that are not part of the restored layout (modals, closed with the window) is stale
    QSet<QString> keep;
    for (Space *s : std::as_const(m_spaces))
        for (Tab *t : s->tabList())
            for (ViewPane *v : t->views()) keep.insert(QString("%1.nebula.md").arg(v->id()));
    QSet<QString> keepEditors;
    for (Space *s : std::as_const(m_spaces))
        for (Tab *t : s->tabList())
            for (EditorPane *e : t->editors()) keepEditors.insert(QString("%1.txt").arg(e->id()));
    QDir editorsDir(Paths::stateDir() + "/editors");
    for (const QString &f : editorsDir.entryList({"*.txt"}, QDir::Files))
        if (!keepEditors.contains(f)) editorsDir.remove(f);
    QDir views(Paths::stateDir() + "/views");
    for (const QString &f : views.entryList({"*.nebula.md"}, QDir::Files))
        if (!keep.contains(f)) views.remove(f);
    for (int id : std::as_const(all))
        if (!live.contains(id)) host->kill(id);

    if (m_spaces.isEmpty()) newSpace(QDir::currentPath());
    else setCurrent(qBound(0, doc["current"].toInt(), int(m_spaces.size()) - 1));
    emit spacesChanged();

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &Workspace::saveNow);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &Workspace::saveNow);
}

void Workspace::scheduleSave() { if (!m_quitting) m_saveTimer.start(); }

void Workspace::saveNow() {
    if (m_quitting) return;
    QJsonArray a;
    for (Space *s : m_spaces) a << s->toJson();
    const QByteArray data = QJsonDocument(QJsonObject{{"version", 1}, {"current", m_current}, {"spaces", a}}).toJson(QJsonDocument::Compact);
    if (data == m_saved) return;
    QFile f(statePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { f.write(data); m_saved = data; }
}

void Workspace::killSession() {
    saveNow();          // the layout and what each pane runs, as they are now
    m_quitting = true;  // nothing the shutdown below closes may overwrite it
    m_saveTimer.stop();
    for (Space *s : std::as_const(m_spaces)) s->terminateAll();
    HostClient::instance()->shutdown();
    exitApp();
}

void Workspace::exitApp() {
    m_exiting = true;
    emit exitingChanged();
    QCoreApplication::quit();
}

void Workspace::discardSession() {
    for (Space *s : std::as_const(m_spaces)) s->terminateAll();
    HostClient::instance()->shutdown();
    m_spaces.clear();
    saveNow();
    m_quitting = true;
    m_saveTimer.stop();
    exitApp();
}

void Workspace::adopt(Space *s) {
    m_spaces << s;
    s->setParent(this);
    connect(s, &Space::activity, this, [this] { refresh(); emit spacesChanged(); scheduleSave(); });
    connect(s, &Space::currentChanged, this, &Workspace::refresh);
    connect(s, &Space::tabsChanged, this, &Workspace::scheduleSave);
    connect(s, &Space::empty, this, [this, s] { closeSpace(int(m_spaces.indexOf(s))); });
    connect(s, &Space::attention, this, [this](TerminalSession *ts, const QString &kind) {
        if (!Settings::instance()->notifications() && !Settings::instance()->aiSummaries()) return;
        QPointer<TerminalSession> tsp(ts);
        auto notify = [this, tsp, kind](const QString &summary) {
            if (!tsp) return;
            if (!Settings::instance()->notifications()) return;
            const QString title = tsp->agent() + " " + (kind == "done" ? "finished" : "needs your input");
            Platform::notify(title, summary.isEmpty() ? tsp->cwd() : summary);
        };
        if (kind == "done" && Settings::instance()->aiSummaries() && Llm::instance()->available()) {
            const int id = Llm::instance()->summarize(ts->readText(80, true));
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(Llm::instance(), &Llm::finished, this, [tsp, id, notify, conn](int rid, const QString &, const QString &text, const QString &) {
                if (rid != id) return;
                QObject::disconnect(*conn);
                if (tsp && !text.isEmpty()) tsp->setSummary(text);
                notify(text);
            });
        } else {
            notify(QString());
        }
    });
}

int Workspace::newSpace(const QString &cwd, const QString &name, const QString &profile) {
    QString dir = cwd;
    if (dir.isEmpty()) {
        auto *c = currentSpace();
        dir = c ? c->cwd() : QDir::homePath();
    }
    auto *s = new Space(Space::RestoreTag{}, this);
    s->m_cwd = dir;
    s->m_custom = name.trimmed();
    s->m_profile = profile.isEmpty() && currentSpace() ? currentSpace()->profile() : profile;
    s->addTab(dir);
    adopt(s);
    m_current = int(m_spaces.size()) - 1;
    emit spacesChanged();
    emit currentChanged();
    refresh();
    scheduleSave();
    return m_current;
}

void Workspace::closeSpace(int i) {
    if (i < 0 || i >= m_spaces.size()) return;
    for (int ti = 0; ti < m_spaces[i]->tabList().size(); ++ti)
        if (EditorPane *e = m_spaces[i]->tabList()[ti]->dirtyEditor()) {
            focusAgent(i, ti, e->id());
            m_spaces[i]->tabList()[ti]->closePane(e->id());
            return;
        }
    Space *s = m_spaces.takeAt(i);
    s->disconnect(this);
    s->terminateAll();
    s->deleteLater();
    if (m_spaces.isEmpty()) { saveNow(); exitApp(); return; }
    if (m_current >= i) m_current = qMax(0, m_current - 1);
    emit spacesChanged();
    emit currentChanged();
    refresh();
    scheduleSave();
}

void Workspace::setCurrent(int i) {
    if (i < 0 || i >= m_spaces.size()) return;
    m_current = i;
    emit currentChanged();
    refresh();
    scheduleSave();
}

void Workspace::focusAgent(int spaceIdx, int tabIdx, int paneId) {
    setCurrent(spaceIdx);
    Space *s = currentSpace();
    if (!s) return;
    s->setCurrent(tabIdx);
    if (auto *t = s->currentTab()) t->focusPane(paneId);
    refresh();
}

void Workspace::refresh() {
    for (int si = 0; si < m_spaces.size(); ++si)
        for (Tab *t : m_spaces[si]->tabList())
            for (TerminalSession *s : t->sessions())
                s->setActive(si == m_current && t == m_spaces[si]->currentTab() && s == t->focusedSession());
    rebuildAgents();
}

void Workspace::rebuildAgents() {
    QVariantList l;
    for (int si = 0; si < m_spaces.size(); ++si) {
        const auto &tabs = m_spaces[si]->tabList();
        for (int ti = 0; ti < tabs.size(); ++ti)
            for (TerminalSession *s : tabs[ti]->sessions())
                if (!s->agent().isEmpty())
                    l << QVariantMap{{"space", m_spaces[si]->name()}, {"agent", s->agent()}, {"state", s->agentState()},
                                     {"spaceIndex", si}, {"tabIndex", ti}, {"pane", s->id()}, {"summary", s->summary()},
                                     {"since", s->stateSince()}};
    }
    if (l == m_agents) return;
    QHash<int, QString> old;
    for (const QVariant &v : std::as_const(m_agents)) old.insert(v.toMap()["pane"].toInt(), v.toMap()["state"].toString());
    m_agents = l;
    for (const QVariant &v : std::as_const(m_agents)) {
        const QVariantMap m = v.toMap();
        const int pane = m["pane"].toInt();
        if (old.value(pane) != m["state"].toString())
            emit agentEvent({{"event", "agent.state"}, {"pane", pane}, {"agent", m["agent"].toString()}, {"state", m["state"].toString()},
                             {"previous", old.value(pane)}, {"space", m["spaceIndex"].toInt()}, {"tab", m["tabIndex"].toInt()}});
    }
    emit agentsChanged();
}

void Workspace::renameSpace(int i, const QString &name) {
    if (i < 0 || i >= m_spaces.size()) return;
    m_spaces[i]->rename(name);
    scheduleSave();
    emit focusRequested();
}

// ---------------- editors ----------------

EditorPane *Workspace::findEditor(int id, int *spaceIdx, int *tabIdx, Tab **tab) const {
    Tab *t = tabOfPane(id, spaceIdx, tabIdx);
    if (tab) *tab = t;
    return t ? t->editorById(id) : nullptr;
}

QList<EditorPane *> Workspace::allEditors() const {
    QList<EditorPane *> out;
    for (Space *s : m_spaces)
        for (Tab *t : s->tabList()) out << t->editors();
    return out;
}

QVariantMap Workspace::openEditor(const QString &path, int line, const QString &where, int anchorPane) {
    const QFileInfo fi(path);
    if (fi.isDir()) return {{"error", "is a directory"}};
    const QString canonical = fi.exists() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    // already open: focus it
    for (int si = 0; si < m_spaces.size(); ++si)
        for (int ti = 0; ti < m_spaces[si]->tabList().size(); ++ti)
            for (EditorPane *e : m_spaces[si]->tabList()[ti]->editors())
                if (e->path() == canonical) {
                    focusAgent(si, ti, e->id());
                    if (line > 0) e->gotoLine(line);
                    emit focusRequested();
                    return {{"pane", e->id()}};
                }
    auto *e = new EditorPane(PaneIds::next());
    QString err;
    if (!e->open(canonical, &err)) { delete e; return {{"error", err}}; }
    if (line > 0) e->gotoLine(line);
    Space *s = currentSpace();
    Tab *t = s ? s->currentTab() : nullptr;
    int anchor = anchorPane;
    if (anchor <= 0 && t) anchor = t->focusedId();
    int si = -1;
    Tab *at = tabOfPane(anchor, &si);
    const QString w = where.isEmpty() ? "right" : where;
    bool ok = false;
    if (w == "tab" || !at) {
        Space *target = at ? m_spaces[si] : s;
        if (target) {
            target->adopt(Tab::withEditor(e, target));
            target->setCurrent(int(target->tabList().size()) - 1);
            emit target->tabsChanged();
            emit target->activity();
            ok = true;
        }
    } else {
        ok = at->addEditor(e, anchor, w != "down");
        if (ok) focusAgent(si, int(m_spaces[si]->tabList().indexOf(at)), e->id());
    }
    if (!ok) { delete e; return {{"error", "cannot place the editor"}}; }
    scheduleSave();
    emit focusRequested();
    return {{"pane", e->id()}};
}

void Workspace::editView(int viewId) {
    ViewPane *v = findView(viewId);
    if (!v || v->sourceFile().isEmpty()) return;
    if (isModal(v)) setModalHidden(true);   // an agent's view: the editor still opens in a tab of its own
    const QVariantMap r = openEditor(v->sourceFile(), 0, "tab");
    EditorPane *e = r.contains("pane") ? findEditor(r["pane"].toInt()) : nullptr;
    if (e && !m_previews.value(e->id())) linkPreview(e, v);
}

// Keeps `v` showing what `e` holds: the unsaved text shortly after typing stops, the file again once saved.
void Workspace::linkPreview(EditorPane *e, ViewPane *v) {
    m_previews.insert(e->id(), v);
    auto sourceOf = [e] {
        ViewPane::Source src;
        if (e->dirty()) { src.content = e->buffer().text(); src.baseDir = e->baseDir(); }
        else { src.file = e->path(); src.baseDir = e->baseDir(); }
        return src;   // the title stays the document's (front matter), or the file name
    };
    auto *debounce = new QTimer(v);
    debounce->setSingleShot(true);
    debounce->setInterval(250);
    QPointer<EditorPane> ep(e);
    QPointer<ViewPane> vp(v);
    connect(debounce, &QTimer::timeout, v, [ep, vp, sourceOf] { if (ep && vp) vp->setSource(sourceOf()); });
    connect(e, &EditorPane::textChanged, debounce, qOverload<>(&QTimer::start));
    connect(e, &EditorPane::saved, debounce, qOverload<>(&QTimer::start));
}

void Workspace::previewEditor(int editorId) {
    EditorPane *e = findEditor(editorId);
    if (!e || !e->previewable()) return;
    ViewPane *v = m_previews.value(editorId);
    if (!v) v = viewOfFile(e->path());
    if (v) {   // already shown: go there (and make it follow this editor)
        if (!m_previews.value(editorId)) linkPreview(e, v);
        int vs = -1, vt = -1;
        if (findView(v->id(), &vs, &vt) && vs >= 0) focusAgent(vs, vt, v->id());
        return;
    }
    const int id = showFileView(e->path());
    if (ViewPane *nv = id > 0 ? findView(id) : nullptr) {
        linkPreview(e, nv);
        if (e->dirty()) { ViewPane::Source src; src.content = e->buffer().text(); src.baseDir = e->baseDir(); nv->setSource(src); }
    }
}

// A view of `file` (its canonical path) already on screen, docked or as a modal.
ViewPane *Workspace::viewOfFile(const QString &file) const {
    for (Space *s : m_spaces)
        for (Tab *t : s->tabList())
            for (ViewPane *v : t->views())
                if (v->sourceFile() == file) return v;
    for (ViewPane *v : m_modals)
        if (v->sourceFile() == file) return v;
    return nullptr;
}

// Files open in the main area, each in a tab of its own: never as a modal, never splitting a terminal.
QVariantMap Workspace::openFile(const QString &path) {
    const QFileInfo fi(path);
    if (fi.isDir()) return {{"error", "is a directory"}};
    const QString canonical = fi.exists() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    if (isDocument(path) && fi.exists()) {
        if (ViewPane *v = viewOfFile(canonical)) {   // already shown: go there
            int si = -1, ti = -1;
            if (findView(v->id(), &si, &ti) && si >= 0) focusAgent(si, ti, v->id());
            else showModal(v, v->anchor());
            return {{"pane", v->id()}};
        }
        const int id = showFileView(canonical);
        if (id < 0) return {{"error", "cannot open the view"}};
        return {{"pane", id}};
    }
    return openEditor(canonical, 0, "tab");
}

// A view of a document in a new tab of the current space; returns its pane id, or -1.
int Workspace::showFileView(const QString &file) {
    auto *v = new ViewPane(PaneIds::next());
    ViewPane::Source src;
    src.file = file;
    src.baseDir = QFileInfo(file).absolutePath();
    v->setSource(src);
    Space *s = currentSpace();
    Tab *t = s ? s->currentTab() : nullptr;
    if (!t || !placeView(v, t->focusedId(), "tab")) { delete v; return -1; }
    focusAgent(int(m_spaces.indexOf(s)), int(s->tabList().size()) - 1, v->id());
    scheduleSave();
    return v->id();
}

QString Workspace::projectDir() const {
    Space *s = currentSpace();
    const QString dir = s ? s->cwd() : QDir::homePath();
    QProcess git;
    git.setWorkingDirectory(dir);
    git.start("git", {"rev-parse", "--show-toplevel"});
    if (git.waitForFinished(1500) && git.exitCode() == 0) {
        const QString top = QString::fromUtf8(git.readAllStandardOutput()).trimmed();
        if (!top.isEmpty()) return top;
    }
    return dir;
}

void Workspace::terminalAt(const QString &dir) {
    Space *s = currentSpace();
    if (!s) return;
    s->newTabWith(QFileInfo(dir).isDir() ? dir : QFileInfo(dir).absolutePath(), s->profile());
    emit focusRequested();
}

QString Workspace::currentDir() const {
    Space *s = currentSpace();
    Tab *t = s ? s->currentTab() : nullptr;
    if (!t) return QDir::homePath();
    if (TerminalSession *ts = t->focusedSession()) return ts->cwd();
    if (EditorPane *e = t->editorById(t->focusedId())) return e->baseDir();
    return s->cwd();
}

QVariantList Workspace::quickOpen(const QString &query, int limit) {
    // the files of the project: git's list when it is a repository (ignored files stay out), else a bounded walk
    const QString dir = currentDir();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (dir != m_filesDir || now - m_filesAt > 10000) {
        m_files.clear();
        QProcess git;
        git.setWorkingDirectory(dir);
        git.start("git", {"ls-files", "--cached", "--others", "--exclude-standard"});
        if (git.waitForFinished(3000) && git.exitStatus() == QProcess::NormalExit && git.exitCode() == 0) {
            for (const QByteArray &l : git.readAllStandardOutput().split('\n'))
                if (!l.isEmpty()) m_files << QString::fromUtf8(l);
        } else {
            static const QSet<QString> skip = {".git", "node_modules", "target", "build", "dist", ".cache", "__pycache__", ".venv"};
            QDirIterator it(dir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
            while (it.hasNext() && m_files.size() < 50000) {
                const QString p = it.next();
                const QString rel = QDir(dir).relativeFilePath(p);
                bool skipped = false;
                for (const QString &part : rel.split('/')) if (skip.contains(part)) { skipped = true; break; }
                if (!skipped && it.fileInfo().isFile()) m_files << rel;
            }
        }
        m_filesDir = dir;
        m_filesAt = now;
    }
    // fuzzy: every query character in order; contiguous runs, word starts and the file name score higher
    const QString q = query.trimmed().toLower();
    struct Hit { int score; QString path; };
    QVector<Hit> hits;
    for (const QString &f : std::as_const(m_files)) {
        if (q.isEmpty()) { hits.append({-int(f.size()), f}); continue; }
        const QString lf = f.toLower();
        const int nameAt = int(lf.lastIndexOf('/')) + 1;
        int score = 0, qi = 0, prev = -2;
        for (int i = 0; i < lf.size() && qi < q.size(); ++i) {
            if (lf[i] != q[qi]) continue;
            score += 1;
            if (i == prev + 1) score += 5;
            if (i == 0 || lf[i - 1] == '/' || lf[i - 1] == '_' || lf[i - 1] == '-' || lf[i - 1] == '.') score += 8;
            if (i >= nameAt) score += 3;
            prev = i;
            ++qi;
        }
        if (qi < q.size()) continue;
        if (lf.mid(nameAt).startsWith(q)) score += 30;
        hits.append({score * 1000 - int(f.size()), f});
    }
    std::partial_sort(hits.begin(), hits.begin() + qMin<qsizetype>(limit, hits.size()), hits.end(),
                      [](const Hit &a, const Hit &b) { return a.score > b.score; });
    QVariantList out;
    for (int i = 0; i < qMin<qsizetype>(limit, hits.size()); ++i)
        out << QVariantMap{{"path", hits[i].path}, {"abs", QDir(dir).absoluteFilePath(hits[i].path)}};
    return out;
}

// ---------------- actions & keybindings ----------------

namespace {
struct ActionInfo { const char *name, *desc; };
const ActionInfo kActions[] = {
    {"split-right", "Split pane to the right"}, {"split-down", "Split pane downwards"},
    {"close-pane", "Close pane"}, {"zoom-pane", "Zoom / unzoom pane"}, {"toggle-views", "Show / hide agent views"},
    {"focus-left", "Focus pane left"}, {"focus-right", "Focus pane right"},
    {"focus-up", "Focus pane above"}, {"focus-down", "Focus pane below"},
    {"new-tab", "New tab"}, {"close-tab", "Close tab"}, {"next-tab", "Next tab"}, {"prev-tab", "Previous tab"},
    {"goto-tab", "Go to tab N"},
    {"new-space", "New space"}, {"rename-space", "Rename space"}, {"kill-session", "Kill all panes and quit (restored next start)"}, {"discard-session", "Kill all panes, forget the session and quit"}, {"close-space", "Close space"}, {"next-space", "Next space"}, {"prev-space", "Previous space"},
    {"goto-space", "Go to space N"},
    {"next-attention", "Jump to agent that needs attention"},
    {"toggle-sidebar", "Show / hide sidebar"}, {"open-settings", "Open settings"}, {"launch-agent", "Launch an agent"}, {"operator", "Open the operator (AI that runs nebula for you)"}, {"broadcast", "Send a prompt to several agents"}, {"toggle-help", "Show / hide this help"}, {"setup-wizard", "Open the setup wizard"},
    {"quick-open", "Open a file (quick open)"}, {"toggle-explorer", "Show / hide the file explorer"},
    {"font-larger", "Increase font size"}, {"font-smaller", "Decrease font size"}, {"font-reset", "Reset font size"},
};

const char *const kDefaults[][2] = {
    {"Ctrl+Shift+D", "split-right"}, {"Ctrl+Shift+E", "split-down"}, {"Ctrl+Shift+W", "close-pane"}, {"Ctrl+Shift+Z", "zoom-pane"}, {"Ctrl+Shift+O", "toggle-views"},
    {"Ctrl+Shift+Left", "focus-left"}, {"Ctrl+Shift+Right", "focus-right"}, {"Ctrl+Shift+Up", "focus-up"}, {"Ctrl+Shift+Down", "focus-down"},
    {"Ctrl+Shift+T", "new-tab"}, {"Ctrl+Tab", "next-tab"}, {"Ctrl+Shift+Tab", "prev-tab"},
    {"Ctrl+PgDown", "next-tab"}, {"Ctrl+PgUp", "prev-tab"},
    {"Alt+1", "goto-tab:1"}, {"Alt+2", "goto-tab:2"}, {"Alt+3", "goto-tab:3"}, {"Alt+4", "goto-tab:4"}, {"Alt+5", "goto-tab:5"},
    {"Alt+6", "goto-tab:6"}, {"Alt+7", "goto-tab:7"}, {"Alt+8", "goto-tab:8"}, {"Alt+9", "goto-tab:9"},
    {"Ctrl+Shift+N", "new-space"}, {"Ctrl+Shift+R", "rename-space"}, {"Ctrl+Shift+Q", "close-space"},
    {"Ctrl+Shift+PgDown", "next-space"}, {"Ctrl+Shift+PgUp", "prev-space"},
    {"Ctrl+Alt+1", "goto-space:1"}, {"Ctrl+Alt+2", "goto-space:2"}, {"Ctrl+Alt+3", "goto-space:3"}, {"Ctrl+Alt+4", "goto-space:4"},
    {"Ctrl+Alt+5", "goto-space:5"}, {"Ctrl+Alt+6", "goto-space:6"}, {"Ctrl+Alt+7", "goto-space:7"}, {"Ctrl+Alt+8", "goto-space:8"},
    {"Ctrl+Alt+9", "goto-space:9"},
    {"Ctrl+Shift+A", "next-attention"}, {"Ctrl+Shift+B", "toggle-sidebar"}, {"Ctrl+,", "open-settings"}, {"Ctrl+Shift+L", "launch-agent"}, {"Ctrl+Shift+I", "operator"}, {"Ctrl+Shift+M", "broadcast"}, {"Ctrl+Shift+?", "toggle-help"},
    {"Ctrl+Shift+P", "quick-open"}, {"Ctrl+E", "toggle-explorer"},
    {"Ctrl++", "font-larger"}, {"Ctrl+=", "font-larger"}, {"Ctrl+-", "font-smaller"}, {"Ctrl+0", "font-reset"},
};

QKeyCombination normalize(QKeyCombination c) {
    Qt::KeyboardModifiers m = c.keyboardModifiers() & ~Qt::KeypadModifier;
    int k = c.toCombined() & ~Qt::KeyboardModifierMask;
    if (k == Qt::Key_Backtab) { k = Qt::Key_Tab; m |= Qt::ShiftModifier; }
    // shifted symbols (?, +, _ ...) already encode Shift in the key itself
    if (k >= 0x20 && k < 0x7f && !(k >= 'A' && k <= 'Z')) m &= ~Qt::ShiftModifier;
    return QKeyCombination(m, Qt::Key(k));
}
} // namespace

namespace {
// "ctrl+shift+d", "Ctrl+Shift+D" and a recorded key event all become the same text
QString canonicalKeys(const QString &keys) {
    const QKeySequence seq = QKeySequence::fromString(keys, QKeySequence::PortableText);
    if (seq.isEmpty() || seq.count() != 1) return {};
    return QKeySequence(normalize(seq[0])).toString(QKeySequence::PortableText);
}

struct ConfLine { QString raw, keys, action; };   // keys empty: a comment, blank or unparsable line, kept as is

QString keysConfPath() { return Paths::configDir() + "/keys.conf"; }

QList<ConfLine> readKeysConf() {
    QList<ConfLine> out;
    QFile f(keysConfPath());
    if (!f.open(QIODevice::ReadOnly)) return out;
    QByteArray all = f.readAll();
    if (all.endsWith('\n')) all.chop(1);
    if (all.isEmpty()) return out;
    for (const QByteArray &raw : all.split('\n')) {
        ConfLine l{QString::fromUtf8(raw), {}, {}};
        const QString line = l.raw.trimmed();
        const int eq = line.indexOf(" = ");
        if (!line.isEmpty() && !line.startsWith('#') && eq > 0) {
            l.keys = canonicalKeys(line.left(eq).trimmed());
            l.action = line.mid(eq + 3).trimmed();
        }
        out << l;
    }
    return out;
}

bool writeKeysConf(const QList<ConfLine> &lines) {
    QDir().mkpath(Paths::configDir());
    QFile f(keysConfPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QByteArray out;
    bool header = false;
    for (const ConfLine &l : lines) header |= l.raw.startsWith("# nebula key bindings");
    if (!header) out += "# nebula key bindings: <keys> = <action>   (use 'none' to unbind)\n";
    for (const ConfLine &l : lines) out += l.raw.toUtf8() + '\n';
    return f.write(out) == out.size();
}

// keys -> action after the defaults and then every keys.conf line, in order (a later line wins). Keys are canonical;
// *display keeps the text each winning entry was written with ("Ctrl+Shift+?" rather than the canonical "Ctrl+?").
QHash<QString, QString> effectiveBindings(const QList<ConfLine> &lines, QStringList *order = nullptr, QHash<QString, QString> *display = nullptr) {
    QHash<QString, QString> table;
    auto put = [&](const QString &keys, const QString &action, const QString &text) {
        if (keys.isEmpty()) return;
        if (order && !table.contains(keys)) *order << keys;
        table[keys] = action;
        if (display) (*display)[keys] = text;
    };
    for (auto &d : kDefaults) put(canonicalKeys(d[0]), d[1], d[0]);
    for (const ConfLine &l : lines) put(l.keys, l.action, l.raw.trimmed().section(" = ", 0, 0).trimmed());
    return table;
}

QStringList defaultKeysOf(const QString &action) {
    QStringList out;
    for (auto &d : kDefaults)
        if (action == d[1]) out << canonicalKeys(d[0]);
    return out;
}

QString actionDesc(const QString &action) {
    for (auto &a : kActions)
        if (action.section(':', 0, 0) == a.name) return a.desc;
    return action;
}
} // namespace

void Workspace::loadBindings() {
    m_bindings.clear();
    QStringList order;
    const QList<ConfLine> lines = readKeysConf();
    for (const ConfLine &l : lines)
        if (!l.raw.trimmed().isEmpty() && !l.raw.trimmed().startsWith('#') && l.raw.contains(" = ") && l.keys.isEmpty())
            qWarning("nebula: invalid key binding '%s'", qPrintable(l.raw.trimmed()));
    QHash<QString, QString> display;
    const QHash<QString, QString> table = effectiveBindings(lines, &order, &display);
    for (const QString &keys : std::as_const(order)) {
        const QString action = table[keys];
        if (action == "none" || action.isEmpty()) continue;
        const QKeySequence seq = QKeySequence::fromString(keys, QKeySequence::PortableText);
        m_bindings.append({normalize(seq[0]), display.value(keys, keys), action});
    }
}

QVariantList Workspace::keymap() const {
    QVariantList out;
    const QList<ConfLine> lines = readKeysConf();
    for (auto &a : kActions) {
        const QString name = a.name;
        const bool numbered = name == "goto-tab" || name == "goto-space";   // nine bindings each: keys.conf only
        QStringList keys, defaults;
        for (const Binding &b : m_bindings) {
            if (numbered ? b.action.startsWith(name + ":") : b.action == name) keys << b.keys;
        }
        for (auto &d : kDefaults)
            if (numbered ? QString(d[1]).startsWith(name + ":") : name == d[1]) defaults << d[0];
        auto canonical = [](const QStringList &l) { QSet<QString> out; for (const QString &k : l) out.insert(canonicalKeys(k)); return out; };
        const bool changed = canonical(keys) != canonical(defaults);
        if (numbered) {
            auto collapse = [](const QStringList &l) {
                for (const QString &k : l) if (k.endsWith("+1")) return QStringList{k.chopped(1) + "1-9"};
                return l.isEmpty() ? QStringList() : QStringList{l.first()};
            };
            keys = collapse(keys);
            defaults = collapse(defaults);
        }
        out << QVariantMap{{"action", name}, {"desc", QString(a.desc).replace(" N", numbered ? " 1-9" : " N")}, {"keys", keys},
                           {"defaults", defaults}, {"changed", changed}, {"editable", !numbered}};
    }
    return out;
}

QString Workspace::keysFor(int key, int modifiers) const {
    if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_AltGr
        || key == Qt::Key_Super_L || key == Qt::Key_Super_R || key == Qt::Key_unknown || key == 0)
        return {};
    const QKeyCombination c = normalize(QKeyCombination(Qt::KeyboardModifiers(modifiers), Qt::Key(key)));
    return QKeySequence(c).toString(QKeySequence::PortableText);
}

QVariantMap Workspace::bindingOwner(const QString &keys) const {
    const QString k = canonicalKeys(keys);
    for (const Binding &b : m_bindings)
        if (canonicalKeys(b.keys) == k) return {{"action", b.action}, {"desc", actionDesc(b.action)}};
    return {};
}

bool Workspace::addBinding(const QString &action, const QString &keys) {
    const QString k = canonicalKeys(keys);
    if (k.isEmpty() || action.isEmpty()) return false;
    QList<ConfLine> lines = readKeysConf();
    lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const ConfLine &l) { return l.keys == k; }), lines.end());
    if (effectiveBindings(lines).value(k) != action) lines << ConfLine{k + " = " + action, k, action};
    writeKeysConf(lines);
    reloadBindings();
    return true;
}

void Workspace::removeBinding(const QString &action, const QString &keys) {
    const QString k = canonicalKeys(keys);
    QList<ConfLine> lines = readKeysConf();
    lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const ConfLine &l) { return l.keys == k && l.action == action; }), lines.end());
    if (effectiveBindings(lines).value(k) == action) lines << ConfLine{k + " = none", k, "none"};
    writeKeysConf(lines);
    reloadBindings();
}

void Workspace::resetBinding(const QString &action) {
    const QStringList defaults = defaultKeysOf(action);
    QList<ConfLine> lines = readKeysConf();
    // drop every override that touches this action: its own lines and anything rebinding or unbinding its defaults
    lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const ConfLine &l) {
        return !l.keys.isEmpty() && (l.action == action || defaults.contains(l.keys));
    }), lines.end());
    writeKeysConf(lines);
    reloadBindings();
}

void Workspace::resetAllBindings() {
    QList<ConfLine> lines = readKeysConf();
    lines.erase(std::remove_if(lines.begin(), lines.end(), [](const ConfLine &l) { return !l.keys.isEmpty(); }), lines.end());
    writeKeysConf(lines);
    reloadBindings();
}

QVariantList Workspace::bindings() const {
    QHash<QString, QString> desc;
    for (auto &a : kActions) desc[a.name] = a.desc;
    QVariantList out;
    QHash<QString, QStringList> grouped;
    QStringList order;
    for (const Binding &b : m_bindings) {
        const QString name = b.action.section(':', 0, 0);
        QString d = desc.value(name, name);
        QString keys = b.keys;
        if (b.action.contains(':')) {
            if (b.action.section(':', 1) != "1") continue;
            d = d.replace('N', "1-9");
            keys = keys.left(keys.size() - 1) + "1-9";
        }
        if (!grouped.contains(d)) order << d;
        grouped[d] << keys;
    }
    for (const QString &d : std::as_const(order)) out << QVariantMap{{"desc", d}, {"keys", grouped[d]}};
    return out;
}

bool Workspace::eventFilter(QObject *o, QEvent *e) {
    if (e->type() != QEvent::KeyPress || !qobject_cast<QQuickWindow *>(o)) return false;
    if (m_capturing) return false;   // the settings page is recording a shortcut
    auto *ke = static_cast<QKeyEvent *>(e);
    const QKeyCombination c = normalize(ke->keyCombination());
    if (m_help && c.key() == Qt::Key_Escape) { hideHelp(); return true; }
    if (modalVisible() && m_overlay.isEmpty() && c.key() == Qt::Key_Escape && c.keyboardModifiers() == Qt::NoModifier) { setModalHidden(true); return true; }
    if (!m_overlay.isEmpty() && c.key() == Qt::Key_Escape && c.keyboardModifiers() == Qt::NoModifier) { hideOverlay(); return true; }
    if (m_settings && c.key() == Qt::Key_Escape && c.keyboardModifiers() == Qt::NoModifier) {
        auto *fi = static_cast<QQuickWindow *>(o)->activeFocusItem();
        if (!(fi && fi->inherits("QQuickTextInput"))) { setSettingsVisible(false); return true; }
    }
    for (const Binding &b : std::as_const(m_bindings))
        if (b.combo == c) return runAction(b.action);
    return false;
}

bool Workspace::runAction(const QString &spec) {
    const QString name = spec.section(':', 0, 0);
    const int arg = spec.section(':', 1).toInt();
    Space *s = currentSpace();
    Tab *t = s ? s->currentTab() : nullptr;
    const int ns = int(m_spaces.size());
    if (name == "toggle-sidebar") { m_sidebar = !m_sidebar; emit viewChanged(); return true; }
    if (name == "launch-agent") { showOverlay("launcher"); return true; }
    if (name == "operator") { showOverlay("operator"); return true; }
    if (name == "broadcast") { showOverlay("broadcast"); return true; }
    if (name == "open-settings") { setSettingsVisible(!m_settings); return true; }
    if (name == "setup-wizard") { showOverlay("onboarding"); return true; }
    if (name == "quick-open") { showOverlay("quickopen"); return true; }
    if (name == "toggle-explorer") {
        const bool open = !Settings::instance()->property("explorerOpen").toBool();
        Settings::instance()->setProperty("explorerOpen", open);
        if (!open) emit focusRequested();   // the keyboard goes back to the pane it came from
        return true;
    }
    if (name == "toggle-help") { m_help = !m_help; emit viewChanged(); return true; }
    if (name == "toggle-views") { if (m_modals.isEmpty()) return false; setModalHidden(!m_modalHidden); return true; }
    if (name == "font-larger") { m_theme->zoom(1); return true; }
    if (name == "font-smaller") { m_theme->zoom(-1); return true; }
    if (name == "font-reset") { m_theme->resetZoom(); return true; }
    if (name == "kill-session") { killSession(); return true; }
    if (name == "discard-session") { discardSession(); return true; }
    if (name == "new-space") { newSpace(); return true; }
    if (name == "rename-space") { emit renameRequested(m_current); return true; }
    if (name == "close-space") { closeSpace(m_current); return true; }
    if (name == "next-space") { setCurrent((m_current + 1) % qMax(1, ns)); return true; }
    if (name == "prev-space") { setCurrent((m_current + ns - 1) % qMax(1, ns)); return true; }
    if (name == "goto-space") { setCurrent(arg - 1); return true; }
    if (name == "next-attention") {
        for (const char *want : {"blocked", "done"})
            for (const QVariant &v : std::as_const(m_agents)) {
                const QVariantMap m = v.toMap();
                if (m["state"].toString() == want) { focusAgent(m["spaceIndex"].toInt(), m["tabIndex"].toInt(), m["pane"].toInt()); return true; }
            }
        return true;
    }
    if (!s) return false;
    if (name == "new-tab") { s->newTab(); return true; }
    if (name == "close-tab") { s->closeTab(s->currentIndex()); return true; }
    if (name == "next-tab") { s->step(1); return true; }
    if (name == "prev-tab") { s->step(-1); return true; }
    if (name == "goto-tab") { s->setCurrent(arg - 1); return true; }
    if (!t) return false;
    if (name == "split-right") { t->split(true); return true; }
    if (name == "split-down") { t->split(false); return true; }
    if (name == "close-pane") { t->closePane(t->focusedId()); return true; }
    if (name == "zoom-pane") { t->toggleZoom(); return true; }
    if (name == "focus-left") { t->focusDirection(-1, 0); return true; }
    if (name == "focus-right") { t->focusDirection(1, 0); return true; }
    if (name == "focus-up") { t->focusDirection(0, -1); return true; }
    if (name == "focus-down") { t->focusDirection(0, 1); return true; }
    return false;
}

void Workspace::reloadBindings() {
    loadBindings();
    emit bindingsChanged();
}

void Workspace::openKeysConfig() {
    const QString path = Paths::configDir() + "/keys.conf";
    if (!QFileInfo::exists(path)) {
        QDir().mkpath(Paths::configDir());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write("# nebula key bindings: <keys> = <action>   (use 'none' to unbind)\n# Example:\n# Ctrl+Shift+D = split-right\n# Alt+Left = focus-left\n");
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void Workspace::showOverlay(const QString &name, const QVariantMap &data) {
    m_overlay = name;
    m_overlayData = data;
    m_help = false;
    emit viewChanged();
}

void Workspace::hideOverlay() {
    if (m_overlay.isEmpty()) return;
    m_overlay.clear();
    m_overlayData.clear();
    emit viewChanged();
    emit focusRequested();
}

void Workspace::copyToClipboard(const QString &text) { QGuiApplication::clipboard()->setText(text); }

void Workspace::setSpaceProfile(int i, const QString &profile) {
    if (i >= 0 && i < m_spaces.size()) m_spaces[i]->setProfile(profile);
}
