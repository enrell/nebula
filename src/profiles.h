#pragma once
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariantList>

// Named credential/env profiles. Secret values live in SecretStore; only names + non-secret env are stored on disk.
class Profiles : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList list READ list NOTIFY changed)
    Q_PROPERTY(QVariantList providers READ providers CONSTANT)
    Q_PROPERTY(QString defaultName READ defaultName NOTIFY changed)
    Q_PROPERTY(QString backend READ backend CONSTANT)
public:
    explicit Profiles(QObject *parent = nullptr);
    static Profiles *instance() { return s_instance; }

    struct Provider { QString id, label, keyVar, baseVar, baseUrl, model; };
    static const QList<Provider> &presets();

    QVariantList list() const;
    QVariantList providers() const;
    QString defaultName() const { return m_default; }
    QString backend() const;
    QStringList names() const;
    bool exists(const QString &name) const { return m_profiles.contains(name); }
    QString resolve(const QString &name) const;          // "" -> default profile (may be "")

    // env entries ("K=V") to inject into a pane using this profile
    QStringList envFor(const QString &name) const;
    QString provider(const QString &name) const;
    QString envValue(const QString &name, const QString &var) const;   // secret or plain env
    QString baseUrl(const QString &name) const;
    QString defaultModel(const QString &name) const;

    Q_INVOKABLE bool save(const QString &name, const QString &provider, const QString &key, const QVariantMap &env);
    Q_INVOKABLE void remove(const QString &name);
    Q_INVOKABLE void setDefault(const QString &name);
    QJsonObject describe(const QString &name) const;

signals:
    void changed();

private:
    struct Profile { QString provider; QStringList vars; QMap<QString, QString> env; };
    void load();
    void store() const;

    static inline Profiles *s_instance = nullptr;
    QMap<QString, Profile> m_profiles;
    QString m_default;
};
