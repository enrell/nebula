#pragma once
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantMap>

// Small provider-agnostic chat client (Anthropic, OpenAI-compatible, Gemini) used by the built-in AI features.
class Llm : public QObject {
    Q_OBJECT
public:
    explicit Llm(QObject *parent = nullptr);
    static Llm *instance() { return s_instance; }

    // opts: profile, model. Returns a request id; the result arrives through finished().
    int ask(const QString &kind, const QString &system, const QString &prompt, const QVariantMap &opts = {});

    int summarize(const QString &screenText);
    Q_INVOKABLE bool available() const;
    Q_INVOKABLE int test(const QString &profile) { return ask("test", "Reply with the single word OK.", "ping", {{"profile", profile}}); }

signals:
    void finished(int id, const QString &kind, const QString &text, const QString &error);

private:
    static inline Llm *s_instance = nullptr;
    QNetworkAccessManager m_net;
    int m_nextId = 1;
};
