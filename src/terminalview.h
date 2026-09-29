#pragma once
#include "terminalsession.h"
#include <QFont>
#include <QPointer>
#include <QElapsedTimer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

class Theme;

class TerminalView : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(bool paneFocused READ paneFocused WRITE setPaneFocused NOTIFY paneFocusedChanged)
public:
    explicit TerminalView(QQuickItem *parent = nullptr);
    static void setTheme(Theme *t) { s_theme = t; }

    QObject *session() const { return m_s; }
    void setSession(QObject *s);
    bool paneFocused() const { return m_focused; }
    void setPaneFocused(bool f);
    void paint(QPainter *p) override;
    Q_INVOKABLE bool hasSelection() const { return selectionActive(); }
    Q_INVOKABLE void copy() { copySelection(); }
    Q_INVOKABLE QString selectedText() const;
    Q_INVOKABLE void pasteClipboard();
    Q_INVOKABLE void openUrl(const QString &url);

signals:
    void sessionChanged();
    void paneFocusedChanged();
    void activated();
    void contextMenuRequested(qreal x, qreal y, bool hasSelection, const QString &url);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void inputMethodEvent(QInputMethodEvent *e) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery q) const override;
    void focusOutEvent(QFocusEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void geometryChange(const QRectF &n, const QRectF &o) override;
    void componentComplete() override;

private:
    void rebuildFont();
    void syncSize();
    QPoint cellAtPos(const QPointF &p) const;
    bool selectionActive() const { return m_selA != m_selB; }
    void copySelection();
    QString lineChars(int absRow) const;
    void selectWord(const QPoint &c);
    QString urlAt(const QPoint &c) const;
    void scrollTo(qreal y);
    int topRow() const;

    static inline Theme *s_theme = nullptr;
    QPointer<TerminalSession> m_s;
    QFont m_font, m_bold, m_italic, m_boldItalic;
    qreal m_cw = 8, m_ch = 16, m_ascent = 12;
    QString m_preedit;
    int m_offset = 0, m_lastSb = 0;
    bool m_focused = false, m_selecting = false, m_mouseFwd = false, m_dragScroll = false;
    QElapsedTimer m_clickTimer;
    QPoint m_lastClickCell{-1, -1};
    int m_clicks = 0;
    QPoint m_selA{-1, -1}, m_selB{-1, -1}; // x = col, y = absolute row
};
