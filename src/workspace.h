#pragma once
#include "terminalsession.h"
#include <QList>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
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
class EditorPane;

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
    QList<EditorPane *> editors() const;
    bool hasPane(int id) const { return m_sessions.contains(id) || m_views.contains(id) || m_editors.contains(id); }

    Q_INVOKABLE QObject *session(int id) const { return m_sessions.value(id); }
    Q_INVOKABLE QObject *view(int id) const;
    Q_INVOKABLE QObject *editor(int id) const;
    EditorPane *editorById(int id) const { return m_editors.value(id); }
    EditorPane *dirtyEditor() const;   // one with unsaved changes, if any
    // Puts an editor next to `anchorPane` and focuses it (opening a file is asking to type in it).
    bool addEditor(EditorPane *e, int anchorPane, bool sideBySide);
    static Tab *withEditor(EditorPane *e, QObject *parent);
    TerminalSession *sessionById(int id) const { return m_sessions.value(id); }
    ViewPane *viewById(int id) const { return m_views.value(id); }
    // Puts a view next to `anchorPane` (right or below). Focus stays where it is: a view must not take the
    // keyboard away from the agent that produced it.
    bool addView(ViewPane *v, int anchorPane, bool sideBySide);
    static Tab *withView(ViewPane *v, QObject *parent);   // a new tab holding only this view
    Q_INVOKABLE int split(bool sideBySide);
    int splitWith(bool sideBySide, const QString &cwd, const QString &profile);
    // Closes a pane; a modified editor asks first (save, discard or cancel) and closes when answered.
    Q_INVOKABLE void closePane(int id);
    void closePaneNow(int id) { removePane(id, true); }
    ViewPane *takeView(int id);   // removes a view from the layout without closing it (to show it as a modal)
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
    TerminalSession *makeSession(const QString &cwd, int id = 0, bool attach = false, const QString &prefill = QString(), const QString &profile = QString(),
                                 bool autorun = false);
    void adoptView(ViewPane *v);
    void adoptEditor(EditorPane *e);
    bool addLeaf(int pane, int anchorPane, bool sideBySide);
    QString kindOf(int pane) const;
    ViewPane *removePane(int id, bool destroy);
    Node *find(Node *n, int pane) const;
    QVariantMap toVariant(const Node *n) const;
    void rects(const Node *n, QRectF r, QHash<int, QRectF> &out) const;
    void leaves(const Node *n, QList<int> &out) const;

    Node *m_root = nullptr;
    QHash<int, TerminalSession *> m_sessions;
    QHash<int, ViewPane *> m_views;
    QHash<int, EditorPane *> m_editors;
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
    Q_PROPERTY(QObject *modalView READ modalView NOTIFY modalChanged)
    Q_PROPERTY(int modalCount READ modalCount NOTIFY modalChanged)
    Q_PROPERTY(bool modalVisible READ modalVisible NOTIFY modalChanged)
    // while the settings page records a shortcut, key presses go to it instead of running bindings
    Q_PROPERTY(bool capturingKeys READ capturingKeys WRITE setCapturingKeys NOTIFY capturingKeysChanged)
    Q_PROPERTY(bool exiting READ exiting NOTIFY exitingChanged)   // read when the window is asked to close
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

    // Views shown as a modal over the workspace (the default placement). A stack: the newest is on top.
    // Hiding keeps them (Esc, Ctrl+Shift+O, the status bar brings them back); closing discards the top one.
    QObject *modalView() const;
    int modalCount() const { return int(m_modals.size()); }
    bool modalVisible() const { return !m_modals.isEmpty() && !m_modalHidden; }
    void showModal(ViewPane *v, int anchorPane);
    Q_INVOKABLE void closeModal();
    Q_INVOKABLE void setModalHidden(bool hidden);
    Q_INVOKABLE bool dockModal(const QString &where);   // top modal -> split next to its anchor ("right"/"down") or a tab
    Q_INVOKABLE void popOutView(int viewId);           // docked view -> modal
    bool closeView(int id);                            // docked or modal

    // Editors. openEditor focuses the file when it is already open; `where` is right | down | tab (default: right
    // of the focused pane). Returns {pane} or {error}.
    Q_INVOKABLE QVariantMap openEditor(const QString &path, int line = 0, const QString &where = QString(), int anchorPane = -1);
    EditorPane *findEditor(int id, int *spaceIdx = nullptr, int *tabIdx = nullptr, Tab **tab = nullptr) const;
    QList<EditorPane *> allEditors() const;
    Q_INVOKABLE void editView(int viewId);         // the file a view shows, in an editor next to it
    // What the explorer does with a click: .neb / Markdown as a view (placed like agent views), anything else in the
    // editor, reusing the current tab's editor when it has no unsaved changes, as an IDE does. Returns {pane} or {error}.
    Q_INVOKABLE QVariantMap openFile(const QString &path);
    Q_INVOKABLE QString projectDir() const;        // the explorer's root: the current space's git work tree or folder
    Q_INVOKABLE void terminalAt(const QString &dir);   // a new tab with a shell in dir
    Q_INVOKABLE void previewEditor(int editorId);  // a Markdown / .neb editor shown as a view next to it, live
    void linkPreview(EditorPane *e, ViewPane *v);
    // Quick open (Ctrl+P): files under the current space's directory that match `query`, best first.
    Q_INVOKABLE QVariantList quickOpen(const QString &query, int limit = 40);
    Q_INVOKABLE QString currentDir() const;
    bool isModal(const ViewPane *v) const;
    QList<ViewPane *> modals() const;

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
    // Editing bindings from the settings page. Changes are written to keys.conf (comments and other lines kept)
    // as the smallest set of overrides on top of the defaults.
    Q_INVOKABLE QVariantList keymap() const;                     // [{action, desc, keys[], defaults[], changed, editable}]
    Q_INVOKABLE QString keysFor(int key, int modifiers) const;   // a key event as "Ctrl+Shift+D", "" for a bare modifier
    Q_INVOKABLE QVariantMap bindingOwner(const QString &keys) const;   // {action, desc} bound to these keys, or {}
    Q_INVOKABLE bool addBinding(const QString &action, const QString &keys);
    Q_INVOKABLE void removeBinding(const QString &action, const QString &keys);
    Q_INVOKABLE void resetBinding(const QString &action);
    Q_INVOKABLE void resetAllBindings();
    bool capturingKeys() const { return m_capturing; }
    void setCapturingKeys(bool on) { if (on != m_capturing) { m_capturing = on; emit capturingKeysChanged(); } }
    Q_INVOKABLE void openKeysConfig();
    Q_INVOKABLE void hideHelp() { if (m_help) { m_help = false; emit viewChanged(); } }
    void restore();
    // Terminates every pane and quits. The layout, each pane's program and each agent's conversation are kept and
    // come back on the next start; discardSession forgets them instead.
    Q_INVOKABLE void killSession();
    Q_INVOKABLE void discardSession();
    // Quits the window on purpose. The window turns down close requests to stay in the tray, and Qt's quit() asks
    // every window first, so quitting must say it means it.
    Q_INVOKABLE void exitApp();
    bool exiting() const { return m_exiting; }
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
    void modalChanged();
    void capturingKeysChanged();
    void exitingChanged();

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
    bool m_quitting = false;   // after killSession: panes closing on the way out must not be saved
    bool m_exiting = false;
    bool m_sidebar = true, m_help = false, m_settings = false, m_capturing = false;
    QList<ViewPane *> m_modals;
    bool m_modalHidden = false;
    QString m_overlay;
    QVariantMap m_overlayData;
    QByteArray m_saved;
    QHash<int, QPointer<ViewPane>> m_previews;   // editor id -> the view showing it live
    QString m_filesDir;                          // quick open's file list and when it was read
    QStringList m_files;
    qint64 m_filesAt = 0;
    QTimer m_saveTimer;
};
