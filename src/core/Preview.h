#pragma once

#include "core/Document.h"

#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTextLayout>
#include <QVector>

#include <memory>

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
    int line = 0; // 0-based line in the block's QTextLayout
    bool rtl = false;
};

struct CellGeom {
    int row = 0;
    int col = 0;
    QRectF rect;
};

struct DocumentLayout;

// Inputs for blockTextLayout. paint=false reproduces measurement (page-1 fields,
// no formats, empty text becomes a space). paint=true reproduces page painting.
struct BlockTextInput {
    qreal width = 0;
    qreal zoom = 1;
    int fieldPage = 1;    // value fed to page fields (display page number when painting)
    int pages = 1;
    int physicalPage = 1; // LaidBlock::page; footnote ordinals are per physical page
    bool paint = false;
    const DocumentLayout *docLayout = nullptr;
    const QVector<TextRun> *runs = nullptr; // nullptr → block.runs (cells pass their own)
    qreal fragmentY0 = 0;
    qreal fragmentHeight = -1; // < 0: whole block, page-split skip stays 0
};

// One QTextLayout for a block, already line-broken, plus the model ↔ display
// offset map. Display text differs from plainText() because of soft hyphens,
// rendered fields, drop-cap skip, and (via pageSkip*) a block split across pages.
struct BlockTextLayout {
    std::unique_ptr<QTextLayout> layout;
    QString display;
    QFont font;
    QVector<int> displayToModel; // display cursor → model offset; -1 = synthetic (prefix)
    QVector<int> modelToDisplay; // model cursor → display cursor
    int modelLength = 0;
    int pageSkipDisplay = 0; // first display cursor visible in fragmentY0/Height
    int pageSkipModel = 0;   // model offset of that cursor (content on earlier pages)
    qreal textHeight = 0;
    QString dropLetter;
    int dropLines = 0;
    qreal dropWidth = 0;
    qreal dropHeight = 0;
    QFont dropFont;

    int toDisplay(int modelOffset) const;
    int toModel(int displayOffset) const;
};

BlockTextLayout blockTextLayout(const Document &doc, const Block &block, const BlockTextInput &in);

struct LaidBlock {
    QString id;
    QString type;
    int page = 1;
    QRectF rect;
    qreal markerWidth = 0;
    QString marker;
    QVector<CellGeom> cells;
    qreal layoutY0 = 0;
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
    QVector<int> displayOf; // displayOf[physical] = printed page number

    void relayout(const Document &doc, double zoom);
    HitResult hitTest(const Document &doc, int page, QPointF posInPage) const;
    CaretRect caret(const Document &doc, const QString &id, int offset, int row = -1, int col = -1) const;
    // Visual (left/right → QTextLayout::left/rightCursorPosition) or grapheme
    // (next/prev → next/previousCursorPosition). `move` is left|right|next|prev.
    // Writes the new model offset. False when the block has no text layout.
    bool caretMove(const Document &doc, const QString &id, int offset, const QString &move, int *outOffset) const;
    int pageOf(const QString &id) const;
    int displayPage(int physical) const;
    // Horizontal shift of painted page content, in millimetres. Even pages with
    // gutter_mirror move left by gutterMm (negative). Odd pages and a zero
    // gutter stay at 0. Paint applies it; hitTest undoes it.
    qreal pageContentOffsetMm(const Document &doc, int physicalPage) const;
};

void paintPage(QPainter *p, const Document &doc, const DocumentLayout &lay, int page,
               const QString &selId = {}, int selStart = -1, int selEnd = -1,
               int selRow = -1, int selCol = -1, bool caretOn = false,
               int caretOff = 0);

PreviewResult renderPage(const Document &doc, int page, double zoom);
QByteArray renderPagePng(const Document &doc, int page, double zoom);
// PNG of one shape, chart, QR, or image, painted with the same figure painter as the page.
QByteArray renderBlockPng(const Document &doc, const Block &block);
bool exportNativePdf(const Document &doc, const QString &path, QString *error, bool pdfa = false,
                     int fromPage = 0, int toPage = 0, bool booklet = false, int copies = 1,
                     bool comments = false, bool ua = false, QStringList *warnings = nullptr);

} // namespace aioffice
