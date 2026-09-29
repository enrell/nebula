#include "llm.h"
#include "profiles.h"
#include "settings.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

Llm::Llm(QObject *parent) : QObject(parent) { s_instance = this; }

static QString aiProfile(const QVariantMap &opts) {
    QString p = opts.value("profile").toString();
    if (p.isEmpty() && Settings::instance()) p = Settings::instance()->aiProfile();
    return Profiles::instance()->resolve(p);
}

bool Llm::available() const { return Profiles::instance()->exists(aiProfile({})); }

int Llm::ask(const QString &kind, const QString &system, const QString &prompt, const QVariantMap &opts) {
    const int id = m_nextId++;
    auto fail = [this, id, kind](const QString &msg) {
        QTimer::singleShot(0, this, [this, id, kind, msg] { emit finished(id, kind, {}, msg); });
        return id;
    };
    Profiles *pf = Profiles::instance();
    const QString profile = aiProfile(opts);
    if (!pf->exists(profile)) return fail("no AI profile configured (Settings > Providers)");
    const QString prov = pf->provider(profile);
    QString model = opts.value("model").toString();
    if (model.isEmpty() && Settings::instance()) model = Settings::instance()->aiModel();
    if (model.isEmpty()) model = pf->defaultModel(profile);
    if (model.isEmpty()) return fail("no model set for this profile (Settings > AI features)");
    QString keyVar;
    for (const auto &p : Profiles::presets())
        if (p.id == prov) keyVar = p.keyVar;
    const QString key = pf->envValue(profile, keyVar);
    if (key.isEmpty()) return fail("profile '" + profile + "' has no API key");
    QString base = pf->baseUrl(profile);
    while (base.endsWith('/')) base.chop(1);

    QNetworkRequest req;
    QByteArray body;
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(60000);
    if (prov == "anthropic") {
        req.setUrl(QUrl(base + "/v1/messages"));
        req.setRawHeader("x-api-key", key.toUtf8());
        req.setRawHeader("anthropic-version", "2023-06-01");
        body = QJsonDocument(QJsonObject{{"model", model}, {"max_tokens", 1024}, {"system", system},
                                         {"messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", prompt}}}}}).toJson(QJsonDocument::Compact);
    } else if (prov == "gemini") {
        req.setUrl(QUrl(base + "/v1beta/models/" + model + ":generateContent"));
        req.setRawHeader("x-goog-api-key", key.toUtf8());
        body = QJsonDocument(QJsonObject{{"system_instruction", QJsonObject{{"parts", QJsonArray{QJsonObject{{"text", system}}}}}},
                                         {"contents", QJsonArray{QJsonObject{{"role", "user"}, {"parts", QJsonArray{QJsonObject{{"text", prompt}}}}}}}}).toJson(QJsonDocument::Compact);
    } else {
        if (base.isEmpty()) return fail("profile '" + profile + "' needs a base URL");
        req.setUrl(QUrl(base + "/chat/completions"));
        req.setRawHeader("Authorization", "Bearer " + key.toUtf8());
        body = QJsonDocument(QJsonObject{{"model", model},
                                         {"messages", QJsonArray{QJsonObject{{"role", "system"}, {"content", system}}, QJsonObject{{"role", "user"}, {"content", prompt}}}}}).toJson(QJsonDocument::Compact);
    }

    QNetworkReply *reply = m_net.post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, kind, prov] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const QJsonObject o = QJsonDocument::fromJson(data).object();
        if (reply->error() != QNetworkReply::NoError) {
            QString msg = o["error"].toObject()["message"].toString();
            if (msg.isEmpty()) msg = o["error"].toString();
            emit finished(id, kind, {}, msg.isEmpty() ? reply->errorString() : msg);
            return;
        }
        QString text;
        if (prov == "anthropic") {
            for (const QJsonValue &c : o["content"].toArray()) text += c.toObject()["text"].toString();
        } else if (prov == "gemini") {
            for (const QJsonValue &c : o["candidates"].toArray().first().toObject()["content"].toObject()["parts"].toArray()) text += c.toObject()["text"].toString();
        } else {
            text = o["choices"].toArray().first().toObject()["message"].toObject()["content"].toString();
        }
        if (text.isEmpty()) emit finished(id, kind, {}, "empty response");
        else emit finished(id, kind, text.trimmed(), {});
    });
    return id;
}

int Llm::summarize(const QString &screenText) {
    return ask("summary",
               "You summarize what a coding agent just did, from the end of its terminal session. Reply with ONE short line (max 100 characters), "
               "past tense, no quotes, no markdown. If it stopped to ask a question, say what it asks.",
               screenText.right(8000));
}
