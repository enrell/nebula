#include "ptyprocess.h"
#include "win/conpty.h"
#include "win/winproc.h"
#include <QDir>
#include <QMap>
#include <QMetaObject>
#include <QTimer>
#include <QWinEventNotifier>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace {

// What the reader and writer threads need. It is shared with the Pty so the threads can outlive it for the moment
// the console takes to close.
struct Core {
    conpty::Session *s = nullptr;
    std::mutex m;                    // guards `owner`
    Pty *owner = nullptr;            // null once the Pty is gone: the reader then only drains
    std::mutex wm;                   // guards the input queue
    std::condition_variable wcv;
    std::deque<QByteArray> wq;
    bool wstop = false;
};

// CreateProcess takes the environment as one block, NAME=value NUL ... NUL NUL, sorted without regard to case.
// Names are case-insensitive on Windows, so a later entry replaces an earlier one that differs only by case.
std::wstring environmentBlock(const QStringList &env) {
    QMap<QString, QString> byName;   // upper-cased name -> the whole NAME=value entry
    for (const QString &e : env) {
        const int eq = e.indexOf('=', 1);   // from 1: "=C:=C:\dir" entries have an '=' in front
        if (eq <= 0) continue;
        byName.insert(e.left(eq).toUpper(), e);
    }
    if (byName.isEmpty()) return {};
    std::wstring block;
    for (const QString &e : std::as_const(byName)) {
        block += e.toStdWString();
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

} // namespace

struct Pty::Impl {
    std::shared_ptr<Core> core;
    std::thread reader, writer;
    qint64 pid = -1;
    QWinEventNotifier *exitWatch = nullptr;
    bool exited = false, eof = false, drained = false, reported = false;
    int status = 0;
};

Pty::Pty(QObject *parent) : QObject(parent), d(new Impl) {}

Pty *Pty::start(const Spec &spec, QString *error, QObject *parent) {
    QString cwd = spec.cwd;
    if (cwd.isEmpty() || !QDir(cwd).exists()) cwd = QDir::homePath();
    std::wstring why;
    conpty::Session *s = conpty::start(spec.shell.toStdWString(), QDir::toNativeSeparators(cwd).toStdWString(), environmentBlock(spec.env),
                                       spec.rows, spec.cols, &why);
    if (!s) {
        if (error) *error = QString::fromStdWString(why);
        return nullptr;
    }
    auto *p = new Pty(parent);
    auto core = std::make_shared<Core>();
    core->s = s;
    core->owner = p;
    p->d->core = core;
    p->d->pid = qint64(conpty::pid(s));

    // The console is read and written with blocking calls, so each direction has a thread. Output is handed to
    // the Qt thread as events; the owner pointer is only used while core->m is held, which ~Pty also takes.
    p->d->reader = std::thread([core] {
        char buf[65536];
        while (const size_t n = conpty::read(core->s, buf, sizeof buf)) {
            std::lock_guard<std::mutex> g(core->m);
            Pty *o = core->owner;
            if (!o) continue;            // the Pty is gone: keep draining so the console can close
            const QByteArray data(buf, int(n));
            QMetaObject::invokeMethod(o, [o, data] { o->deliver(data); }, Qt::QueuedConnection);
        }
        std::lock_guard<std::mutex> g(core->m);
        if (Pty *o = core->owner) QMetaObject::invokeMethod(o, [o] { o->outputEnded(); }, Qt::QueuedConnection);
    });
    p->d->writer = std::thread([core] {
        for (;;) {
            QByteArray chunk;
            {
                std::unique_lock<std::mutex> lock(core->wm);
                core->wcv.wait(lock, [&] { return core->wstop || !core->wq.empty(); });
                if (core->wstop) return;
                chunk = std::move(core->wq.front());
                core->wq.pop_front();
            }
            conpty::write(core->s, chunk.constData(), size_t(chunk.size()));   // fails once the console is gone: the input is dropped
        }
    });

    p->d->exitWatch = new QWinEventNotifier(conpty::process(s), p);
    connect(p->d->exitWatch, &QWinEventNotifier::activated, p, [p] { p->processEnded(); });
    return p;
}

Pty::~Pty() {
    delete d->exitWatch;
    const std::shared_ptr<Core> core = d->core;
    {
        std::lock_guard<std::mutex> g(core->m);
        core->owner = nullptr;
    }
    conpty::kill(core->s);
    {
        std::lock_guard<std::mutex> g(core->wm);
        core->wstop = true;
    }
    core->wcv.notify_all();
    // Closing the console can take a while and needs the reader to keep draining, so it happens off this thread.
    // Once the console is closed the reader sees the end of the output, and the session can be freed.
    std::thread([core, reader = std::move(d->reader), writer = std::move(d->writer)]() mutable {
        conpty::closeConsole(core->s);
        conpty::closeInput(core->s);
        if (reader.joinable()) reader.join();
        if (writer.joinable()) writer.join();
        conpty::release(core->s);
    }).detach();
    delete d;
}

qint64 Pty::pid() const { return d->pid; }
bool Pty::exited() const { return d->exited; }

void Pty::write(const QByteArray &data) {
    if (d->exited || data.isEmpty()) return;
    {
        std::lock_guard<std::mutex> g(d->core->wm);
        d->core->wq.push_back(data);
    }
    d->core->wcv.notify_one();
}

void Pty::resize(int rows, int cols) {
    if (!d->exited) conpty::resize(d->core->s, rows, cols);
}

qint64 Pty::foregroundPid() const {
    if (d->exited) return -1;
    const DWORD child = winproc::foreground(DWORD(d->pid));
    return child ? qint64(child) : d->pid;
}

void Pty::hangup() {
    if (!d->exited) conpty::kill(d->core->s);
}

void Pty::deliver(const QByteArray &data) { emit output(data); }

void Pty::outputEnded() {
    d->eof = true;
    maybeFinish();
}

void Pty::processEnded() {
    d->exitWatch->setEnabled(false);
    DWORD code = 0;
    conpty::exitCode(d->core->s, &code);
    d->status = int(code);
    d->exited = true;
    // The console can still have output on its way; wait for it to say it is done, but not for long
    QTimer::singleShot(300, this, [this] {
        d->drained = true;
        maybeFinish();
    });
    maybeFinish();
}

void Pty::maybeFinish() {
    if (!d->exited || d->reported || (!d->eof && !d->drained)) return;
    d->reported = true;
    emit finished(d->status);
}
