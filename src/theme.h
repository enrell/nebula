#pragma once
#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

class Settings;

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor bg MEMBER m_bg NOTIFY changed)
    Q_PROPERTY(QColor fg MEMBER m_fg NOTIFY changed)
    Q_PROPERTY(QColor accent MEMBER m_accent NOTIFY changed)
    Q_PROPERTY(QColor muted MEMBER m_muted NOTIFY changed)
    Q_PROPERTY(QColor panel MEMBER m_panel NOTIFY changed)
    Q_PROPERTY(QColor border MEMBER m_border NOTIFY changed)
    Q_PROPERTY(QColor red MEMBER m_red NOTIFY changed)
    Q_PROPERTY(QColor green MEMBER m_green NOTIFY changed)
    Q_PROPERTY(QColor yellow MEMBER m_yellow NOTIFY changed)
    Q_PROPERTY(QColor blue MEMBER m_blue NOTIFY changed)
    Q_PROPERTY(QColor selection MEMBER m_selection NOTIFY changed)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY changed)
    Q_PROPERTY(QString autoFontFamily READ autoFontFamily CONSTANT)
    Q_PROPERTY(qreal fontSize READ fontSize NOTIFY changed)
public:
    explicit Theme(Settings *settings, QObject *parent = nullptr);
    QColor ansi(int i) const;
    QColor bg() const { return m_bg; }
    QColor fg() const { return m_fg; }
    QColor selection() const { return m_selection; }
    QColor accent() const { return m_accent; }
    QString fontFamily() const;
    QString autoFontFamily() const { return m_fontFamily; }
    qreal fontSize() const;
    Q_INVOKABLE void zoom(int delta);
    Q_INVOKABLE void resetZoom();
signals:
    void changed();
private:
    void load();
    void loadFont();
    void watch();
    QColor m_bg{"#101018"}, m_fg{"#e0e0e0"}, m_accent{"#b48ead"}, m_muted{"#666677"}, m_panel{"#1a1a24"},
        m_border{"#2a2a38"}, m_red{"#bf616a"}, m_green{"#a3be8c"}, m_yellow{"#ebcb8b"}, m_blue{"#81a1c1"},
        m_selection{"#b48ead"};
    QColor m_palette[256];
    QString m_fontFamily;
    Settings *m_settings;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_dir, m_file;
};
