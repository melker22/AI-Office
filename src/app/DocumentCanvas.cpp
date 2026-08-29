#include "app/DocumentCanvas.h"

#include "core/Preview.h"

#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

namespace aioffice {

DocumentCanvas::DocumentCanvas(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setOpaquePainting(false);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setFlag(ItemIsFocusScope, true);
    setFlag(ItemAcceptsInputMethod, true);
    setKeepMouseGrab(true);
    blink_.setInterval(530);
    connect(&blink_, &QTimer::timeout, this, [this]() {
        caretOn_ = !caretOn_;
        update();
    });
    blink_.start();
}

void DocumentCanvas::setController(AppController *c)
{
    if (controller_ == c)
        return;
    if (controller_)
        disconnect(controller_, nullptr, this, nullptr);
    controller_ = c;
    if (controller_) {
        connect(controller_, &AppController::previewChanged, this, [this]() { update(); });
        connect(controller_, &AppController::documentChanged, this, [this]() { update(); });
    }
    emit controllerChanged();
    update();
}

void DocumentCanvas::setDark(bool v)
{
    if (dark_ == v)
        return;
    dark_ = v;
    emit darkChanged();
    update();
}

QPointF DocumentCanvas::pagePos(const QPointF &itemPos, qreal *originX, qreal *originY) const
{
    if (!controller_)
        return {};
    const auto prev = renderPage(controller_->document(), controller_->page(), controller_->zoom());
    const qreal x = (width() - prev.image.width()) / 2.0;
    const qreal y = qMax(24.0, (height() - prev.image.height()) / 2.0);
    if (originX)
        *originX = x;
    if (originY)
        *originY = y;
    return QPointF(itemPos.x() - x, itemPos.y() - y);
}

void DocumentCanvas::applyHit(const QPointF &itemPos, bool extend)
{
    if (!controller_)
        return;
    DocumentLayout lay;
    lay.relayout(controller_->document(), controller_->zoom());
    qreal ox = 0, oy = 0;
    const QPointF p = pagePos(itemPos, &ox, &oy);
    const auto hit = lay.hitTest(controller_->document(), controller_->page(), p);
    if (!hit.ok)
        return;
    controller_->placeCaret(hit.id, hit.offset, hit.row, hit.col, extend);
    forceActiveFocus();
    caretOn_ = true;
    update();
}

void DocumentCanvas::paint(QPainter *painter)
{
    painter->fillRect(boundingRect(), dark_ ? QColor(QStringLiteral("#0E0C0A"))
                                            : QColor(QStringLiteral("#E4DDD2")));
    if (!controller_)
        return;
    DocumentLayout lay;
    lay.relayout(controller_->document(), controller_->zoom());
    const int page = controller_->page();
    QImage img(qMax(1, lay.paperW), qMax(1, lay.paperH), QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(QStringLiteral("#FDFBF7")));
    QPainter ip(&img);
    ip.setRenderHint(QPainter::Antialiasing);
    ip.setRenderHint(QPainter::TextAntialiasing);
    const auto sel = controller_->session()->selection();
    const int a = qMin(sel.anchorOff, sel.focusOff);
    const int b = qMax(sel.anchorOff, sel.focusOff);
    const bool same = sel.anchorId == sel.focusId && !sel.anchorId.isEmpty();
    paintPage(&ip, controller_->document(), lay, page, same ? sel.focusId : QString(),
              same ? a : -1, same ? b : -1, sel.tableRow, sel.tableCol,
              caretOn_ && hasActiveFocus(), sel.focusOff);
    ip.end();
    const qreal x = (width() - img.width()) / 2.0;
    const qreal y = qMax(24.0, (height() - img.height()) / 2.0);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 60));
    painter->drawRoundedRect(QRectF(x + 8, y + 10, img.width(), img.height()), 0, 0);
    painter->drawImage(QPointF(x, y), img);
}

void DocumentCanvas::mousePressEvent(QMouseEvent *event)
{
    applyHit(event->position(), event->modifiers() & Qt::ShiftModifier);
    dragging_ = true;
    event->accept();
}

void DocumentCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging_)
        applyHit(event->position(), true);
    event->accept();
}

void DocumentCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    dragging_ = false;
    event->accept();
}

void DocumentCanvas::wheelEvent(QWheelEvent *event)
{
    if (!controller_)
        return;
    if (event->angleDelta().y() < 0)
        controller_->setPage(controller_->page() + 1);
    else if (event->angleDelta().y() > 0)
        controller_->setPage(controller_->page() - 1);
    event->accept();
}

void DocumentCanvas::focusInEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusInEvent(event);
    caretOn_ = true;
    update();
}

void DocumentCanvas::keyPressEvent(QKeyEvent *event)
{
    if (!controller_) {
        event->ignore();
        return;
    }
    auto *sess = controller_->session();
    Selection s = sess->selection();
    if (s.focusId.isEmpty() && !controller_->document().body.isEmpty()) {
        s.focusId = s.anchorId = controller_->document().body.first().id;
        s.focusOff = s.anchorOff = 0;
        sess->setSelection(s);
    }
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    if (event->key() == Qt::Key_PageDown) {
        controller_->setPage(controller_->page() + 1);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_PageUp) {
        controller_->setPage(controller_->page() - 1);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        sess->commitSplit();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Backspace) {
        sess->commitBackspace();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Home) {
        s.focusOff = s.anchorOff = 0;
        sess->setSelection(s);
        controller_->notifyEdited();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_End) {
        const Block *b = controller_->document().findBlock(s.focusId);
        s.focusOff = s.anchorOff = b ? b->plainText().size() : 0;
        sess->setSelection(s);
        controller_->notifyEdited();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Left) {
        s.focusOff = s.anchorOff = qMax(0, s.focusOff - 1);
        sess->setSelection(s);
        controller_->notifyEdited();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Right) {
        const Block *b = controller_->document().findBlock(s.focusId);
        const int n = b ? b->plainText().size() : 0;
        s.focusOff = s.anchorOff = qMin(n, s.focusOff + 1);
        sess->setSelection(s);
        controller_->notifyEdited();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Tab && s.focusId.startsWith(QLatin1Char('l'))) {
        sess->bumpListLevel(s.focusId, 1);
        event->accept();
        return;
    }
    if (ctrl)
        return;
    const QString t = event->text();
    if (!t.isEmpty() && t[0].isPrint()) {
        sess->commitCaretInsert(t);
        event->accept();
        return;
    }
    event->ignore();
}

void DocumentCanvas::inputMethodEvent(QInputMethodEvent *event)
{
    if (!controller_ || event->commitString().isEmpty()) {
        event->ignore();
        return;
    }
    auto *sess = controller_->session();
    sess->commitCaretInsert(event->commitString());
    event->accept();
}

QVariant DocumentCanvas::inputMethodQuery(Qt::InputMethodQuery) const
{
    return QRectF(0, 0, 1, 20);
}

} // namespace aioffice
