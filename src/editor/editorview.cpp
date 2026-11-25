#include "editor/editorview.h"
#include "theme.h"
#include <QClipboard>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethod>
#include <QPainter>
#include <QRegularExpression>
#include <cmath>

EditorView::EditorView(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
    setFlag(ItemAcceptsInputMethod);
    setActiveFocusOnTab(false);
    setOpaquePainting(true);
    setAntialiasing(false);
    if (s_theme) {
        connect(s_theme, &Theme::changed, this, [this] { rebuildFont(); update(); });
        rebuildFont();
    }
    // the cursor blinks while the view has the keyboard, and stays on while you type
    m_blink.setInterval(530);
    connect(&m_blink, &QTimer::timeout, this, [this] { m_blinkOn = !m_blinkOn; update(cursorRect().adjusted(-2, 0, 2, 0).toAlignedRect()); });
    m_autoScroll.setInterval(40);
    connect(&m_autoScroll, &QTimer::timeout, this, [this] { dragTo(m_dragPoint); });
}

void EditorView::componentComplete() {
    QQuickPaintedItem::componentComplete();
    if (m_focused) forceActiveFocus();
}

void EditorView::rebuildFont() {
    m_font = QFont(s_theme->fontFamily());
    m_font.setStyleHint(QFont::Monospace);
    m_font.setPointSizeF(s_theme->fontSize());
    m_font.setHintingPreference(QFont::PreferFullHinting);
    m_bold = m_font; m_bold.setBold(true);
    m_italic = m_font; m_italic.setItalic(true);
    QFontMetricsF fm(m_font);
    m_cw = qMax<qreal>(1, fm.horizontalAdvance(QLatin1Char('M')));
    m_ch = qMax<qreal>(1, std::ceil(fm.height() * 1.15));   // a little air between lines
    m_ascent = fm.ascent() + (m_ch - fm.height()) / 2;
    emit metricsChanged();
}

void EditorView::setPane(QObject *o) {
    auto *p = qobject_cast<EditorPane *>(o);
    if (p == m_pane.data()) return;
    if (m_pane) disconnect(m_pane, nullptr, this, nullptr);
    m_pane = p;
    if (m_pane) {
        connect(m_pane, &EditorPane::textChanged, this, [this] { update(); });
        connect(m_pane, &EditorPane::changed, this, [this] { update(); });
        connect(m_pane, &EditorPane::cursorChanged, this, [this] {
            m_blinkOn = true;
            if (m_blink.isActive()) m_blink.start();
            if (hasActiveFocus()) QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
            update();
        });
        connect(m_pane, &EditorPane::ensureCursorVisible, this, &EditorView::ensureCursorVisible);
    }
    emit paneChanged();
    update();
}

void EditorView::setPaneFocused(bool f) {
    if (f == m_focused) return;
    m_focused = f;
    if (f && isComponentComplete()) forceActiveFocus();
    emit paneFocusedChanged();
    update();
}

void EditorView::focusInEvent(QFocusEvent *e) {
    QQuickPaintedItem::focusInEvent(e);
    m_blinkOn = true;
    m_blink.start();
    update();
}

void EditorView::focusOutEvent(QFocusEvent *e) {
    QQuickPaintedItem::focusOutEvent(e);
    m_blink.stop();
    m_blinkOn = true;
    if (!m_preedit.isEmpty()) m_preedit.clear();
    update();
}

void EditorView::geometryChange(const QRectF &n, const QRectF &o) {
    QQuickPaintedItem::geometryChange(n, o);
    // a scroll position chosen while the pane was still being laid out (a few lines tall) must not stick
    if (m_pane && n.height() != o.height()) {
        m_pane->scrollLine = qBound(0, m_pane->scrollLine, maxScroll());
        if (m_pane->buffer().lineCount() <= visibleLines()) m_pane->scrollLine = 0;
    }
    emit metricsChanged();
}

// past the end by half a screen, so the last lines can be read in the middle of the view
int EditorView::maxScroll() const { return m_pane ? qMax(0, m_pane->buffer().lineCount() - visibleLines() / 2) : 0; }

int EditorView::visibleLines() const { return qMax(1, int(height() / m_ch)); }

qreal EditorView::gutterWidth() const {
    const int lines = m_pane ? m_pane->buffer().lineCount() : 1;
    const int digits = qMax(3, int(QString::number(lines).size()));
    return (digits + 3) * m_cw;
}

int EditorView::textColumns() const { return qMax(1, int((width() - gutterWidth() - 10) / m_cw)); }

EditorPane::Pos EditorView::posAt(const QPointF &p) const {
    if (!m_pane) return {};
    const auto &b = m_pane->buffer();
    const int line = qBound(0, m_pane->scrollLine + int(std::floor(p.y() / m_ch)), b.lineCount() - 1);
    const qreal x = p.x() - gutterWidth();
    const int visual = qMax(0, m_pane->scrollColumn + int(std::floor(x / m_cw + 0.5)));
    return {line, m_pane->columnAt(line, visual)};
}

QRectF EditorView::cursorRect() const {
    if (!m_pane) return {};
    const auto c = m_pane->cursor();
    const qreal x = gutterWidth() + (m_pane->visualColumn(c.line, c.col) - m_pane->scrollColumn) * m_cw;
    const qreal y = (c.line - m_pane->scrollLine) * m_ch;
    return QRectF(x, y, 2, m_ch);
}

void EditorView::ensureCursorVisible() {
    if (!m_pane || height() < 1) return;
    const auto c = m_pane->cursor();
    const int rows = visibleLines(), margin = qMin(3, rows / 4);
    int top = m_pane->scrollLine;
    if (c.line < top + margin) top = c.line - margin;
    if (c.line > top + rows - 1 - margin) top = c.line - rows + 1 + margin;
    m_pane->scrollLine = m_pane->buffer().lineCount() <= rows ? 0 : qBound(0, top, maxScroll());
    const int cols = textColumns(), v = m_pane->visualColumn(c.line, c.col);
    int left = m_pane->scrollColumn;
    if (v < left + 2) left = qMax(0, v - cols / 3);
    if (v > left + cols - 2) left = v - cols + cols / 3;
    m_pane->scrollColumn = qMax(0, left);
    update();
}

void EditorView::scrollBy(int lines) {
    if (!m_pane) return;
    m_pane->scrollLine = qBound(0, m_pane->scrollLine + lines, maxScroll());
    update();
}

QColor EditorView::kindColor(Highlighter::Kind k) const {
    auto c = [](const char *name) { return s_theme->property(name).value<QColor>(); };
    switch (k) {
    case Highlighter::Keyword: return c("accent");
    case Highlighter::Type: return c("blue");
    case Highlighter::String: return c("green");
    case Highlighter::Comment: return c("muted");
    case Highlighter::Number: case Highlighter::Constant: return c("yellow");
    case Highlighter::Function: return c("blue").lighter(125);
    case Highlighter::Preproc: case Highlighter::Tag: return c("red");
    case Highlighter::Heading: return c("accent");
    case Highlighter::Emphasis: return c("yellow");
    case Highlighter::Link: return c("blue");
    case Highlighter::Code: return c("green");
    default: return s_theme->fg();
    }
}

// The bracket next to the cursor and its partner, searched within a few thousand lines.
bool EditorView::findBracketMatch(EditorPane::Pos *a, EditorPane::Pos *b) const {
    const auto &buf = m_pane->buffer();
    const auto c = m_pane->cursor();
    const QString &l = buf.line(c.line);
    static const QString open = "([{", close = ")]}";
    int at = -1;
    if (c.col < l.size() && (open.contains(l[c.col]) || close.contains(l[c.col]))) at = c.col;
    else if (c.col > 0 && (open.contains(l[c.col - 1]) || close.contains(l[c.col - 1]))) at = c.col - 1;
    if (at < 0) return false;
    const QChar ch = l[at];
    const bool forward = open.contains(ch);
    const QChar mate = forward ? close[open.indexOf(ch)] : open[close.indexOf(ch)];
    int depth = 0, line = c.line, col = at;
    for (int steps = 0; steps < 200000; ++steps) {
        const QString &t = buf.line(line);
        if (col >= 0 && col < t.size()) {
            if (t[col] == ch) ++depth;
            else if (t[col] == mate && --depth == 0) { *a = {c.line, at}; *b = {line, col}; return true; }
        }
        col += forward ? 1 : -1;
        while (col < 0 || col >= buf.line(line).size()) {
            line += forward ? 1 : -1;
            if (line < 0 || line >= buf.lineCount() || qAbs(line - c.line) > 3000) return false;
            col = forward ? 0 : int(buf.line(line).size()) - 1;
            if (buf.line(line).isEmpty()) continue;
            break;
        }
    }
    return false;
}

void EditorView::paint(QPainter *p) {
    const QColor bg = s_theme->bg(), fg = s_theme->fg();
    const QColor muted = s_theme->property("muted").value<QColor>(), panel = s_theme->property("panel").value<QColor>();
    p->fillRect(QRectF(0, 0, width(), height()), bg);
    if (!m_pane) return;
    const auto &buf = m_pane->buffer();
    const qreal gw = gutterWidth();
    const int first = m_pane->scrollLine, rows = visibleLines() + 1, left = m_pane->scrollColumn, cols = textColumns() + 2;
    const auto sel = m_pane->selection();
    const bool hasSel = !sel.empty();
    const auto cur = m_pane->cursor();
    QColor selBg = s_theme->selection();
    selBg.setAlpha(m_focused ? 110 : 60);
    QColor matchBg = s_theme->property("yellow").value<QColor>();
    matchBg.setAlpha(55);

    QRegularExpression searchRe;
    if (!m_search.isEmpty()) {
        QString pat = m_searchRegex ? m_search : QRegularExpression::escape(m_search);
        if (m_searchWord) pat = "\\b(?:" + pat + ")\\b";
        searchRe = QRegularExpression(pat, m_searchCase ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
    }
    EditorPane::Pos ba, bb;
    const bool bracket = m_focused && !hasSel && findBracketMatch(&ba, &bb);

    p->setClipRect(QRectF(gw, 0, width() - gw, height()));
    for (int r = 0; r < rows; ++r) {
        const int line = first + r;
        if (line >= buf.lineCount()) break;
        const qreal y = r * m_ch;
        const QString &text = buf.line(line);
        auto xOf = [&](int col) { return gw + (m_pane->visualColumn(line, col) - left) * m_cw; };

        if (line == cur.line && !hasSel) p->fillRect(QRectF(gw, y, width() - gw, m_ch), panel);
        // selection
        if (hasSel && line >= sel.a.line && line <= sel.b.line) {
            const qreal x0 = line == sel.a.line ? xOf(sel.a.col) : gw - left * m_cw;
            qreal x1 = line == sel.b.line ? xOf(sel.b.col) : xOf(int(text.size())) + m_cw;   // the newline is selected too
            p->fillRect(QRectF(x0, y, qMax<qreal>(x1 - x0, 0), m_ch), selBg);
        }
        // find matches
        if (searchRe.isValid() && !m_search.isEmpty())
            for (auto it = searchRe.globalMatch(text); it.hasNext();) {
                const auto m = it.next();
                if (m.capturedLength() == 0) continue;
                const qreal x0 = xOf(int(m.capturedStart())), x1 = xOf(int(m.capturedEnd()));
                p->fillRect(QRectF(x0, y + 1, x1 - x0, m_ch - 2), matchBg);
            }
        if (bracket && (line == ba.line || line == bb.line)) {
            p->setPen(muted);
            p->setBrush(Qt::NoBrush);
            for (const auto &q : {ba, bb})
                if (q.line == line) p->drawRect(QRectF(xOf(q.col), y + 0.5, m_cw - 1, m_ch - 1));
        }

        // text: runs of plain ASCII in one colour are drawn together; tabs, wide and other characters one by one
        const QVector<Highlighter::Span> spans = m_pane->spans(line);
        int si = 0;
        auto kindAt = [&](int i) {
            while (si < spans.size() && spans[si].start + spans[si].length <= i) ++si;
            return si < spans.size() && spans[si].start <= i ? spans[si].kind : Highlighter::Plain;
        };
        QString run;
        int runVisual = 0;
        QColor runColor;
        const QFont *runFont = &m_font;
        auto flush = [&] {
            if (run.isEmpty()) return;
            p->setFont(*runFont);
            p->setPen(runColor);
            p->drawText(QPointF(gw + (runVisual - left) * m_cw, y + m_ascent), run);
            run.clear();
        };
        int v = 0;
        for (int i = 0; i < text.size() && v < left + cols; ++i) {
            const QChar c = text[i];
            if (c == '\t') { flush(); v = (v / m_pane->tabWidth() + 1) * m_pane->tabWidth(); continue; }
            char32_t u = c.unicode();
            int units = 1;
            if (c.isHighSurrogate() && i + 1 < text.size()) { u = QChar::surrogateToUcs4(c, text[i + 1]); units = 2; }
            const int w = EditorPane::charWidth(u);
            const Highlighter::Kind k = kindAt(i);
            const QColor color = kindColor(k);
            const QFont *font = k == Highlighter::Heading ? &m_bold : k == Highlighter::Emphasis || k == Highlighter::Comment ? &m_italic : &m_font;
            if (v + w <= left || c == ' ') { flush(); v += w; i += units - 1; continue; }
            const bool simple = u < 0x80;
            if (simple && !run.isEmpty() && color == runColor && font == runFont && runVisual + run.size() == v) {
                run += c;
            } else {
                flush();
                runVisual = v;
                runColor = color;
                runFont = font;
                run = text.mid(i, units);
                if (!simple) flush();
            }
            v += w;
            i += units - 1;
        }
        flush();
    }
    p->setClipping(false);

    // cursor (and the text an input method is composing)
    if (cur.line >= first && cur.line < first + rows) {
        const QRectF cr = cursorRect();
        if (!m_preedit.isEmpty() && m_focused) {
            const qreal w = std::ceil(QFontMetricsF(m_font).horizontalAdvance(m_preedit) / m_cw) * m_cw;
            p->fillRect(QRectF(cr.x(), cr.y(), w, m_ch), s_theme->selection().darker(250));
            p->setPen(fg);
            p->setFont(m_font);
            p->drawText(QPointF(cr.x(), cr.y() + m_ascent), m_preedit);
            p->drawLine(QPointF(cr.x(), cr.bottom() - 1.5), QPointF(cr.x() + w, cr.bottom() - 1.5));
        } else if (cr.x() >= gw - 1) {
            if (m_focused && hasActiveFocus()) { if (m_blinkOn) p->fillRect(cr, s_theme->accent()); }
            else p->fillRect(QRectF(cr.x(), cr.y(), 1, cr.height()), muted);
        }
    }

    // gutter: line numbers, the cursor's line brighter
    p->fillRect(QRectF(0, 0, gw - m_cw, height()), bg);
    p->setFont(m_font);
    for (int r = 0; r < rows; ++r) {
        const int line = first + r;
        if (line >= buf.lineCount()) break;
        p->setPen(line == cur.line ? fg : muted);
        const QString n = QString::number(line + 1);
        p->drawText(QPointF(gw - m_cw * 2 - n.size() * m_cw, r * m_ch + m_ascent), n);
    }

    // scrollbar, when the file is longer than the view
    const int total = buf.lineCount() + visibleLines() / 2;
    if (total > visibleLines()) {
        const qreal h = height(), th = qMax<qreal>(24, h * visibleLines() / total), ty = (h - th) * first / qMax(1, total - visibleLines());
        QColor c = s_theme->accent();
        c.setAlpha(m_dragScrollbar ? 200 : 90);
        p->fillRect(QRectF(width() - 5, qMin(ty, h - th), 4, th), c);
    }
}

// ---- input

void EditorView::keyPressEvent(QKeyEvent *e) {
    if (!m_pane) { e->ignore(); return; }
    if (m_pane->keyPress(e, visibleLines())) { e->accept(); ensureCursorVisible(); }
    else e->ignore();
}

void EditorView::inputMethodEvent(QInputMethodEvent *e) {
    if (!m_pane) { e->ignore(); return; }
    if (!e->commitString().isEmpty()) m_pane->insertText(e->commitString());
    m_preedit = e->preeditString();
    e->accept();
    update();
}

QVariant EditorView::inputMethodQuery(Qt::InputMethodQuery q) const {
    switch (q) {
    case Qt::ImEnabled: return true;
    case Qt::ImCursorRectangle: return cursorRect().adjusted(0, 0, m_cw, 0);
    case Qt::ImFont: return m_font;
    case Qt::ImHints: return int(Qt::ImhNoPredictiveText | Qt::ImhMultiLine);
    case Qt::ImSurroundingText: return m_pane ? m_pane->buffer().line(m_pane->cursor().line) : QString();
    case Qt::ImCursorPosition: return m_pane ? m_pane->cursor().col : 0;
    case Qt::ImAnchorPosition: return m_pane && m_pane->anchor().line == m_pane->cursor().line ? m_pane->anchor().col : (m_pane ? m_pane->cursor().col : 0);
    default: return QQuickItem::inputMethodQuery(q);
    }
}

void EditorView::mousePressEvent(QMouseEvent *e) {
    forceActiveFocus();
    emit activated();
    if (!m_pane) return;
    const QPointF pt = e->position();
    if (e->button() == Qt::MiddleButton) {
        const QString t = QGuiApplication::clipboard()->text(QClipboard::Selection);
        if (!t.isEmpty()) { m_pane->setCursor(posAt(pt), false); m_pane->paste(t); }
        return;
    }
    // the scrollbar
    if (pt.x() >= width() - 10 && m_pane->buffer().lineCount() > visibleLines()) {
        m_dragScrollbar = true;
        mouseMoveEvent(e);
        return;
    }
    if (m_clickTimer.isValid() && m_clickTimer.elapsed() < 400 && (pt - m_lastClick).manhattanLength() < 6) m_clicks = m_clicks % 3 + 1;
    else m_clicks = 1;
    m_clickTimer.restart();
    m_lastClick = pt;
    const auto pos = posAt(pt);
    m_lineSelecting = pt.x() < gutterWidth() - m_cw || m_clicks == 3;
    if (m_lineSelecting) { m_pane->selectLine(pos.line); m_selectOrigin = {pos.line, 0}; }
    else if (m_clicks == 2) { m_pane->selectWordAt(pos); m_selectOrigin = m_pane->selection().a; }
    else m_pane->setCursor(pos, e->modifiers() & Qt::ShiftModifier);
    if (m_clicks == 1 && !m_lineSelecting) m_selectOrigin = m_pane->anchor();
    m_selecting = true;
}

// the press before this already counted the second click (word selection); only swallow the event
void EditorView::mouseDoubleClickEvent(QMouseEvent *e) { e->accept(); }

void EditorView::dragTo(const QPointF &pt) {
    if (!m_pane) return;
    if (pt.y() < 0) scrollBy(-1 - int(-pt.y() / m_ch));
    else if (pt.y() > height()) scrollBy(1 + int((pt.y() - height()) / m_ch));
    const auto pos = posAt(QPointF(pt.x(), qBound<qreal>(0, pt.y(), height() - 1)));
    if (m_lineSelecting) {
        const auto &buf = m_pane->buffer();
        auto lineEnd = [&](int l) { return l + 1 < buf.lineCount() ? EditorPane::Pos{l + 1, 0} : EditorPane::Pos{l, int(buf.line(l).size())}; };
        const int o = m_selectOrigin.line;
        if (pos.line >= o) m_pane->setSelection({o, 0}, lineEnd(pos.line));
        else m_pane->setSelection(lineEnd(o), {pos.line, 0});
    } else {
        m_pane->setSelection(m_selectOrigin, pos);
    }
    update();
}

void EditorView::mouseMoveEvent(QMouseEvent *e) {
    if (!m_pane) return;
    const QPointF pt = e->position();
    if (m_dragScrollbar) {
        const int total = m_pane->buffer().lineCount() + visibleLines() / 2;
        m_pane->scrollLine = qBound(0, int(pt.y() / qMax<qreal>(1, height()) * total) - visibleLines() / 2, qMax(0, total - visibleLines()));
        update();
        return;
    }
    if (!m_selecting) return;
    m_dragPoint = pt;
    dragTo(pt);
    if (pt.y() < 0 || pt.y() > height()) { if (!m_autoScroll.isActive()) m_autoScroll.start(); }
    else m_autoScroll.stop();
}

void EditorView::mouseReleaseEvent(QMouseEvent *) {
    m_selecting = false;
    m_dragScrollbar = false;
    m_autoScroll.stop();
    update();
}

void EditorView::wheelEvent(QWheelEvent *e) {
    if (!m_pane) return;
    if (e->modifiers() & Qt::ControlModifier) { s_theme->zoom(e->angleDelta().y() > 0 ? 1 : -1); return; }
    const QPoint px = e->pixelDelta();
    const bool horizontal = (e->modifiers() & Qt::ShiftModifier) || qAbs(e->angleDelta().x()) > qAbs(e->angleDelta().y());
    if (horizontal) {
        const qreal d = px.isNull() ? (e->angleDelta().x() ? e->angleDelta().x() : e->angleDelta().y()) / 120.0 * 6 : (px.x() ? px.x() : px.y()) / m_cw;
        m_pane->scrollColumn = qMax(0, m_pane->scrollColumn - int(std::round(d)));
        update();
        return;
    }
    // touchpads send pixels; keep the remainder so slow scrolls still move
    m_wheelRemainder += px.isNull() ? -e->angleDelta().y() / 120.0 * 3 : -px.y() / m_ch;
    const int lines = int(m_wheelRemainder);
    m_wheelRemainder -= lines;
    if (lines) scrollBy(lines);
}
