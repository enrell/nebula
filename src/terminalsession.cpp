#include "terminalsession.h"
#include "agentdetect.h"
#include "paths.h"
#include "settings.h"
#include "hostclient.h"
#include "profiles.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

static int s_nextId = 1;

void TerminalSession::reserveId(int id) { if (id >= s_nextId) s_nextId = id + 1; }

TerminalSession::TerminalSession(const QString &cwd, QObject *parent, int fixedId, bool attach, const QString &prefill, const QString &profile)
    : QObject(parent), m_id(fixedId > 0 ? fixedId : s_nextId++), m_cwd(cwd), m_prefill(prefill), m_profile(profile) {
    reserveId(m_id);
    m_vt = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vt, 1);
    vterm_output_set_callback(m_vt, cbOutput, this);
    m_screen = vterm_obtain_screen(m_vt);
    static const VTermScreenCallbacks cbs = {cbDamage, nullptr, cbMoveCursor, cbSetProp, cbBell, nullptr, cbPush, cbPop, cbSbClear};
    vterm_screen_set_callbacks(m_screen, &cbs, this);
    vterm_screen_enable_altscreen(m_screen, 1);
    vterm_screen_reset(m_screen, 1);

    m_repaint.setSingleShot(true);
    m_repaint.setInterval(8);
    connect(&m_repaint, &QTimer::timeout, this, &TerminalSession::updated);
    m_poll.setInterval(400);
    connect(&m_poll, &QTimer::timeout, this, &TerminalSession::poll);
    m_lastOutput.start();
    start(cwd, attach);
    m_poll.start();
}

TerminalSession::~TerminalSession() {
    HostClient::instance()->unregisterPane(m_id);
    vterm_free(m_vt);
}

// Close the pane for good (the shell is killed). Destroying the object only detaches.
void TerminalSession::terminate() {
    if (!m_terminated) HostClient::instance()->kill(m_id);
    m_terminated = true;
    m_alive = false;
}

void TerminalSession::start(const QString &cwd, bool attach) {
    auto *host = HostClient::instance();
    host->registerPane(m_id, this);
    if (attach) {
        m_replaying = true;
        host->attach(m_id);
    } else {
        QString shell;
        if (Settings::instance()) shell = Settings::instance()->shell();
        host->spawn(m_id, cwd, shell, m_rows, m_cols, Profiles::instance() ? Profiles::instance()->envFor(m_profile) : QStringList());
        host->attach(m_id);
        if (!m_prefill.isEmpty()) {
            const QByteArray note = "\x1b[2m[nebula] session restored - press Enter to resume:\x1b[0m\r\n";
            vterm_input_write(m_vt, note.constData(), size_t(note.size()));
            QTimer::singleShot(350, this, [this] { if (m_alive) sendText(m_prefill); });
        }
    }
}

void TerminalSession::hostOutput(const QByteArray &d) {
    vterm_input_write(m_vt, d.constData(), size_t(d.size()));
    if (!m_replaying) m_lastOutput.restart();
    vterm_screen_flush_damage(m_screen);
    if (!m_repaint.isActive()) m_repaint.start();
}

void TerminalSession::hostSize(int rows, int cols) {
    if (rows < 1 || cols < 2) return;
    m_rows = rows;
    m_cols = cols;
    vterm_set_size(m_vt, rows, cols);
}

void TerminalSession::hostReplayDone() {
    m_replaying = false;
    m_lastOutput.restart();
    emit updated();
}

void TerminalSession::hostExit(int) {
    if (!m_alive) return;
    m_alive = false;
    emit finished();
}

void TerminalSession::hostGone() { hostExit(-2); }

void TerminalSession::writeOut(const char *s, size_t len) {
    if (m_alive) HostClient::instance()->input(m_id, QByteArray(s, qsizetype(len)));
}

void TerminalSession::resize(int rows, int cols) {
    rows = qMax(1, rows);
    cols = qMax(2, cols);
    if (rows == m_rows && cols == m_cols) return;
    m_rows = rows;
    m_cols = cols;
    vterm_set_size(m_vt, rows, cols);
    if (m_alive) HostClient::instance()->resize(m_id, rows, cols);
    vterm_screen_flush_damage(m_screen);
    emit updated();
}

void TerminalSession::cellAt(int absRow, int col, VTermScreenCell &cell) const {
    const int sb = int(m_sb.size());
    if (absRow < sb) {
        const auto &line = m_sb[size_t(absRow)];
        if (col < int(line.size())) { cell = line[size_t(col)]; return; }
        memset(&cell, 0, sizeof cell);
        cell.width = 1;
        cell.fg.type = VTERM_COLOR_DEFAULT_FG;
        cell.bg.type = VTERM_COLOR_DEFAULT_BG;
        return;
    }
    vterm_screen_get_cell(m_screen, VTermPos{absRow - sb, col}, &cell);
}

QString TerminalSession::textRange(int r0, int c0, int r1, int c1) const {
    QString out;
    const int total = int(m_sb.size()) + m_rows;
    r0 = qBound(0, r0, total - 1);
    r1 = qBound(0, r1, total - 1);
    for (int r = r0; r <= r1; ++r) {
        int a = r == r0 ? c0 : 0, b = r == r1 ? c1 : m_cols - 1;
        QString line;
        VTermScreenCell cell;
        for (int c = a; c <= b && c < m_cols; ++c) {
            cellAt(r, c, cell);
            if (cell.chars[0] == uint32_t(-1)) continue;
            if (!cell.chars[0]) { line += ' '; continue; }
            for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i) line += QString::fromUcs4(&cell.chars[i], 1);
        }
        while (line.endsWith(' ')) line.chop(1);
        out += line;
        if (r != r1) out += '\n';
    }
    return out;
}

QString TerminalSession::tailText(int maxLines) const {
    QStringList lines;
    for (int r = m_rows - 1; r >= 0 && lines.size() < maxLines; --r) {
        const QString l = textRange(int(m_sb.size()) + r, 0, int(m_sb.size()) + r, m_cols - 1);
        if (!l.trimmed().isEmpty()) lines.prepend(l);
    }
    return lines.join('\n');
}

QString TerminalSession::readText(int lines, bool scrollback) const {
    const int sb = int(m_sb.size());
    int start = scrollback ? (lines > 0 ? qMax(0, sb + m_rows - lines) : 0) : sb;
    QString t = textRange(start, 0, sb + m_rows - 1, m_cols - 1);
    while (t.endsWith('\n')) t.chop(1);
    if (!scrollback && lines > 0 && lines < m_rows) {
        QStringList l = t.split('\n');
        t = l.mid(qMax(0, int(l.size()) - lines)).join('\n');
    }
    return t;
}

void TerminalSession::injectKey(int key, int qtMods, const QString &text) {
    QKeyEvent e(QEvent::KeyPress, key, Qt::KeyboardModifiers(qtMods), text);
    keyPress(&e);
}

void TerminalSession::reportState(const QString &state, int ttlSeconds) {
    if (state.isEmpty()) { m_reported.clear(); return; }
    m_reported = state;
    m_reportTtl = ttlSeconds;
    m_reportAge.start();
    poll();
}

int TerminalSession::cbDamage(VTermRect, void *u) {
    auto *s = static_cast<TerminalSession *>(u);
    if (!s->m_repaint.isActive()) s->m_repaint.start();
    return 1;
}
int TerminalSession::cbMoveCursor(VTermPos pos, VTermPos, int visible, void *u) {
    auto *s = static_cast<TerminalSession *>(u);
    s->m_cursor = pos;
    s->m_cursorVisible = visible;
    return 1;
}
int TerminalSession::cbSetProp(VTermProp p, VTermValue *v, void *u) {
    auto *s = static_cast<TerminalSession *>(u);
    switch (p) {
    case VTERM_PROP_CURSORVISIBLE: s->m_cursorVisible = v->boolean; break;
    case VTERM_PROP_ALTSCREEN: s->m_altScreen = v->boolean; break;
    case VTERM_PROP_MOUSE: s->m_mouse = v->number; break;
    case VTERM_PROP_CURSORSHAPE: s->m_cursorShape = v->number; break;
    case VTERM_PROP_TITLE:
        if (v->string.initial) s->m_title.clear();
        s->m_title.append(v->string.str, qsizetype(v->string.len));
        break;
    default: break;
    }
    return 1;
}
int TerminalSession::cbBell(void *) { return 1; }
int TerminalSession::cbPush(int cols, const VTermScreenCell *cells, void *u) {
    auto *s = static_cast<TerminalSession *>(u);
    s->m_sb.emplace_back(cells, cells + cols);
    const size_t limit = size_t(Settings::instance() ? Settings::instance()->scrollback() : 10000);
    while (s->m_sb.size() > limit) s->m_sb.pop_front();
    return 1;
}
int TerminalSession::cbPop(int, VTermScreenCell *, void *) { return 0; }
int TerminalSession::cbSbClear(void *u) {
    static_cast<TerminalSession *>(u)->m_sb.clear();
    return 1;
}
void TerminalSession::cbOutput(const char *s, size_t len, void *u) { static_cast<TerminalSession *>(u)->writeOut(s, len); }

// ---- input ----

static VTermModifier vmods(int qtMods) {
    int m = 0;
    if (qtMods & Qt::ShiftModifier) m |= VTERM_MOD_SHIFT;
    if (qtMods & Qt::AltModifier) m |= VTERM_MOD_ALT;
    if (qtMods & Qt::ControlModifier) m |= VTERM_MOD_CTRL;
    return VTermModifier(m);
}

void TerminalSession::keyPress(QKeyEvent *e) {
    const int key = e->key();
    const Qt::KeyboardModifiers qm = e->modifiers();
    VTermModifier mod = vmods(int(qm));
    VTermKey vk = VTERM_KEY_NONE;
    switch (key) {
    case Qt::Key_Return: case Qt::Key_Enter: vk = VTERM_KEY_ENTER; break;
    case Qt::Key_Tab: vk = VTERM_KEY_TAB; break;
    case Qt::Key_Backtab: vk = VTERM_KEY_TAB; mod = VTermModifier(mod | VTERM_MOD_SHIFT); break;
    case Qt::Key_Backspace: vk = VTERM_KEY_BACKSPACE; break;
    case Qt::Key_Escape: vk = VTERM_KEY_ESCAPE; break;
    case Qt::Key_Up: vk = VTERM_KEY_UP; break;
    case Qt::Key_Down: vk = VTERM_KEY_DOWN; break;
    case Qt::Key_Left: vk = VTERM_KEY_LEFT; break;
    case Qt::Key_Right: vk = VTERM_KEY_RIGHT; break;
    case Qt::Key_Insert: vk = VTERM_KEY_INS; break;
    case Qt::Key_Delete: vk = VTERM_KEY_DEL; break;
    case Qt::Key_Home: vk = VTERM_KEY_HOME; break;
    case Qt::Key_End: vk = VTERM_KEY_END; break;
    case Qt::Key_PageUp: vk = VTERM_KEY_PAGEUP; break;
    case Qt::Key_PageDown: vk = VTERM_KEY_PAGEDOWN; break;
    default:
        if (key >= Qt::Key_F1 && key <= Qt::Key_F35) vk = VTermKey(VTERM_KEY_FUNCTION(key - Qt::Key_F1 + 1));
    }
    if (vk != VTERM_KEY_NONE) { vterm_keyboard_key(m_vt, vk, mod); return; }

    if (qm & (Qt::ControlModifier | Qt::AltModifier)) {
        if (key >= 0x20 && key <= 0x7e) {
            uint32_t c = uint32_t(key);
            if (c >= 'A' && c <= 'Z' && !(qm & Qt::ShiftModifier)) c += 32;
            int m = mod;
            if (c >= 'a' && c <= 'z') m &= ~VTERM_MOD_SHIFT;
            vterm_keyboard_unichar(m_vt, c, VTermModifier(m));
            return;
        }
    }
    const QString t = e->text();
    for (char32_t c : t.toUcs4())
        if (c >= 0x20 && c != 0x7f) vterm_keyboard_unichar(m_vt, c, (qm & Qt::AltModifier) ? VTERM_MOD_ALT : VTERM_MOD_NONE);
}

void TerminalSession::sendText(const QString &text) {
    for (char32_t c : text.toUcs4()) vterm_keyboard_unichar(m_vt, c, VTERM_MOD_NONE);
}

void TerminalSession::paste(const QString &text) {
    if (text.isEmpty()) return;
    vterm_keyboard_start_paste(m_vt);
    const QByteArray b = QString(text).replace("\r\n", "\n").replace('\n', '\r').toUtf8();
    writeOut(b.constData(), size_t(b.size()));
    vterm_keyboard_end_paste(m_vt);
}

void TerminalSession::mouseMove(int row, int col, int mods) { vterm_mouse_move(m_vt, row, col, vmods(mods)); }
void TerminalSession::mouseButton(int button, bool pressed, int mods) { vterm_mouse_button(m_vt, button, pressed, vmods(mods)); }

// ---- metadata / agent detection ----

QString TerminalSession::label() const { return m_agent.isEmpty() ? m_proc : m_agent; }

void TerminalSession::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    if (active && m_state == "done") { m_sawWorking = false; setState("idle"); }
}

void TerminalSession::setState(const QString &s) {
    if (s == m_state) return;
    const QString prev = m_state;
    m_state = s;
    if (s == "working") m_summary.clear();
    if (!m_active && !m_agent.isEmpty() && (s == "blocked" || s == "done") && prev != s) emit attention(s);
    emit metaChanged();
}

static QString readFirstLine(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readLine()).trimmed() : QString();
}

static QString gitBranch(QString dir) {
    while (!dir.isEmpty() && dir != "/") {
        const QString head = dir + "/.git/HEAD";
        if (QFileInfo::exists(head)) {
            const QString l = readFirstLine(head);
            if (l.startsWith("ref: refs/heads/")) return l.mid(16);
            return l.left(7);
        }
        dir = QFileInfo(dir).absolutePath();
        if (dir == "/") break;
    }
    return {};
}

void TerminalSession::poll() {
    if (m_alive && !m_replaying) HostClient::instance()->requestInfo(m_id);
}

QString TerminalSession::resumeCommand() const {
    static const QHash<QString, QString> resume = {
        {"claude", "claude --continue"}, {"opencode", "opencode --continue"}, {"codex", "codex resume --last"},
        {"aider", "aider --restore-chat-history"}};
    return m_agent.isEmpty() ? QString() : resume.value(m_agent, m_agent);
}

void TerminalSession::hostInfo(qint64 pid, qint64 fg) {
    if (!m_alive) return;
    m_pid = pid;
    bool changed = false;
    const QString cwd = QFileInfo(QString("/proc/%1/cwd").arg(m_pid)).symLinkTarget();
    if (!cwd.isEmpty() && cwd != m_cwd) { m_cwd = cwd; changed = true; }
    const QString branch = gitBranch(m_cwd);
    if (branch != m_branch) { m_branch = branch; changed = true; }

    QString proc, agent;
    if (fg > 0) {
        QFile f(QString("/proc/%1/cmdline").arg(fg));
        QStringList args;
        if (f.open(QIODevice::ReadOnly))
            for (const QByteArray &a : f.readAll().split('\0'))
                if (!a.isEmpty()) args << QString::fromUtf8(a);
        const QString comm = readFirstLine(QString("/proc/%1/comm").arg(fg));
        proc = comm.isEmpty() ? (args.isEmpty() ? QString() : QFileInfo(args[0]).fileName()) : comm;
        agent = AgentDetect::identify(args, comm);
    }
    if (proc != m_proc) { m_proc = proc; changed = true; }
    if (agent != m_agent) {
        m_agent = agent;
        m_sawWorking = false;
        m_notWorkingPolls = 0;
        m_reported.clear();
        m_state = agent.isEmpty() ? QString() : "idle";
        changed = true;
    }
    if (!m_agent.isEmpty()) {
        QString raw;
        if (!m_reported.isEmpty() && m_reportAge.isValid() && m_reportAge.elapsed() < qint64(m_reportTtl) * 1000) raw = m_reported;
        else raw = AgentDetect::classify({m_agent, tailText(14), title(), m_lastOutput.elapsed() < 1500, m_active});

        // hysteresis: a spinner can vanish for a frame or two; only leave "working" after two quiet polls
        if (raw == "done") setState("done");
        else if (raw == "working") { m_sawWorking = true; m_notWorkingPolls = 0; setState("working"); }
        else if (m_state == "working" && ++m_notWorkingPolls < 2 && raw != "blocked") {}
        else if (raw == "blocked") setState("blocked");
        else if (m_sawWorking && !m_active) { setState("done"); }
        else if (m_state != "done") { m_sawWorking = false; setState("idle"); }
    }
    if (changed) emit metaChanged();
}
