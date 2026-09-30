#pragma once
#include "terminalsession.h"
#include <QList>
#include <QJsonObject>
#include <QObject>
#include <QJsonArray>
#include <QRectF>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>
#include <memory>

class Space;
class Workspace;
class ViewPane;

class Tab : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap layout READ layout NOTIFY layoutChanged)
    Q_PROPERTY(int focusedId READ focusedId NOTIFY focusChanged)
    Q_PROPERTY(QString title READ title NOTIFY activity)
    Q_PROPERTY(bool zoomed READ zoomed NOTIFY layoutChanged)
public:
    explicit Tab(const QString &cwd, const QString &profile, QObject *parent = nullptr);
    ~Tab() override;
    static Tab *restore(const QJsonObject &o, const QSet<int> &livePanes, QObject *parent);
    QJsonObject toJson() const;
    void terminateAll();
    QVariantMap layout() const;
    int focusedId() const { return m_focus; }
    bool zoomed() const { return m_zoom; }
    QString title() const;
    QList<TerminalSession *> sessions() const {
        auto l = m_sessions.values();
        std::sort(l.begin(), l.end(), [](auto *a, auto *b) { return a->id() < b->id(); });
        return l;
    }
    TerminalSession *focusedSession() const { return m_sessions.value(m_focus); }   // null when a view is focused
    QList<ViewPane *> views() const;
    bool hasPane(int id) const { return m_sessions.contains(id) || m_views.contains(id); }

    Q_INVOKABLE QObject *session(int id) const { return m_sessions.value(id); }
    Q_INVOKABLE QObject *view(int id) const;
    TerminalSession *sessionById(int id) const { return m_sessions.value(id); }
    ViewPane *viewById(int id) const { return m_views.value(id); }
    // Puts a view next to `anchorPane` (right or below). Focus stays where it is: a view must not take the
    // keyboard away from the agent that produced it.
    bool addView(ViewPane *v, int anchorPane, bool sideBySide);
    static Tab *withView(ViewPane *v, QObject *parent);   // a new tab holding only this view
    Q_INVOKABLE int split(bool sideBySide);
    int splitWith(bool sideBySide, const QString &cwd, const QString &profile);
    Q_INVOKABLE void closePane(int id);
    Q_INVOKABLE void focusPane(int id);
    Q_INVOKABLE void focusDirection(int dx, int dy);
    Q_INVOKABLE void focusCycle(int delta);
    Q_INVOKABLE void setRatio(int nodeId, double ratio);
    Q_INVOKABLE void toggleZoom();

signals:
    void layoutChanged();
    void focusChanged();
    void activity();
    void empty();
    void attention(TerminalSession *s, const QString &kind);

private:
    struct Node {
        int id = 0, pane = -1;
        bool horizontal = true;
        double ratio = 0.5;
        Node *a = nullptr, *b = nullptr, *parent = nullptr;
        ~Node() { delete a; delete b; }
        bool leaf() const { return pane >= 0; }
    };
    struct RestoreTag {};
    explicit Tab(RestoreTag, QObject *parent) : QObject(parent) {}
    TerminalSession *makeSession(const QString &cwd, int id = 0, bool attach = false, const QString &prefill = QString(), const QString &profile = QString());
    void adoptView(ViewPane *v);
    Node *find(Node *n, int pane) const;
    QVariantMap toVariant(const Node *n) const;
    void rects(const Node *n, QRectF r, QHash<int, QRectF> &out) const;
    void leaves(const Node *n, QList<int> &out) const;

    Node *m_root = nullptr;
    QHash<int, TerminalSession *> m_sessions;
    QHash<int, ViewPane *> m_views;
    int m_focus = -1;
    bool m_zoom = false;
    int m_nodeSeq = 1;
    QString m_profile;
};

class Workspace;

class Space : public QObject {
    Q_OBJECT
    friend class Workspace;
    Q_PROPERTY(QString name READ name NOTIFY activity)
    Q_PROPERTY(bool customName READ hasCustomName NOTIFY activity)
    Q_PROPERTY(QString branch READ branch NOTIFY activity)
    Q_PROPERTY(QString state READ state NOTIFY activity)
    Q_PROPERTY(QString profile READ profile NOTIFY activity)
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(QObject *currentTab READ currentTab NOTIFY currentChanged)
public:
    explicit Space(const QString &cwd, QObject *parent = nullptr);
    static Space *restore(const QJsonObject &o, const QSet<int> &livePanes, QObject *parent);
    QJsonObject toJson() const;
    void terminateAll();
    QString name() const;
    QString customName() const { return m_custom; }
    bool hasCustomName() const { return !m_custom.isEmpty(); }
    Q_INVOKABLE void rename(const QString &name);
    QString cwd() const;
    QString branch() const;
    QString state() const;
    QVariantList tabs() const;
    int currentIndex() const { return m_current; }
    Tab *currentTab() const { return m_tabs.value(m_current); }
    const QList<Tab *> &tabList() const { return m_tabs; }

    Q_INVOKABLE void newTab();
    int newTabWith(const QString &cwd, const QString &profile);
    QString profile() const { return m_profile; }
    Q_INVOKABLE void setProfile(const QString &p) { m_profile = p; emit activity(); }
    Q_INVOKABLE void closeTab(int i);
    Q_INVOKABLE void setCurrent(int i);
    void step(int delta);

signals:
    void tabsChanged();
    void currentChanged();
    void activity();
    void empty();
    void attention(TerminalSession *s, const QString &kind);

private:
    struct RestoreTag {};
    explicit Space(RestoreTag, QObject *parent) : QObject(parent) {}
    Tab *addTab(const QString &cwd);
    void adopt(Tab *t);
    QList<Tab *> m_tabs;
    int m_current = 0;
    QString m_custom, m_cwd, m_profile;
};

class Theme;

class Workspace : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList spaces READ spaces NOTIFY spacesChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(QObject *currentSpace READ currentSpace NOTIFY currentChanged)
    Q_PROPERTY(QVariantList agents READ agents NOTIFY agentsChanged)
    Q_PROPERTY(QVariantList bindings READ bindings NOTIFY bindingsChanged)
    Q_PROPERTY(bool settingsVisible READ settingsVisible NOTIFY viewChanged)
    Q_PROPERTY(bool sidebarVisible READ sidebarVisible NOTIFY viewChanged)
    Q_PROPERTY(bool helpVisible READ helpVisible NOTIFY viewChanged)
    Q_PROPERTY(QString overlay READ overlay NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap overlayData READ overlayData NOTIFY viewChanged)
public:
    explicit Workspace(Theme *theme, QObject *parent = nullptr);
    QVariantList spaces() const;
    const QList<Space *> &spaceList() const { return m_spaces; }
    int currentIndex() const { return m_current; }
    Space *currentSpace() const { return m_spaces.value(m_current); }
    QVariantList agents() const { return m_agents; }
    QVariantList bindings() const;
    bool sidebarVisible() const { return m_sidebar; }
    bool helpVisible() const { return m_help; }
    QString overlay() const { return m_overlay; }
    QVariantMap overlayData() const { return m_overlayData; }
    Q_INVOKABLE void showOverlay(const QString &name, const QVariantMap &data = {});
    Q_INVOKABLE void hideOverlay();

    Q_INVOKABLE void copyToClipboard(const QString &text);
    Q_INVOKABLE void setSpaceProfile(int i, const QString &profile);
    bool settingsVisible() const { return m_settings; }

    Q_INVOKABLE int newSpace(const QString &cwd = QString(), const QString &name = QString(), const QString &profile = QString());
    Q_INVOKABLE void renameSpace(int i, const QString &name);
    Q_INVOKABLE void closeSpace(int i);
    Q_INVOKABLE void setCurrent(int i);
    Q_INVOKABLE void focusAgent(int spaceIdx, int tabIdx, int paneId);
    Q_INVOKABLE bool runAction(const QString &spec);
    Q_INVOKABLE void setSettingsVisible(bool v) { if (m_settings != v) { m_settings = v; if (!v) emit focusRequested(); emit viewChanged(); } }
    Q_INVOKABLE void reloadBindings();
    Q_INVOKABLE void openKeysConfig();
    Q_INVOKABLE void hideHelp() { if (m_help) { m_help = false; emit viewChanged(); } }
    void restore();
    Q_INVOKABLE void killSession();
    void saveNow();

    // locate a pane by its global id; returns null if it does not exist
    TerminalSession *findPane(int id, int *spaceIdx = nullptr, int *tabIdx = nullptr, Tab **tab = nullptr) const;
    ViewPane *findView(int id, int *spaceIdx = nullptr, int *tabIdx = nullptr, Tab **tab = nullptr) const;
    Tab *tabOfPane(int id, int *spaceIdx = nullptr, int *tabIdx = nullptr) const;   // any pane kind
    // Shows a view next to `anchorPane` ("right" / "down") or in a new tab of that pane's space ("tab").
    bool placeView(ViewPane *v, int anchorPane, const QString &where);
    TerminalSession *focusedPane() const;

signals:
    void spacesChanged();
    void currentChanged();
    void agentsChanged();
    void viewChanged();
    void bindingsChanged();
    void agentEvent(const QJsonObject &ev);
    void renameRequested(int index);
    void focusRequested();

private:
    struct Binding {
        QKeyCombination combo;
        QString keys, action;
    };
    void refresh();
    void rebuildAgents();
    void scheduleSave();
    void adopt(Space *s);
    void loadBindings();
    bool eventFilter(QObject *o, QEvent *e) override;

    Theme *m_theme;
    QList<Space *> m_spaces;
    QList<Binding> m_bindings;
    int m_current = 0;
    QVariantList m_agents;
    bool m_sidebar = true, m_help = false, m_settings = false;
    QString m_overlay;
    QVariantMap m_overlayData;
    QByteArray m_saved;
    QTimer m_saveTimer;
};
