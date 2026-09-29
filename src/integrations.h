#pragma once
#include <QJsonObject>
#include <QObject>
#include <QVariantList>

// Installs/removes the glue that lets agent CLIs report exact state to nebula (hooks) and control it (MCP).
class Integrations : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList list READ list NOTIFY changed)
public:
    explicit Integrations(QObject *parent = nullptr);
    static Integrations *instance() { return s_instance; }

    QVariantList list() const;
    // kind: "hooks" | "mcp". Return an empty string on success, otherwise a human-readable error.
    Q_INVOKABLE QString install(const QString &agent, const QString &kind);
    Q_INVOKABLE QString remove(const QString &agent, const QString &kind);
    Q_INVOKABLE void refresh() { emit changed(); }

signals:
    void changed();

private:
    static inline Integrations *s_instance = nullptr;
};
