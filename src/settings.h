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
    Q_PROPERTY(bool closeToTray MEMBER m_closeToTray NOTIFY changed)   // closing the window keeps nebula in the tray
    Q_PROPERTY(bool explorerOpen MEMBER m_explorerOpen NOTIFY changed)   // the explorer, on the right, closed by default
    Q_PROPERTY(int explorerWidth MEMBER m_explorerWidth NOTIFY changed)
    Q_PROPERTY(bool explorerHidden MEMBER m_explorerHidden NOTIFY changed)   // show hidden files
    Q_PROPERTY(QString aiProfile MEMBER m_aiProfile NOTIFY changed)
    Q_PROPERTY(QString aiModel MEMBER m_aiModel NOTIFY changed)
    Q_PROPERTY(bool aiSummaries MEMBER m_aiSummaries NOTIFY changed)
    Q_PROPERTY(QString operatorAgent MEMBER m_operatorAgent NOTIFY changed)
    Q_PROPERTY(QString operatorModel MEMBER m_operatorModel NOTIFY changed)
    Q_PROPERTY(bool operatorApproveAll MEMBER m_operatorApproveAll NOTIFY changed)
    Q_PROPERTY(bool onboarded MEMBER m_onboarded NOTIFY changed)
    Q_PROPERTY(QString viewPlacement MEMBER m_viewPlacement NOTIFY changed)
    Q_PROPERTY(QString restorePrograms MEMBER m_restorePrograms NOTIFY changed)
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
    // After a reboot (or Kill all & quit), what a pane's program comes back as: "run" starts agents and known
    // interactive programs again (others are typed at the prompt), "type" types every command, "off" restores
    // only the shell.
    QString restorePrograms() const { return m_restorePrograms; }
    static const QStringList &restoreModes() { static const QStringList m{"run", "type", "off"}; return m; }

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
    bool m_explorerOpen = false, m_explorerHidden = false, m_closeToTray = true;
    int m_explorerWidth = 260;
    QString m_aiProfile, m_aiModel;
    bool m_aiSummaries = false;
    QString m_operatorAgent;        // ACP agent command line; empty = auto-detect
    QString m_operatorModel;        // model id passed to the agent via session/set_config_option; empty = agent default
    bool m_operatorApproveAll = true;   // yolo: the operator never asks before running its tools
    bool m_onboarded = false;       // first-run wizard finished or skipped (kept across resetAll)
    QString m_viewPlacement = "modal";   // where agent views open by default: modal | right | down | tab
    QString m_restorePrograms = "run";
    QTimer m_saveTimer;
};
