#include "settings.h"
#include "paths.h"
#include <QFile>
#include <QJsonDocument>
#include <QMetaProperty>

Settings::Settings(QObject *parent) : QObject(parent) {
    s_instance = this;
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(300);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { save(); });
    load();
    connect(this, &Settings::changed, &m_saveTimer, qOverload<>(&QTimer::start));
    connect(this, &Settings::fontChanged, &m_saveTimer, qOverload<>(&QTimer::start));
}

QString Settings::socketPath() const { return Paths::socket(); }

QString Settings::path() { return Paths::configDir() + "/settings.json"; }

void Settings::setFontFamily(const QString &f) {
    if (f == m_fontFamily) return;
    m_fontFamily = f.trimmed();
    emit fontChanged();
}

void Settings::setFontSize(double s) {
    if (s != 0) s = qBound(6.0, s, 32.0);
    if (qFuzzyCompare(s + 1, m_fontSize + 1)) return;
    m_fontSize = s;
    emit fontChanged();
}

void Settings::load() {
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it) set(it.key(), it.value().toVariant());
}

QJsonObject Settings::toJson() const {
    QJsonObject o;
    const QMetaObject *mo = metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i)
        if (mo->property(i).isWritable()) o[QString::fromLatin1(mo->property(i).name())] = QJsonValue::fromVariant(mo->property(i).read(this));
    return o;
}

void Settings::save() const {
    Paths::configDir();
    QDir().mkpath(Paths::configDir());
    QFile f(path());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
}

bool Settings::set(const QString &key, const QVariant &value) {
    const QMetaObject *mo = metaObject();
    const int idx = mo->indexOfProperty(key.toLatin1().constData());
    if (idx < mo->propertyOffset()) return false;
    QMetaProperty p = mo->property(idx);
    QVariant v = value;
    if (!v.canConvert(p.metaType()) || !v.convert(p.metaType())) return false;
    if (key == "scrollback") v = qBound(0, v.toInt(), 1000000);
    if (key == "sidebarWidth") v = qBound(140, v.toInt(), 600);
    if (key == "viewPlacement" && !viewPlacements().contains(v.toString())) return false;
    return p.write(this, v);
}

void Settings::resetAll() {
    setFontFamily({});
    setFontSize(0);
    m_scrollback = 10000;
    m_shell.clear();
    m_copyOnSelect = true;
    m_focusFollowsMouse = false;
    m_notifications = true;
    m_urlClick = true;
    m_sidebarWidth = 236;
    m_aiProfile.clear();
    m_aiModel.clear();
    m_aiSummaries = false;
    m_operatorAgent.clear();
    m_operatorModel.clear();
    m_operatorApproveAll = true;
    m_viewPlacement = "modal";
    emit changed();
}
