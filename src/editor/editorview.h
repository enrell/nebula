#pragma once
#include "editor/editorpane.h"
#include <QElapsedTimer>
#include <QFont>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class Theme;

// Draws an EditorPane and turns mouse, wheel, keyboard and input-method events into its commands. Only the visible
// lines are laid out and coloured, so the cost of a frame does not grow with the file.
class EditorView : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject *pane READ pane WRITE setPane NOTIFY paneChanged)
    Q_PROPERTY(bool paneFocused READ paneFocused WRITE setPaneFocused NOTIFY paneFocusedChanged)
    // matches of the find bar are marked in the visible text
    Q_PROPERTY(QString searchText MEMBER m_search WRITE setSearchText NOTIFY searchChanged)
    Q_PROPERTY(bool searchCase MEMBER m_searchCase NOTIFY searchChanged)
    Q_PROPERTY(bool searchWord MEMBER m_searchWord NOTIFY searchChanged)
    Q_PROPERTY(bool searchRegex MEMBER m_searchRegex NOTIFY searchChanged)
    Q_PROPERTY(int visibleLines READ visibleLines NOTIFY metricsChanged)
public:
    explicit EditorView(QQuickItem *parent = nullptr);
    static void setTheme(Theme *t) { s_theme = t; }

    QObject *pane() const { return m_pane; }
    void setPane(QObject *p);
    bool paneFocused() const { return m_focused; }
    void setPaneFocused(bool f);
    void setSearchText(const QString &s) { if (s != m_search) { m_search = s; emit searchChanged(); update(); } }
    int visibleLines() const;
    void paint(QPainter *p) override;
    Q_INVOKABLE void ensureCursorVisible();

signals:
    void paneChanged();
    void paneFocusedChanged();
    void searchChanged();
    void metricsChanged();
    void activated();

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void inputMethodEvent(QInputMethodEvent *e) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery q) const override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void geometryChange(const QRectF &n, const QRectF &o) override;
    void componentComplete() override;

private:
    void rebuildFont();
    qreal gutterWidth() const;
    int textColumns() const;
    int maxScroll() const;
    EditorPane::Pos posAt(const QPointF &p) const;
    QRectF cursorRect() const;
    void dragTo(const QPointF &p);
    void scrollBy(int lines);
    QColor kindColor(Highlighter::Kind k) const;
    bool findBracketMatch(EditorPane::Pos *a, EditorPane::Pos *b) const;

    static inline Theme *s_theme = nullptr;
    QPointer<EditorPane> m_pane;
    QFont m_font, m_bold, m_italic;
    qreal m_cw = 8, m_ch = 16, m_ascent = 12;
    bool m_focused = false, m_selecting = false, m_lineSelecting = false, m_dragScrollbar = false, m_blinkOn = true;
    int m_clicks = 0;
    QElapsedTimer m_clickTimer;
    QPointF m_lastClick, m_dragPoint;
    EditorPane::Pos m_selectOrigin;
    QTimer m_blink, m_autoScroll;
    qreal m_wheelRemainder = 0;
    QString m_preedit, m_search;
    bool m_searchCase = false, m_searchWord = false, m_searchRegex = false;
};
