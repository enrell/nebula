#include "terminalview.h"
#include "theme.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QFocusEvent>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QDesktopServices>
#include <QPainter>
#include <QRegularExpression>
#include <QUrl>
#include "settings.h"

TerminalView::TerminalView(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setFlag(ItemAcceptsInputMethod);
    setFlag(ItemIsFocusScope, false);
    setActiveFocusOnTab(false);
    setOpaquePainting(true);
    setAntialiasing(false);
    if (s_theme) {
        connect(s_theme, &Theme::changed, this, [this] { rebuildFont(); syncSize(); update(); });
        rebuildFont();
    }
}

void TerminalView::componentComplete() {
    QQuickPaintedItem::componentComplete();
    syncSize();
    if (m_focused) forceActiveFocus();
}

void TerminalView::rebuildFont() {
    m_font = QFont(s_theme->fontFamily());
    m_font.setStyleHint(QFont::Monospace);
    m_font.setPointSizeF(s_theme->fontSize());
    m_font.setHintingPreference(QFont::PreferFullHinting);
    m_bold = m_font; m_bold.setBold(true);
    m_italic = m_font; m_italic.setItalic(true);
    m_boldItalic = m_bold; m_boldItalic.setItalic(true);
    QFontMetricsF fm(m_font);
    m_cw = qMax<qreal>(1, fm.horizontalAdvance(QLatin1Char('M')));
    m_ch = qMax<qreal>(1, std::ceil(fm.height()));
    m_ascent = fm.ascent();
}

void TerminalView::setSession(QObject *o) {
    auto *s = qobject_cast<TerminalSession *>(o);
    if (s == m_s.data()) return;
    if (m_s) disconnect(m_s, nullptr, this, nullptr);
    m_s = s;
    m_offset = 0;
    m_selA = m_selB = QPoint(-1, -1);
    if (m_s) {
        m_lastSb = m_s->scrollbackSize();
        connect(m_s, &TerminalSession::updated, this, [this] {
            const int sb = m_s->scrollbackSize();
            if (m_offset > 0) m_offset = qBound(0, m_offset + sb - m_lastSb, sb);
            m_lastSb = sb;
            if (hasActiveFocus()) QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
            update();
        });
        syncSize();
    }
    emit sessionChanged();
    update();
}

void TerminalView::setPaneFocused(bool f) {
    if (f == m_focused) return;
    m_focused = f;
    if (f && isComponentComplete()) forceActiveFocus();
    emit paneFocusedChanged();
    update();
}

void TerminalView::geometryChange(const QRectF &n, const QRectF &o) {
    QQuickPaintedItem::geometryChange(n, o);
    syncSize();
}

void TerminalView::syncSize() {
    if (!m_s || width() < 1 || height() < 1) return;
    m_s->resize(int(height() / m_ch), int(width() / m_cw));
}

int TerminalView::topRow() const { return m_s->scrollbackSize() - m_offset; }

QPoint TerminalView::cellAtPos(const QPointF &p) const {
    int c = qBound(0, int(p.x() / m_cw), m_s->cols() - 1);
    int r = qBound(0, int(p.y() / m_ch), m_s->rows() - 1);
    return QPoint(c, topRow() + r);
}

void TerminalView::paint(QPainter *p) {
    const QColor bg = s_theme->bg(), fgDefault = s_theme->fg();
    p->fillRect(QRectF(0, 0, width(), height()), bg);
    if (!m_s) return;

    const int rows = m_s->rows(), cols = m_s->cols(), top = topRow();
    QPoint sa = m_selA, sb = m_selB;
    if (sb.y() < sa.y() || (sb.y() == sa.y() && sb.x() < sa.x())) std::swap(sa, sb);
    const bool sel = selectionActive();

    auto resolve = [&](const VTermColor &c, bool isFg) -> QColor {
        if (VTERM_COLOR_IS_DEFAULT_FG(&c)) return fgDefault;
        if (VTERM_COLOR_IS_DEFAULT_BG(&c)) return bg;
        if (VTERM_COLOR_IS_INDEXED(&c)) return s_theme->ansi(c.indexed.idx);
        (void)isFg;
        return QColor(c.rgb.red, c.rgb.green, c.rgb.blue);
    };

    struct Run { int col; QString text; QColor fg; QFont *font; bool ul, st; int cells; };
    std::vector<Run> runs;
    QFont *fonts[4] = {&m_font, &m_bold, &m_italic, &m_boldItalic};

    VTermScreenCell cell;
    for (int r = 0; r < rows; ++r) {
        const qreal y = r * m_ch;
        const int abs = top + r;
        runs.clear();
        int bgStart = 0;
        QColor bgCur;
        auto flushBg = [&](int endCol) {
            if (endCol > bgStart && bgCur != bg) p->fillRect(QRectF(bgStart * m_cw, y, (endCol - bgStart) * m_cw, m_ch), bgCur);
        };
        for (int c = 0; c < cols; ++c) {
            m_s->cellAt(abs, c, cell);
            QColor f = resolve(cell.fg, true), b = resolve(cell.bg, false);
            if (cell.attrs.font == 1) f = QColor::fromRgbF(f.redF() * 0.5 + b.redF() * 0.5, f.greenF() * 0.5 + b.greenF() * 0.5, f.blueF() * 0.5 + b.blueF() * 0.5);   // SGR 2 (faint), see TerminalSession::translateFaint
            if (cell.attrs.reverse) std::swap(f, b);
            bool selected = false;
            if (sel) {
                const bool after = abs > sa.y() || (abs == sa.y() && c >= sa.x());
                const bool before = abs < sb.y() || (abs == sb.y() && c <= sb.x());
                selected = after && before;
            }
            if (selected) { b = s_theme->selection(); f = s_theme->bg(); }
            if (c == 0) { bgStart = 0; bgCur = b; }
            else if (b != bgCur) { flushBg(c); bgStart = c; bgCur = b; }

            const uint32_t ch = cell.chars[0];
            if (ch == uint32_t(-1) || cell.attrs.conceal) continue;
            const bool blank = ch == 0 || ch == ' ';
            if (blank && !cell.attrs.underline && !cell.attrs.strike) continue;
            QString s = blank ? QString(QLatin1Char(' ')) : QString::fromUcs4(reinterpret_cast<const char32_t *>(cell.chars), [&] {
                int n = 0; while (n < VTERM_MAX_CHARS_PER_CELL && cell.chars[n]) ++n; return n; }());
            QFont *font = fonts[(cell.attrs.bold ? 1 : 0) | (cell.attrs.italic ? 2 : 0)];
            const bool ascii = blank || ch < 0x80;
            const bool ul = cell.attrs.underline, st = cell.attrs.strike;
            if (ascii && !runs.empty()) {
                Run &l = runs.back();
                if (l.col + l.cells == c && l.fg == f && l.font == font && l.ul == ul && l.st == st && l.text.at(0).unicode() < 0x80) {
                    l.text += s; ++l.cells; continue;
                }
            }
            runs.push_back({c, s, f, font, ul, st, cell.width > 0 ? cell.width : 1});
            if (!ascii) runs.back().text = s;
        }
        flushBg(cols);
        for (const Run &run : runs) {
            const qreal x = run.col * m_cw;
            p->setFont(*run.font);
            p->setPen(run.fg);
            if (run.text.trimmed().isEmpty() && !run.ul && !run.st) continue;
            p->drawText(QPointF(x, y + m_ascent), run.text);
            if (run.ul) p->drawLine(QPointF(x, y + m_ch - 1.5), QPointF(x + run.cells * m_cw, y + m_ch - 1.5));
            if (run.st) p->drawLine(QPointF(x, y + m_ch / 2), QPointF(x + run.cells * m_cw, y + m_ch / 2));
        }
    }

    if (m_offset == 0 && m_s->cursorVisible()) {
        const VTermPos cp = m_s->cursor();
        if (cp.row >= 0 && cp.row < rows) {
            QRectF r(cp.col * m_cw, cp.row * m_ch, m_cw, m_ch);
            const QColor ac = s_theme->accent();
            if (!m_focused) { p->setPen(ac); p->setBrush(Qt::NoBrush); p->drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5)); }
            else {
                const int shape = m_s->cursorShape();
                if (shape == VTERM_PROP_CURSORSHAPE_UNDERLINE) p->fillRect(QRectF(r.x(), r.bottom() - 2, r.width(), 2), ac);
                else if (shape == VTERM_PROP_CURSORSHAPE_BAR_LEFT) p->fillRect(QRectF(r.x(), r.y(), 2, r.height()), ac);
                else {
                    p->fillRect(r, ac);
                    VTermScreenCell cc;
                    m_s->cellAt(top + cp.row, cp.col, cc);
                    if (cc.chars[0] > ' ' && cc.chars[0] != uint32_t(-1)) {
                        p->setPen(bg);
                        p->setFont(cc.attrs.bold ? m_bold : m_font);
                        p->drawText(QPointF(r.x(), r.y() + m_ascent), QString::fromUcs4(reinterpret_cast<const char32_t *>(cc.chars), 1));
                    }
                }
            }
        }
    }

    if (m_offset > 0) {
        const qreal total = m_s->scrollbackSize() + rows, h = height();
        const qreal th = qMax<qreal>(24, h * rows / total);
        const qreal ty = (h - th) * (m_s->scrollbackSize() - m_offset) / qMax(1, m_s->scrollbackSize());
        QColor c = s_theme->accent();
        c.setAlpha(m_dragScroll ? 220 : 140);
        p->fillRect(QRectF(width() - 5, ty, 4, th), c);
    }

    if (!m_preedit.isEmpty() && m_offset == 0 && m_focused) {
        const VTermPos cp = m_s->cursor();
        const qreal w = std::ceil(QFontMetricsF(m_font).horizontalAdvance(m_preedit) / m_cw) * m_cw;
        const qreal x = qMin<qreal>(cp.col * m_cw, qMax<qreal>(0, width() - w));
        p->fillRect(QRectF(x, cp.row * m_ch, w, m_ch), s_theme->selection().darker(250));
        p->setPen(s_theme->fg());
        p->setFont(m_font);
        p->drawText(QPointF(x, cp.row * m_ch + m_ascent), m_preedit);
        p->drawLine(QPointF(x, (cp.row + 1) * m_ch - 1.5), QPointF(x + w, (cp.row + 1) * m_ch - 1.5));
    }
}

void TerminalView::keyPressEvent(QKeyEvent *e) {
    if (!m_s) return;
    const auto m = e->modifiers();
    const int k = e->key();
    if ((m & Qt::ControlModifier) && (m & Qt::ShiftModifier) && k == Qt::Key_V || (m == Qt::ShiftModifier && k == Qt::Key_Insert)) {
        m_s->paste(QGuiApplication::clipboard()->text());
        m_offset = 0;
        return;
    }
    if (((m & Qt::ControlModifier) && (m & Qt::ShiftModifier) && k == Qt::Key_C) || (m == Qt::ControlModifier && k == Qt::Key_Insert)) {
        copySelection();
        return;
    }
    if (m == Qt::ShiftModifier && (k == Qt::Key_PageUp || k == Qt::Key_PageDown)) {
        int d = m_s->rows() - 1;
        m_offset = qBound(0, m_offset + (k == Qt::Key_PageUp ? d : -d), m_s->scrollbackSize());
        update();
        return;
    }
    if (k == Qt::Key_Shift || k == Qt::Key_Control || k == Qt::Key_Alt || k == Qt::Key_Meta || k == Qt::Key_AltGr) return;
    m_offset = 0;
    m_selA = m_selB = QPoint(-1, -1);
    m_s->keyPress(e);
    update();
}

void TerminalView::inputMethodEvent(QInputMethodEvent *e) {
    if (!m_s) { e->ignore(); return; }
    if (!e->commitString().isEmpty()) {
        m_offset = 0;
        m_selA = m_selB = QPoint(-1, -1);
        m_s->sendText(e->commitString());
    }
    m_preedit = e->preeditString();
    e->accept();
    update();
}

QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery q) const {
    switch (q) {
    case Qt::ImEnabled: return true;
    case Qt::ImCursorRectangle: {
        if (!m_s) return QRectF();
        const VTermPos c = m_s->cursor();
        return QRectF(c.col * m_cw, c.row * m_ch, m_cw, m_ch);
    }
    case Qt::ImFont: return m_font;
    case Qt::ImHints: return int(Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase | Qt::ImhMultiLine);
    case Qt::ImSurroundingText: return QString();
    case Qt::ImCurrentSelection: return QString();
    case Qt::ImCursorPosition:
    case Qt::ImAnchorPosition: return 0;
    default: return QQuickItem::inputMethodQuery(q);
    }
}

void TerminalView::focusOutEvent(QFocusEvent *e) {
    QQuickPaintedItem::focusOutEvent(e);
    if (!m_preedit.isEmpty()) { m_preedit.clear(); update(); }
}

QString TerminalView::selectedText() const {
    if (!m_s || !selectionActive()) return {};
    QPoint a = m_selA, b = m_selB;
    if (b.y() < a.y() || (b.y() == a.y() && b.x() < a.x())) std::swap(a, b);
    return m_s->textRange(a.y(), a.x(), b.y(), b.x());
}

void TerminalView::copySelection() {
    if (!m_s || !selectionActive()) return;
    QPoint a = m_selA, b = m_selB;
    if (b.y() < a.y() || (b.y() == a.y() && b.x() < a.x())) std::swap(a, b);
    const QString t = m_s->textRange(a.y(), a.x(), b.y(), b.x());
    QGuiApplication::clipboard()->setText(t, QClipboard::Clipboard);
    QGuiApplication::clipboard()->setText(t, QClipboard::Selection);
}

QString TerminalView::lineChars(int absRow) const {
    QString out;
    VTermScreenCell cell;
    for (int c = 0; c < m_s->cols(); ++c) {
        m_s->cellAt(absRow, c, cell);
        const uint32_t ch = cell.chars[0];
        out += (ch == 0 || ch == uint32_t(-1)) ? QChar(' ') : (ch < 0x10000 ? QChar(char16_t(ch)) : QChar('?'));
    }
    return out;
}

void TerminalView::selectWord(const QPoint &c) {
    const QString line = lineChars(c.y());
    auto isWord = [](QChar ch) { return ch.isLetterOrNumber() || QStringLiteral("_-./~:@%+=?&#\\").contains(ch); };
    int a = c.x(), b = c.x();
    if (isWord(line[a])) {
        while (a > 0 && isWord(line[a - 1])) --a;
        while (b < line.size() - 1 && isWord(line[b + 1])) ++b;
    } else if (line[a] == ' ') {
        while (a > 0 && line[a - 1] == ' ') --a;
        while (b < line.size() - 1 && line[b + 1] == ' ') ++b;
    }
    m_selA = QPoint(a, c.y());
    m_selB = QPoint(b, c.y());
}

QString TerminalView::urlAt(const QPoint &c) const {
    static const QRegularExpression rx(R"re((?:https?|ftp|file)://[^\s<>"'`]+)re");
    const QString line = lineChars(c.y());
    for (auto it = rx.globalMatch(line); it.hasNext();) {
        const auto m = it.next();
        if (c.x() < m.capturedStart() || c.x() >= m.capturedEnd()) continue;
        QString u = m.captured();
        while (!u.isEmpty() && QStringLiteral(".,;:!?)]}>").contains(u.back())) u.chop(1);
        return c.x() < m.capturedStart() + u.size() ? u : QString();
    }
    return {};
}

void TerminalView::openUrl(const QString &url) { if (!url.isEmpty()) QDesktopServices::openUrl(QUrl(url)); }

void TerminalView::pasteClipboard() {
    if (!m_s) return;
    m_offset = 0;
    m_s->paste(QGuiApplication::clipboard()->text());
}

void TerminalView::scrollTo(qreal y) {
    const int sb = m_s->scrollbackSize();
    m_offset = qBound(0, sb - int(qBound<qreal>(0, y / height(), 1) * sb + 0.5), sb);
    update();
}

void TerminalView::mousePressEvent(QMouseEvent *e) {
    forceActiveFocus();
    emit activated();
    if (!m_s) return;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    if (m_s->mouseMode() && !shift) {
        const QPoint c = cellAtPos(e->position());
        m_mouseFwd = true;
        m_s->mouseMove(c.y() - topRow(), c.x(), int(e->modifiers()));
        m_s->mouseButton(e->button() == Qt::LeftButton ? 1 : e->button() == Qt::MiddleButton ? 2 : 3, true, int(e->modifiers()));
        return;
    }
    const QPoint cell = cellAtPos(e->position());
    if (e->button() == Qt::RightButton) {
        emit contextMenuRequested(e->position().x(), e->position().y(), selectionActive(), urlAt(cell));
        return;
    }
    if (e->button() == Qt::MiddleButton) {
        m_s->paste(QGuiApplication::clipboard()->text(QClipboard::Selection));
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    if (m_offset > 0 && e->position().x() >= width() - 12) {
        m_dragScroll = true;
        scrollTo(e->position().y());
        return;
    }
    if ((e->modifiers() & Qt::ControlModifier) && Settings::instance()->property("urlClick").toBool()) {
        const QString url = urlAt(cell);
        if (!url.isEmpty()) { openUrl(url); return; }
    }
    if (m_clickTimer.isValid() && m_clickTimer.elapsed() < 400 && cell == m_lastClickCell) m_clicks = m_clicks % 3 + 1;
    else m_clicks = 1;
    m_clickTimer.restart();
    m_lastClickCell = cell;
    if (m_clicks == 2) selectWord(cell);
    else if (m_clicks == 3) { m_selA = QPoint(0, cell.y()); m_selB = QPoint(m_s->cols() - 1, cell.y()); }
    else m_selA = m_selB = cell;
    m_selecting = true;
    update();
}

void TerminalView::mouseMoveEvent(QMouseEvent *e) {
    if (!m_s) return;
    if (m_dragScroll) { scrollTo(e->position().y()); return; }
    if (m_mouseFwd) {
        const QPoint c = cellAtPos(e->position());
        m_s->mouseMove(c.y() - topRow(), c.x(), int(e->modifiers()));
    } else if (m_selecting && m_clicks == 1) {
        m_selB = cellAtPos(e->position());
        update();
    }
}

void TerminalView::mouseReleaseEvent(QMouseEvent *e) {
    if (!m_s) return;
    if (m_dragScroll) { m_dragScroll = false; update(); return; }
    if (m_mouseFwd) {
        m_mouseFwd = false;
        m_s->mouseButton(e->button() == Qt::LeftButton ? 1 : e->button() == Qt::MiddleButton ? 2 : 3, false, int(e->modifiers()));
    } else if (m_selecting) {
        m_selecting = false;
        if (m_clicks == 1) m_selB = cellAtPos(e->position());
        if (selectionActive() && Settings::instance()->copyOnSelect()) copySelection();
        update();
    }
}

void TerminalView::wheelEvent(QWheelEvent *e) {
    if (!m_s) return;
    if (e->modifiers() & Qt::ControlModifier) {
        s_theme->zoom(e->angleDelta().y() > 0 ? 1 : -1);
        return;
    }
    const int steps = qRound(e->angleDelta().y() / 120.0 * 3);
    if (!steps) return;
    if (m_s->mouseMode()) {
        const QPoint c = cellAtPos(e->position());
        m_s->mouseMove(c.y() - topRow(), c.x(), int(e->modifiers()));
        for (int i = 0; i < qAbs(steps); ++i) { m_s->mouseButton(steps > 0 ? 4 : 5, true, 0); }
    } else if (m_s->altScreen()) {
        QKeyEvent ke(QEvent::KeyPress, steps > 0 ? Qt::Key_Up : Qt::Key_Down, Qt::NoModifier);
        for (int i = 0; i < qAbs(steps); ++i) m_s->keyPress(&ke);
    } else {
        m_offset = qBound(0, m_offset + steps, m_s->scrollbackSize());
        update();
    }
}
