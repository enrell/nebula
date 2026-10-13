#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

// A program on a pseudo-terminal: forkpty on Unix (ptyprocess_posix.cpp), a ConPTY pseudo console on Windows
// (ptyprocess_win.cpp). The host daemon owns one per pane; everything above it is the same on both systems.
class Pty : public QObject {
    Q_OBJECT
public:
    struct Spec {
        QString shell;         // Unix: the path of the shell. Windows: a complete command line.
        QString cwd;
        QStringList env;       // the whole environment, NAME=value
        int rows = 24, cols = 80;
    };

    // nullptr (and `error` set) when the program cannot be started
    static Pty *start(const Spec &spec, QString *error, QObject *parent = nullptr);
    ~Pty() override;

    qint64 pid() const;
    bool exited() const;
    void write(const QByteArray &data);
    void resize(int rows, int cols);
    // What the user is talking to: the foreground process group's leader on Unix; on Windows, which has no such
    // thing, the newest child of the shell (or the shell itself). -1 when unknown.
    qint64 foregroundPid() const;
    // Ask the program to end (SIGHUP; on Windows the shell is terminated and closing the console ends the rest).
    // The destructor does this too.
    void hangup();

signals:
    void output(const QByteArray &data);
    void finished(int status);      // once, after the last output; 128 + signal when a signal ended it

private:
    explicit Pty(QObject *parent);
    struct Impl;
    Impl *d;
#ifdef Q_OS_WIN
    void deliver(const QByteArray &data);
    void outputEnded();
    void processEnded();
    void maybeFinish();
#else
    void onReadable();
#endif
};
