#pragma once

#include "app/AppController.h"

#include <QQuickPaintedItem>
#include <QTimer>

namespace aioffice {

class DocumentCanvas : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(AppController *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY darkChanged)

public:
    explicit DocumentCanvas(QQuickItem *parent = nullptr);

    AppController *controller() const { return controller_; }
    void setController(AppController *c);
    bool dark() const { return dark_; }
    void setDark(bool v);

    void paint(QPainter *painter) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

signals:
    void controllerChanged();
    void darkChanged();

protected:
    void focusInEvent(QFocusEvent *event) override;

private:
    QPointF pagePos(const QPointF &itemPos, qreal *originX, qreal *originY) const;
    void applyHit(const QPointF &itemPos, bool extend);
    void refresh();

    AppController *controller_ = nullptr;
    bool dark_ = false;
    bool caretOn_ = true;
    bool dragging_ = false;
    QTimer blink_;
};

} // namespace aioffice
