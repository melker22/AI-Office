#pragma once

#include "core/Document.h"

#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace aioffice {

struct PreviewResult {
    QImage image;
    int page = 1;
    int pages = 1;
};

struct HitResult {
    QString id;
    int offset = 0;
    int row = -1;
    int col = -1;
    int page = 1;
    bool ok = false;
};

struct CaretRect {
    QRectF rect;
    int page = 1;
    bool ok = false;
};

struct CellGeom {
    int row = 0;
    int col = 0;
    QRectF rect;
};

struct LaidBlock {
    QString id;
    QString type;
    int page = 1;
    QRectF rect;
    qreal markerWidth = 0;
    QString marker;
    QVector<CellGeom> cells;
};

struct DocumentLayout {
    double zoom = 1.15;
    int pages = 1;
    int paperW = 0;
    int paperH = 0;
    qreal marginL = 0;
    qreal marginT = 0;
    qreal marginR = 0;
    qreal marginB = 0;
    qreal contentW = 0;
    qreal contentH = 0;
    QVector<LaidBlock> blocks;

    void relayout(const Document &doc, double zoom);
    HitResult hitTest(const Document &doc, int page, QPointF posInPage) const;
    CaretRect caret(const Document &doc, const QString &id, int offset, int row = -1, int col = -1) const;
    int pageOf(const QString &id) const;
};

void paintPage(QPainter *p, const Document &doc, const DocumentLayout &lay, int page,
               const QString &selId = {}, int selStart = -1, int selEnd = -1,
               int selRow = -1, int selCol = -1, bool caretOn = false,
               int caretOff = 0);

PreviewResult renderPage(const Document &doc, int page, double zoom);
QByteArray renderPagePng(const Document &doc, int page, double zoom);
bool exportNativePdf(const Document &doc, const QString &path, QString *error, bool pdfa = false);

} // namespace aioffice
