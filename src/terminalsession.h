#pragma once
#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QSocketNotifier>
#include <QTimer>
#include <deque>
#include <vector>
#include <vterm.h>

class QKeyEvent;

class TerminalSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(int id READ id CONSTANT)
    Q_PROPERTY(QString label READ label NOTIFY metaChanged)
    Q_PROPERTY(QString cwd READ cwd NOTIFY metaChanged)
    Q_PROPERTY(QString branch READ branch NOTIFY metaChanged)
    Q_PROPERTY(QString agent READ agent NOTIFY metaChanged)
    Q_PROPERTY(QString agentState READ agentState NOTIFY metaChanged)
    Q_PROPERTY(QString title READ title NOTIFY metaChanged)
    Q_PROPERTY(QString profile READ profile CONSTANT)
    Q_PROPERTY(QString summary READ summary NOTIFY metaChanged)
public:
    // fixedId/attach: reuse a pane that already lives in the host (session restore). prefill is typed at the prompt (not executed).
    TerminalSession(const QString &cwd, QObject *parent = nullptr, int fixedId = 0, bool attach = false, const QString &prefill = QString(), const QString &profile = QString());
    ~TerminalSession() override;

    int id() const { return m_id; }
    QString label() const;
    QString cwd() const { return m_cwd; }
    QString branch() const { return m_branch; }
    QString agent() const { return m_agent; }
    QString agentState() const { return m_state; }
    QString title() const { return QString::fromUtf8(m_title); }
    void setActive(bool active);
    bool active() const { return m_active; }
    void reportState(const QString &state, int ttlSeconds);
    QString readText(int lines, bool scrollback) const;
    QString tailText(int maxLines) const;
    void terminate();
    QString profile() const { return m_profile; }
    QString summary() const { return m_summary; }
    void setSummary(const QString &s) { if (s != m_summary) { m_summary = s; emit metaChanged(); } }
    QString resumeCommand() const;
    // callbacks from HostClient
    void hostOutput(const QByteArray &d);
    void hostSize(int rows, int cols);
    void hostReplayDone();
    void hostExit(int status);
    void hostInfo(qint64 pid, qint64 fg);
    void hostGone();
    void injectKey(int key, int qtMods, const QString &text);

    int rows() const { return m_rows; }
    int cols() const { return m_cols; }
    int scrollbackSize() const { return int(m_sb.size()); }
    bool cursorVisible() const { return m_cursorVisible; }
    VTermPos cursor() const { return m_cursor; }
    int cursorShape() const { return m_cursorShape; }
    bool altScreen() const { return m_altScreen; }
    int mouseMode() const { return m_mouse; }
    bool appCursor() const { return m_appCursor; }

    void cellAt(int absRow, int col, VTermScreenCell &cell) const;
    void resize(int rows, int cols);
    void keyPress(QKeyEvent *e);
    void sendText(const QString &text);
    void paste(const QString &text);
    void mouseMove(int row, int col, int mods);
    void mouseButton(int button, bool pressed, int mods);
    QString textRange(int r0, int c0, int r1, int c1) const;

signals:
    void updated();
    void metaChanged();
    void finished();
    void attention(const QString &kind);

private:
    void start(const QString &cwd, bool attach);
    void writeOut(const char *s, size_t len);
    void poll();
    void setState(const QString &s);

    static int cbDamage(VTermRect, void *);
    static int cbMoveCursor(VTermPos, VTermPos, int, void *);
    static int cbSetProp(VTermProp, VTermValue *, void *);
    static int cbBell(void *);
    static int cbPush(int, const VTermScreenCell *, void *);
    static int cbPop(int, VTermScreenCell *, void *);
    static int cbSbClear(void *);
    static void cbOutput(const char *, size_t, void *);
    static int cbCsi(const char *leader, const long args[], int argcount, const char *intermed, char command, void *);
    void reply(const char *s);

    int m_id;
    VTerm *m_vt = nullptr;
    VTermScreen *m_screen = nullptr;
    qint64 m_pid = -1;
    bool m_replaying = false;
    QByteArray m_sgrCarry;
    QByteArray translateFaint(const QByteArray &d);
    int m_wantRows = 0, m_wantCols = 0;
    QString m_prefill, m_cmdline, m_profile, m_summary;
    int m_rows = 24, m_cols = 80;
    std::deque<std::vector<VTermScreenCell>> m_sb;
    VTermPos m_cursor{0, 0};
    bool m_cursorVisible = true, m_altScreen = false, m_appCursor = false;
    int m_mouse = 0, m_cursorShape = 1;
    QByteArray m_title;
    QString m_cwd, m_branch, m_agent, m_state, m_proc;
    bool m_active = false, m_sawWorking = false, m_alive = true, m_terminated = false;
    int m_notWorkingPolls = 0;
    QString m_reported;
    QElapsedTimer m_reportAge;
    int m_reportTtl = 0;
    QTimer m_repaint, m_poll;
    QElapsedTimer m_lastOutput;
};
