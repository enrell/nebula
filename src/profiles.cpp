#include "profiles.h"
#include "paths.h"
#include "secrets.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

const QList<Profiles::Provider> &Profiles::presets() {
    static const QList<Provider> p = {
        {"anthropic", "Anthropic", "ANTHROPIC_API_KEY", "ANTHROPIC_BASE_URL", "https://api.anthropic.com", "claude-haiku-4-5"},
        {"openai", "OpenAI", "OPENAI_API_KEY", "OPENAI_BASE_URL", "https://api.openai.com/v1", "gpt-4o-mini"},
        {"openrouter", "OpenRouter", "OPENROUTER_API_KEY", "", "https://openrouter.ai/api/v1", "openai/gpt-4o-mini"},
        {"gemini", "Google Gemini", "GEMINI_API_KEY", "", "https://generativelanguage.googleapis.com", "gemini-2.0-flash"},
        {"custom", "OpenAI-compatible", "OPENAI_API_KEY", "OPENAI_BASE_URL", "", ""},
    };
    return p;
}

Profiles::Profiles(QObject *parent) : QObject(parent) {
    s_instance = this;
    load();
}

static QString path() { return Paths::configDir() + "/profiles.json"; }

void Profiles::load() {
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    m_default = o["default"].toString();
    for (const QJsonValue &v : o["profiles"].toArray()) {
        const QJsonObject p = v.toObject();
        Profile pr;
        pr.provider = p["provider"].toString();
        for (const QJsonValue &x : p["vars"].toArray()) pr.vars << x.toString();
        const QJsonObject env = p["env"].toObject();
        for (auto it = env.begin(); it != env.end(); ++it) pr.env[it.key()] = it.value().toString();
        m_profiles[p["name"].toString()] = pr;
    }
    if (!m_profiles.contains(m_default)) m_default.clear();
}

void Profiles::store() const {
    QJsonArray arr;
    for (auto it = m_profiles.begin(); it != m_profiles.end(); ++it) {
        QJsonObject env;
        for (auto e = it->env.begin(); e != it->env.end(); ++e) env[e.key()] = e.value();
        arr << QJsonObject{{"name", it.key()}, {"provider", it->provider}, {"vars", QJsonArray::fromStringList(it->vars)}, {"env", env}};
    }
    QDir().mkpath(Paths::configDir());
    QFile f(path());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(QJsonObject{{"default", m_default}, {"profiles", arr}}).toJson(QJsonDocument::Indented));
}

QString Profiles::backend() const { return SecretStore::backend(); }
QStringList Profiles::names() const { return m_profiles.keys(); }
QString Profiles::resolve(const QString &name) const { return name.isEmpty() ? m_default : name; }

QString Profiles::provider(const QString &name) const { return m_profiles.value(resolve(name)).provider; }

QVariantList Profiles::providers() const {
    QVariantList l;
    for (const Provider &p : presets()) l << QVariantMap{{"id", p.id}, {"label", p.label}, {"keyVar", p.keyVar}, {"baseUrl", p.baseUrl}, {"model", p.model}};
    return l;
}

QJsonObject Profiles::describe(const QString &name) const {
    const Profile p = m_profiles.value(name);
    QJsonArray vars;
    for (const QString &v : p.vars) vars << QJsonObject{{"name", v}, {"set", SecretStore::has(name, v)}};
    QJsonObject env;
    for (auto e = p.env.begin(); e != p.env.end(); ++e) env[e.key()] = e.value();
    return {{"name", name}, {"provider", p.provider}, {"default", name == m_default}, {"vars", vars}, {"env", env}};
}

QVariantList Profiles::list() const {
    QVariantList l;
    for (const QString &n : names()) l << describe(n).toVariantMap();
    return l;
}

QStringList Profiles::envFor(const QString &name) const {
    const QString n = resolve(name);
    QStringList out;
    if (!m_profiles.contains(n)) return out;
    const Profile &p = m_profiles[n];
    for (auto e = p.env.begin(); e != p.env.end(); ++e)
        if (e.key() != "NEBULA_BASE_URL" && e.key() != "AINEBULA_BASE_URL") out << e.key() + "=" + e.value();
    for (const QString &v : p.vars) {
        const QString val = SecretStore::get(n, v);
        if (!val.isEmpty()) out << v + "=" + val;
    }
    out << "NEBULA_PROFILE=" + n;
    out << "AINEBULA_PROFILE=" + n;   // pre-rename compat
    return out;
}

QString Profiles::envValue(const QString &name, const QString &var) const {
    const QString n = resolve(name);
    const Profile p = m_profiles.value(n);
    if (p.env.contains(var)) return p.env[var];
    return p.vars.contains(var) ? SecretStore::get(n, var) : QString();
}

QString Profiles::baseUrl(const QString &name) const {
    const QString n = resolve(name);
    const Profile p = m_profiles.value(n);
    for (const Provider &pr : presets()) {
        if (pr.id != p.provider) continue;
        if (p.env.contains("NEBULA_BASE_URL")) return p.env["NEBULA_BASE_URL"];
        if (p.env.contains("AINEBULA_BASE_URL")) return p.env["AINEBULA_BASE_URL"];   // pre-rename compat
        if (!pr.baseVar.isEmpty() && p.env.contains(pr.baseVar)) return p.env[pr.baseVar];
        return pr.baseUrl;
    }
    return {};
}

QString Profiles::defaultModel(const QString &name) const {
    const QString prov = provider(name);
    for (const Provider &pr : presets())
        if (pr.id == prov) return pr.model;
    return {};
}

bool Profiles::save(const QString &name, const QString &providerId, const QString &key, const QVariantMap &env) {
    const QString n = name.trimmed();
    if (n.isEmpty()) return false;
    const Provider *prov = nullptr;
    for (const Provider &p : presets())
        if (p.id == providerId) prov = &p;
    if (!prov) return false;
    Profile pr = m_profiles.value(n);
    pr.provider = providerId;
    if (!prov->keyVar.isEmpty()) {   // OAuth providers (empty keyVar) need no stored secret
        if (!pr.vars.contains(prov->keyVar)) pr.vars << prov->keyVar;
        if (!key.isEmpty() && !SecretStore::set(n, prov->keyVar, key)) return false;
    }
    pr.env.clear();
    for (auto it = env.begin(); it != env.end(); ++it)
        if (!it.value().toString().trimmed().isEmpty()) pr.env[it.key()] = it.value().toString().trimmed();
    if (pr.env.contains("NEBULA_BASE_URL") && !prov->baseVar.isEmpty()) pr.env[prov->baseVar] = pr.env["NEBULA_BASE_URL"];
    m_profiles[n] = pr;
    if (m_default.isEmpty()) m_default = n;
    store();
    emit changed();
    return true;
}

void Profiles::remove(const QString &name) {
    if (!m_profiles.contains(name)) return;
    for (const QString &v : m_profiles[name].vars) SecretStore::remove(name, v);
    m_profiles.remove(name);
    if (m_default == name) m_default = m_profiles.isEmpty() ? QString() : m_profiles.firstKey();
    store();
    emit changed();
}

void Profiles::setDefault(const QString &name) {
    if (!m_profiles.contains(name) && !name.isEmpty()) return;
    m_default = name;
    store();
    emit changed();
}
