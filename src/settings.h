#pragma once
#include <QJsonObject>
#include <QObject>
#include <QTimer>

class Settings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    Q_PROPERTY(double fontSize READ fontSize WRITE setFontSize NOTIFY fontChanged)
    Q_PROPERTY(int scrollback MEMBER m_scrollback NOTIFY changed)
    Q_PROPERTY(QString shell MEMBER m_shell NOTIFY changed)
    Q_PROPERTY(bool copyOnSelect MEMBER m_copyOnSelect NOTIFY changed)
    Q_PROPERTY(bool focusFollowsMouse MEMBER m_focusFollowsMouse NOTIFY changed)
    Q_PROPERTY(bool notifications MEMBER m_notifications NOTIFY changed)
    Q_PROPERTY(bool urlClick MEMBER m_urlClick NOTIFY changed)
    Q_PROPERTY(int sidebarWidth MEMBER m_sidebarWidth NOTIFY changed)
    Q_PROPERTY(QString aiProfile MEMBER m_aiProfile NOTIFY changed)
    Q_PROPERTY(QString aiModel MEMBER m_aiModel NOTIFY changed)
    Q_PROPERTY(bool aiSummaries MEMBER m_aiSummaries NOTIFY changed)
    Q_PROPERTY(QString operatorAgent MEMBER m_operatorAgent NOTIFY changed)
    Q_PROPERTY(QString operatorModel MEMBER m_operatorModel NOTIFY changed)
    Q_PROPERTY(bool operatorApproveAll MEMBER m_operatorApproveAll NOTIFY changed)
    Q_PROPERTY(bool onboarded MEMBER m_onboarded NOTIFY changed)
    Q_PROPERTY(QString viewPlacement MEMBER m_viewPlacement NOTIFY changed)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)
    Q_PROPERTY(QString socketPath READ socketPath CONSTANT)
public:
    explicit Settings(QObject *parent = nullptr);
    static Settings *instance() { return s_instance; }

    QString fontFamily() const { return m_fontFamily; }
    double fontSize() const { return m_fontSize; }
    void setFontFamily(const QString &f);
    void setFontSize(double s);
    int scrollback() const { return m_scrollback; }
    QString shell() const { return m_shell; }
    bool copyOnSelect() const { return m_copyOnSelect; }
    bool notifications() const { return m_notifications; }
    QString aiProfile() const { return m_aiProfile; }
    QString aiModel() const { return m_aiModel; }
    bool aiSummaries() const { return m_aiSummaries; }
    QString operatorAgent() const { return m_operatorAgent; }
    QString operatorModel() const { return m_operatorModel; }
    bool operatorApproveAll() const { return m_operatorApproveAll; }
    QString viewPlacement() const { return m_viewPlacement; }
    static const QStringList &viewPlacements() { static const QStringList p{"modal", "right", "down", "tab"}; return p; }

    Q_INVOKABLE void resetAll();
    QJsonObject toJson() const;
    // Returns false if key is unknown or the value has the wrong type.
    bool set(const QString &key, const QVariant &value);
    static QString path();
    QString configPath() const { return path(); }
    QString socketPath() const;

signals:
    void changed();
    void fontChanged();

private:
    void load();
    void save() const;

    static inline Settings *s_instance = nullptr;
    QString m_fontFamily;        // empty = follow terminal config
    double m_fontSize = 0;       // 0 = auto (10pt)
    int m_scrollback = 10000;
    QString m_shell;             // empty = $SHELL
    bool m_copyOnSelect = true;
    bool m_focusFollowsMouse = false;
    bool m_notifications = true;
    bool m_urlClick = true;
    int m_sidebarWidth = 236;
    QString m_aiProfile, m_aiModel;
    bool m_aiSummaries = false;
    QString m_operatorAgent;        // ACP agent command line; empty = auto-detect
    QString m_operatorModel;        // model id passed to the agent via session/set_config_option; empty = agent default
    bool m_operatorApproveAll = true;   // yolo: the operator never asks before running its tools
    bool m_onboarded = false;       // first-run wizard finished or skipped (kept across resetAll)
    QString m_viewPlacement = "modal";   // where agent views open by default: modal | right | down | tab
    QTimer m_saveTimer;
};
