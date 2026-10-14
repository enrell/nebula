#include "ptyprocess.h"
#include <QFile>
#include <QSocketNotifier>
#include <QTimer>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

struct Pty::Impl {
    int fd = -1;
    pid_t pid = -1;
    bool exited = false;
    QByteArray pending;      // input the kernel has not taken yet
    QSocketNotifier *rn = nullptr, *wn = nullptr;
};

Pty::Pty(QObject *parent) : QObject(parent), d(new Impl) {}

Pty *Pty::start(const Spec &spec, QString *error, QObject *parent) {
    QByteArray sh = spec.shell.toLocal8Bit();
    if (sh.isEmpty()) sh = "/bin/sh";
    const QByteArray dir = QFile::encodeName(spec.cwd);
    std::vector<QByteArray> env;
    for (const QString &e : spec.env) env.push_back(e.toLocal8Bit());
    std::vector<char *> envp;
    for (auto &e : env) envp.push_back(e.data());
    envp.push_back(nullptr);
    char *argv[] = {sh.data(), nullptr};

    struct winsize ws = {quint16(spec.rows), quint16(spec.cols), 0, 0};
    int fd = -1;
    const pid_t pid = forkpty(&fd, nullptr, nullptr, &ws);
    if (pid == 0) {
        if (!dir.isEmpty() && chdir(dir.constData()) != 0) (void)!chdir("/");
        execve(sh.constData(), argv, envp.data());
        _exit(127);
    }
    if (pid < 0) {
        if (error) *error = QString("forkpty failed: ") + strerror(errno);
        return nullptr;
    }
    auto *p = new Pty(parent);
    p->d->fd = fd;
    p->d->pid = pid;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    p->d->rn = new QSocketNotifier(fd, QSocketNotifier::Read, p);
    connect(p->d->rn, &QSocketNotifier::activated, p, [p] { p->onReadable(); });
    p->d->wn = new QSocketNotifier(fd, QSocketNotifier::Write, p);
    p->d->wn->setEnabled(false);
    connect(p->d->wn, &QSocketNotifier::activated, p, [p] {
        const ssize_t n = ::write(p->d->fd, p->d->pending.constData(), size_t(p->d->pending.size()));
        if (n > 0) p->d->pending.remove(0, n);
        if (p->d->pending.isEmpty()) p->d->wn->setEnabled(false);
    });
    return p;
}

Pty::~Pty() {
    delete d->rn;
    delete d->wn;
    if (!d->exited) {
        ::kill(d->pid, SIGHUP);
        close(d->fd);
        const pid_t pid = d->pid;
        // reap it once it has had time to die, so it does not stay a zombie
        QTimer::singleShot(1000, [pid] { waitpid(pid, nullptr, WNOHANG); });
    } else {
        close(d->fd);
    }
    delete d;
}

qint64 Pty::pid() const { return d->pid; }
bool Pty::exited() const { return d->exited; }

void Pty::write(const QByteArray &data) {
    if (d->exited) return;
    const char *s = data.constData();
    size_t len = size_t(data.size());
    if (d->pending.isEmpty()) {
        while (len) {
            const ssize_t n = ::write(d->fd, s, len);
            if (n > 0) { s += n; len -= size_t(n); continue; }
            if (n < 0 && errno == EINTR) continue;
            break;
        }
    }
    if (len) { d->pending.append(s, qsizetype(len)); d->wn->setEnabled(true); }
}

void Pty::resize(int rows, int cols) {
    if (d->exited) return;
    struct winsize ws = {quint16(rows), quint16(cols), 0, 0};
    ioctl(d->fd, TIOCSWINSZ, &ws);
}

qint64 Pty::foregroundPid() const { return d->exited ? -1 : qint64(tcgetpgrp(d->fd)); }

void Pty::hangup() {
    if (!d->exited) ::kill(d->pid, SIGHUP);
}

void Pty::onReadable() {
    char buf[65536];
    for (;;) {
        const ssize_t n = read(d->fd, buf, sizeof buf);
        if (n > 0) {
            emit output(QByteArray(buf, int(n)));
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
        break;
    }
    d->rn->setEnabled(false);
    d->wn->setEnabled(false);
    int st = 0;
    waitpid(d->pid, &st, 0);
    d->exited = true;
    emit finished(WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st));
}
