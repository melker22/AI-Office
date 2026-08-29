#include "core/Preview.h"

#include <QBuffer>
#include <QFontMetricsF>
#include <QMap>
#include <QPageSize>
#include <QPainter>
#include <QPen>
#include <QPagedPaintDevice>
#include <QPdfWriter>
#include <QSet>
#include <QTextCharFormat>
#include <QTextLayout>
#include <QTextOption>

namespace aioffice {
namespace {

const StyleDef *styleNamed(const Document &doc, const QString &name)
{
    for (const auto &s : doc.styles) {
        if (s.name == name)
            return &s;
    }
    return nullptr;
}

QFont fontFor(const Document &doc, const Block &b, const TextRun &r, qreal zoom)
{
    const StyleDef *st = styleNamed(doc, b.style.isEmpty() ? QStringLiteral("Normal") : b.style);
    QString family = QStringLiteral("Liberation Serif");
    qreal size = 12;
    bool bold = false;
    bool italic = false;
    if (st) {
        family = st->font;
        size = st->sizePt;
        bold = st->bold;
        italic = st->italic;
    }
    if (b.type == QLatin1String("heading") && !st) {
        size = qMax(12.0, 24.0 - (b.level - 1) * 4);
        bold = true;
    }
    if (!r.charStyle.isEmpty()) {
        if (const StyleDef *cs = styleNamed(doc, r.charStyle)) {
            family = cs->font;
            size = cs->sizePt;
            if (cs->bold)
                bold = true;
            if (cs->italic)
                italic = true;
        }
    }
    if (!r.font.isEmpty())
        family = r.font;
    if (r.sizePt > 0)
        size = r.sizePt;
    if (r.bold)
        bold = true;
    if (r.italic)
        italic = true;
    QFont f(family);
    f.setPointSizeF(qMax(6.0, size * zoom));
    f.setBold(bold);
    f.setItalic(italic);
    f.setUnderline(r.underline);
    f.setStrikeOut(r.strike);
    if (r.smallCaps)
        f.setCapitalization(QFont::SmallCaps);
    if (r.kerning != 0)
        f.setLetterSpacing(QFont::PercentageSpacing, 100.0 + r.kerning);
    if (r.superscript || r.subscript) {
        f.setPointSizeF(qMax(6.0, f.pointSizeF() * 0.7));
        if (r.superscript)
            f.setStyleStrategy(QFont::PreferDefault);
    }
    return f;
}

QString hyphenateDisplay(QString t)
{
    QString out;
    QString word;
    auto flush = [&]() {
        if (word.size() > 10) {
            for (int i = 0; i < word.size(); ++i) {
                out += word[i];
                if (i >= 3 && i + 3 < word.size() && ((i + 1) % 5 == 0))
                    out += QChar(0x00AD);
            }
        } else {
            out += word;
        }
        word.clear();
    };
    for (const QChar ch : t) {
        if (ch.isLetter()) {
            word += ch;
        } else {
            flush();
            out += ch;
        }
    }
    flush();
    return out;
}

Qt::Alignment qtAlign(const QString &a)
{
    if (a == QLatin1String("center"))
        return Qt::AlignHCenter;
    if (a == QLatin1String("right"))
        return Qt::AlignRight;
    if (a == QLatin1String("justify"))
        return Qt::AlignJustify;
    return Qt::AlignLeft;
}

constexpr qreal kDpi = 96.0;
qreal mmToPx(qreal mm, qreal zoom) { return mm * kDpi / 25.4 * zoom; }

void applyTabs(QTextOption &opt, const Block &b, qreal zoom)
{
    if (b.tabs.isEmpty())
        return;
    QList<QTextOption::Tab> tabs;
    for (const auto &t : b.tabs) {
        QTextOption::Tab tab;
        tab.position = mmToPx(t.posMm, zoom);
        if (t.align == QLatin1String("right"))
            tab.type = QTextOption::RightTab;
        else if (t.align == QLatin1String("center"))
            tab.type = QTextOption::CenterTab;
        else if (t.align == QLatin1String("decimal")) {
            tab.type = QTextOption::DelimiterTab;
            tab.delimiter = QLatin1Char('.');
        } else {
            tab.type = QTextOption::LeftTab;
        }
        tabs.append(tab);
    }
    opt.setTabs(tabs);
}

struct DropCapInfo {
    QString letter;
    int skip = 0;
    int lines = 0;
    qreal width = 0;
    qreal height = 0;
    QFont font;
};

DropCapInfo dropCapInfo(const Document &doc, const Block &b, const QVector<TextRun> &runs, qreal zoom)
{
    DropCapInfo d;
    if (b.dropCapLines < 2 || b.type == QLatin1String("list-item"))
        return d;
    QString text;
    for (const auto &r : runs)
        text += runDisplayText(doc, r, 1, 1);
    int i = 0;
    while (i < text.size() && text.at(i).isSpace())
        ++i;
    if (i >= text.size())
        return d;
    d.letter = text.mid(i, 1);
    d.skip = i + d.letter.size();
    d.lines = b.dropCapLines;
    TextRun probe = runs.isEmpty() ? TextRun{} : runs.first();
    d.font = fontFor(doc, b, probe, zoom);
    d.font.setPointSizeF(qMax(12.0, d.font.pointSizeF() * d.lines * 0.82));
    d.font.setBold(true);
    QFontMetricsF fm(d.font);
    d.width = fm.horizontalAdvance(d.letter) + 6 * zoom;
    d.height = fm.ascent() + fm.descent();
    return d;
}

qreal layoutLines(QTextLayout &layout, qreal width, qreal dropW, int dropLines)
{
    layout.beginLayout();
    qreal y = 0;
    int lineNo = 0;
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        const bool beside = dropLines > 0 && lineNo < dropLines;
        const qreal w = beside ? qMax(qreal(20), width - dropW) : width;
        line.setLineWidth(w);
        line.setPosition(QPointF(beside ? dropW : 0, y));
        y += line.height();
        ++lineNo;
    }
    layout.endLayout();
    return y;
}

qreal measureTextHeight(const Document &doc, const Block &b, const QVector<TextRun> &runs, qreal width,
                        qreal zoom)
{
    QString text;
    for (const auto &r : runs)
        text += runDisplayText(doc, r, 1, 1);
    const DropCapInfo dc = dropCapInfo(doc, b, runs, zoom);
    if (dc.lines > 0 && dc.skip > 0 && dc.skip <= text.size())
        text = text.mid(dc.skip);
    if (doc.page.hyphenate)
        text = hyphenateDisplay(text);
    if (text.isEmpty())
        text = QLatin1Char(' ');
    TextRun probe = runs.isEmpty() ? TextRun{} : runs.first();
    QFont f = fontFor(doc, b, probe, zoom);
    QTextLayout layout(text, f);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    applyTabs(opt, b, zoom);
    layout.setTextOption(opt);
    const qreal y = layoutLines(layout, width, dc.width, dc.lines);
    return qMax(qMax(QFontMetricsF(f).height(), y), dc.height);
}

qreal measureNestedHeight(const Document &doc, const Block &b, const TableCell &cell, qreal cw, qreal zoom)
{
    if (cell.nested.isEmpty())
        return measureTextHeight(doc, b, cell.runs, qMax(qreal(12), cw - 8), zoom) + 8;
    const int cols = cell.nested.first().size();
    qreal h = 0;
    const qreal icw = qMax(qreal(12), (cw - 8) / qMax(1, cols));
    for (const auto &nrow : cell.nested) {
        qreal rh = 16 * zoom;
        for (const auto &nc : nrow)
            rh = qMax(rh, measureTextHeight(doc, b, nc.runs, icw - 4, zoom) + 4);
        h += rh;
    }
    return h + 8;
}

QString listMarker(const Block &b, int number)
{
    if (b.listKind == QLatin1String("checkbox") || b.listKind == QLatin1String("task"))
        return b.checked ? QStringLiteral("☑  ") : QStringLiteral("☐  ");
    if (b.listKind == QLatin1String("decimal"))
        return QString::number(number) + QStringLiteral(".  ");
    if (!b.listBullet.isEmpty())
        return b.listBullet + QStringLiteral("  ");
    return QStringLiteral("•  ");
}

QImage loadBlockImage(const Block &b)
{
    QImage img;
    if (!b.imageBytes.isEmpty())
        img.loadFromData(b.imageBytes);
    if (img.isNull() && !b.imagePath.isEmpty())
        img.load(b.imagePath);
    return img;
}

QVector<qreal> tableColWidths(const Block &b, qreal contentW)
{
    const int cols = b.table.isEmpty() ? 1 : b.table[0].size();
    QVector<qreal> w(cols, contentW / qMax(1, cols));
    if (b.colWidthsMm.size() == cols) {
        qreal sum = 0;
        for (double mm : b.colWidthsMm)
            sum += qMax(1.0, mm);
        if (sum > 0) {
            for (int i = 0; i < cols; ++i)
                w[i] = contentW * qMax(1.0, b.colWidthsMm[i]) / sum;
        }
    }
    return w;
}

} // namespace

void DocumentLayout::relayout(const Document &doc, double z)
{
    zoom = z > 0 ? z : 1.15;
    double wMm = 0, hMm = 0;
    doc.page.pageSizeMm(wMm, hMm);
    paperW = int(mmToPx(wMm, zoom));
    paperH = int(mmToPx(hMm, zoom));
    marginT = mmToPx(doc.page.marginsMm[0], zoom);
    marginR = mmToPx(doc.page.marginsMm[1], zoom);
    marginB = mmToPx(doc.page.marginsMm[2], zoom);
    marginL = mmToPx(doc.page.marginsMm[3] + doc.page.gutterMm, zoom);
    const qreal headerBand = (doc.header.pageNumbers || !doc.header.text.isEmpty()) ? 22 * zoom : 0;
    const qreal footerBand = (doc.footer.pageNumbers || !doc.footer.text.isEmpty()) ? 22 * zoom : 0;
    int fnCount = 0;
    int enCount = 0;
    for (const auto &b : doc.body) {
        if (b.type == QLatin1String("footnote"))
            ++fnCount;
        else if (b.type == QLatin1String("endnote"))
            ++enCount;
    }
    const qreal fnBand = fnCount > 0 ? qMin(qreal(120) * zoom, 28 * zoom * fnCount + 8) : 0;
    contentW = paperW - marginL - marginR;
    contentH = paperH - marginT - marginB - headerBand - footerBand - fnBand;
    const int colN = qBound(1, doc.page.columns, 3);
    const qreal colGap = mmToPx(doc.page.columnGapMm > 0 ? doc.page.columnGapMm : 5, zoom);
    const qreal colWidth = colN <= 1 ? contentW : (contentW - colGap * (colN - 1)) / colN;

    QVector<int> numbers;
    int decimal = 0;
    for (const auto &b : doc.body) {
        if (b.type == QLatin1String("list-item") && b.listKind == QLatin1String("decimal")) {
            if (b.listRestart)
                decimal = 0;
            ++decimal;
            numbers.push_back(decimal);
        } else {
            if (b.type != QLatin1String("list-item"))
                decimal = 0;
            numbers.push_back(0);
        }
    }

    struct Measure {
        qreal height = 0;
        qreal before = 0;
        qreal markerW = 0;
        QString marker;
        QVector<qreal> rowH;
    };
    QVector<Measure> meas;
    for (int i = 0; i < doc.body.size(); ++i) {
        const Block &b = doc.body[i];
        Measure m;
        const StyleDef *st = styleNamed(doc, b.style);
        const qreal beforePt = b.spaceBeforePt >= 0 ? b.spaceBeforePt : (st ? st->spaceBeforePt : 0);
        m.before = beforePt * zoom * kDpi / 72.0;
        if (b.type == QLatin1String("footnote") || b.type == QLatin1String("endnote")) {
            meas.push_back(m);
            continue;
        }
        if (b.type == QLatin1String("break")) {
            meas.push_back(m);
            continue;
        }
        if (b.type == QLatin1String("table")) {
            const auto tw = tableColWidths(b, colWidth);
            for (int r = 0; r < b.table.size(); ++r) {
                qreal rh = 22 * zoom;
                for (int c = 0; c < b.table[r].size(); ++c) {
                    const qreal cw = c < tw.size() ? tw[c] : colWidth;
                    rh = qMax(rh, measureNestedHeight(doc, b, b.table[r][c], cw, zoom));
                }
                m.rowH.push_back(rh);
                m.height += rh;
            }
            m.height += 8;
            meas.push_back(m);
            continue;
        }
        if (b.type == QLatin1String("image")) {
            QImage img = loadBlockImage(b);
            qreal h = mmToPx(qMin(80.0, double(b.imageWidthMm > 0 ? b.imageWidthMm : 80)), zoom);
            if (!img.isNull()) {
                const qreal w = qMin(colWidth, mmToPx(b.imageWidthMm > 0 ? b.imageWidthMm : 80, zoom));
                h = w * img.height() / qMax(1, img.width());
            }
            m.height = h + 8 + (!b.caption.isEmpty() ? 16 * zoom : 0);
            meas.push_back(m);
            continue;
        }
        if (b.type == QLatin1String("list-item")) {
            m.marker = listMarker(b, numbers[i] > 0 ? numbers[i] : 1);
            TextRun probe = b.runs.isEmpty() ? TextRun{} : b.runs.first();
            QFont f = fontFor(doc, b, probe, zoom);
            m.markerW = QFontMetricsF(f).horizontalAdvance(m.marker);
        }
        const qreal indent = mmToPx(b.indentLeftMm + b.indentFirstMm, zoom);
        const qreal textW = colWidth - m.markerW - indent - mmToPx(b.indentRightMm, zoom);
        m.height = measureTextHeight(doc, b, b.runs, qMax(qreal(20), textW), zoom);
        const qreal afterPt = b.spaceAfterPt >= 0 ? b.spaceAfterPt : (st ? st->spaceAfterPt : 8);
        m.height += afterPt * zoom * kDpi / 72.0;
        if (b.lineHeight > 0 && b.lineHeight != 1.0)
            m.height *= b.lineHeight;
        meas.push_back(m);
    }

    blocks.clear();
    displayOf.clear();
    displayOf.resize(2);
    displayOf[1] = qMax(1, doc.page.numberStart);
    QMap<int, QVector<int>> fnByPage;
    int page = 1;
    int display = displayOf[1];
    int col = 0;
    qreal y = 0;
    auto colX = [&](int c) { return marginL + c * (colWidth + colGap); };
    auto advancePage = [&](bool restart, int restartTo) {
        ++page;
        if (restart)
            display = restartTo > 0 ? restartTo : qMax(1, doc.page.numberStart);
        else
            ++display;
        if (displayOf.size() <= page)
            displayOf.resize(page + 1);
        displayOf[page] = display;
    };
    for (int i = 0; i < doc.body.size(); ++i) {
        const Block &b = doc.body[i];
        if (b.type == QLatin1String("footnote") || b.type == QLatin1String("endnote"))
            continue;
        if (b.type == QLatin1String("break") && i > 0) {
            if (b.breakKind == QLatin1String("column")) {
                ++col;
                y = 0;
                if (col >= colN) {
                    col = 0;
                    advancePage(false, 0);
                }
            } else {
                const bool restart = b.pageRestart || b.breakKind == QLatin1String("page_restart");
                advancePage(restart, b.pageNumber);
                col = 0;
                y = 0;
            }
            LaidBlock lb;
            lb.id = b.id;
            lb.type = b.type;
            lb.page = page;
            blocks.push_back(lb);
            continue;
        }
        qreal need = meas[i].before + qMax(qreal(12), meas[i].height);
        if (b.keepWithNext && i + 1 < meas.size())
            need += meas[i + 1].before + meas[i + 1].height;
        if (y > 0 && y + need > contentH) {
            ++col;
            y = 0;
            if (col >= colN) {
                col = 0;
                advancePage(false, 0);
            }
        }
        y += meas[i].before;
        LaidBlock lb;
        lb.id = b.id;
        lb.type = b.type;
        lb.page = page;
        lb.marker = meas[i].marker;
        lb.markerWidth = meas[i].markerW;
        const qreal indent = mmToPx(b.indentLeftMm, zoom);
        lb.rect = QRectF(colX(col) + indent, marginT + headerBand + y, colWidth - indent, meas[i].height);
        if (b.type == QLatin1String("table")) {
            const auto tw = tableColWidths(b, colWidth);
            qreal ty = 0;
            for (int r = 0; r < b.table.size(); ++r) {
                qreal x = lb.rect.left();
                for (int c = 0; c < b.table[r].size(); ++c) {
                    const qreal cw = c < tw.size() ? tw[c] : colWidth;
                    CellGeom g;
                    g.row = r;
                    g.col = c;
                    g.rect = QRectF(x, lb.rect.top() + ty, cw, meas[i].rowH.value(r, 22 * zoom));
                    lb.cells.push_back(g);
                    x += cw;
                }
                ty += meas[i].rowH.value(r, 22 * zoom);
            }
        }
        blocks.push_back(lb);
        for (const auto &r : b.runs) {
            if (r.noteId.isEmpty())
                continue;
            const int ni = doc.indexOf(r.noteId);
            if (ni >= 0 && doc.body[ni].type == QLatin1String("footnote"))
                fnByPage[page].push_back(ni);
        }
        y += meas[i].height;
    }
    for (auto it = fnByPage.begin(); it != fnByPage.end(); ++it) {
        qreal fnY = 0;
        for (int ni : it.value()) {
            const Block &b = doc.body[ni];
            const qreal h = qMax(qreal(14 * zoom), measureTextHeight(doc, b, b.runs, contentW, zoom));
            LaidBlock lb;
            lb.id = b.id;
            lb.type = b.type;
            lb.page = it.key();
            lb.rect = QRectF(marginL, paperH - marginB - footerBand - fnBand + fnY, contentW, h);
            blocks.push_back(lb);
            fnY += h;
        }
    }
    if (enCount > 0) {
        advancePage(false, 0);
        qreal ey = 0;
        for (int i = 0; i < doc.body.size(); ++i) {
            const Block &b = doc.body[i];
            if (b.type != QLatin1String("endnote"))
                continue;
            const qreal h = qMax(qreal(16 * zoom), measureTextHeight(doc, b, b.runs, contentW, zoom) + 8);
            LaidBlock lb;
            lb.id = b.id;
            lb.type = b.type;
            lb.page = page;
            lb.rect = QRectF(marginL, marginT + headerBand + ey, contentW, h);
            blocks.push_back(lb);
            ey += h;
        }
    }
    pages = qMax(1, page);
}

int DocumentLayout::pageOf(const QString &id) const
{
    for (const auto &b : blocks) {
        if (b.id == id)
            return b.page;
    }
    return 1;
}

int DocumentLayout::displayPage(int physical) const
{
    if (physical >= 1 && physical < displayOf.size())
        return qMax(1, displayOf[physical]);
    return qMax(1, physical);
}

HitResult DocumentLayout::hitTest(const Document &doc, int page, QPointF posInPage) const
{
    HitResult h;
    h.page = page;
    for (const auto &lb : blocks) {
        if (lb.page != page)
            continue;
        if (lb.type == QLatin1String("table")) {
            for (const auto &c : lb.cells) {
                if (c.rect.contains(posInPage)) {
                    h.ok = true;
                    h.id = lb.id;
                    h.row = c.row;
                    h.col = c.col;
                    const Block *b = doc.findBlock(lb.id);
                    if (b && h.row < b->table.size() && h.col < b->table[h.row].size()) {
                        QString t;
                        for (const auto &r : b->table[h.row][h.col].runs)
                            t += r.text;
                        h.offset = qBound(0, int(t.size() * (posInPage.x() - c.rect.left()) / qMax(1.0, c.rect.width())),
                                          t.size());
                    }
                    return h;
                }
            }
        }
        if (lb.rect.contains(posInPage) || (posInPage.y() >= lb.rect.top() && posInPage.y() <= lb.rect.bottom())) {
            h.ok = true;
            h.id = lb.id;
            const Block *b = doc.findBlock(lb.id);
            const QString t = b ? b->plainText() : QString();
            const qreal x = posInPage.x() - lb.rect.left() - lb.markerWidth;
            if (!t.isEmpty() && lb.rect.width() > 0)
                h.offset = qBound(0, int(t.size() * x / qMax(1.0, lb.rect.width() - lb.markerWidth)), t.size());
            return h;
        }
    }
    if (!doc.body.isEmpty()) {
        h.ok = true;
        h.id = doc.body.last().id;
        h.offset = doc.body.last().plainText().size();
    }
    return h;
}

CaretRect DocumentLayout::caret(const Document &doc, const QString &id, int offset, int row, int col) const
{
    CaretRect c;
    for (const auto &lb : blocks) {
        if (lb.id != id)
            continue;
        c.page = lb.page;
        c.ok = true;
        if (lb.type == QLatin1String("table") && row >= 0) {
            for (const auto &g : lb.cells) {
                if (g.row == row && g.col == col) {
                    const Block *b = doc.findBlock(id);
                    QString t;
                    if (b && row < b->table.size() && col < b->table[row].size())
                        for (const auto &r : b->table[row][col].runs)
                            t += r.text;
                    const qreal frac = t.isEmpty() ? 0 : qreal(offset) / t.size();
                    const qreal x = g.rect.left() + 4 + frac * (g.rect.width() - 8);
                    c.rect = QRectF(x, g.rect.top() + 2, 1.5 * zoom, g.rect.height() - 4);
                    return c;
                }
            }
        }
        const Block *b = doc.findBlock(id);
        const QString t = b ? b->plainText() : QString();
        const qreal frac = t.isEmpty() ? 0 : qreal(qBound(0, offset, t.size())) / qMax(1, t.size());
        const qreal x = lb.rect.left() + lb.markerWidth + frac * (lb.rect.width() - lb.markerWidth);
        c.rect = QRectF(x, lb.rect.top(), 1.5 * zoom, qMin(lb.rect.height(), 22.0 * zoom));
        return c;
    }
    return c;
}

void paintPage(QPainter *p, const Document &doc, const DocumentLayout &lay, int page,
               const QString &selId, int selStart, int selEnd, int selRow, int selCol, bool caretOn,
               int caretOff)
{
    p->fillRect(0, 0, lay.paperW, lay.paperH,
                QColor(doc.page.background.isEmpty() ? QStringLiteral("#FDFBF7") : doc.page.background));
    p->fillRect(0, 0, lay.paperW, 3, QColor(QStringLiteral("#B5651D")));
    if (doc.page.borderPt > 0) {
        QPen bp(QColor(doc.page.borderColor.isEmpty() ? QStringLiteral("#B5651D") : doc.page.borderColor),
                qMax(qreal(0.5), doc.page.borderPt * lay.zoom));
        p->setPen(bp);
        p->setBrush(Qt::NoBrush);
        p->drawRect(QRectF(lay.marginL - 4, lay.marginT - 4,
                           lay.contentW + 8, lay.paperH - lay.marginT - lay.marginB + 8));
    }

    const int shown = lay.displayPage(page);
    const bool hasHeader = doc.header.pageNumbers || !doc.header.text.isEmpty();
    const bool hasFooter = doc.footer.pageNumbers || !doc.footer.text.isEmpty();
    auto pageLabel = [&]() {
        QString t = doc.header.pageNumbers || doc.footer.pageNumbers
            ? QStringLiteral("Página %1 de %2")
                  .arg(formatPageNumber(shown, doc.page.numberFormat))
                  .arg(lay.pages)
            : QString();
        return t;
    };
    p->setPen(QColor(QStringLiteral("#6B6258")));
    QFont hf(QStringLiteral("IBM Plex Sans"));
    hf.setPointSizeF(9 * lay.zoom);
    p->setFont(hf);
    if (hasHeader) {
        QString t = doc.header.text;
        if (doc.header.pageNumbers)
            t = t.isEmpty() ? pageLabel() : t + QStringLiteral("  ·  ") + pageLabel();
        p->drawText(QRectF(lay.marginL, 8 * lay.zoom, lay.contentW, 18 * lay.zoom), Qt::AlignLeft | Qt::AlignVCenter, t);
    }
    if (hasFooter) {
        QString t = doc.footer.text;
        if (doc.footer.pageNumbers)
            t = t.isEmpty() ? pageLabel() : t + QStringLiteral("  ·  ") + pageLabel();
        p->drawText(QRectF(lay.marginL, lay.paperH - lay.marginB + 2, lay.contentW, 18 * lay.zoom),
                    Qt::AlignLeft | Qt::AlignVCenter, t);
    }

    if (doc.page.columnSeparator && doc.page.columns > 1) {
        const int colN = qBound(1, doc.page.columns, 3);
        const qreal colGap = mmToPx(doc.page.columnGapMm > 0 ? doc.page.columnGapMm : 5, lay.zoom);
        const qreal colWidth = (lay.contentW - colGap * (colN - 1)) / colN;
        p->setPen(QPen(QColor(QStringLiteral("#C4B8A8")), 1));
        for (int c = 1; c < colN; ++c) {
            const qreal x = lay.marginL + c * (colWidth + colGap) - colGap / 2;
            p->drawLine(QPointF(x, lay.marginT), QPointF(x, lay.paperH - lay.marginB));
        }
    }

    for (const auto &lb : lay.blocks) {
        if (lb.page != page)
            continue;
        const Block *bp = doc.findBlock(lb.id);
        if (!bp || bp->type == QLatin1String("break"))
            continue;
        const Block &b = *bp;
        if (b.type == QLatin1String("table")) {
            for (const auto &g : lb.cells) {
                const bool head = b.headerRow && g.row == 0;
                p->setPen(QPen(QColor(QStringLiteral("#D2C8B8")), 1));
                p->setBrush(head ? QColor(QStringLiteral("#FBF0E6")) : QColor(QStringLiteral("#FDFBF7")));
                if (selId == lb.id && selRow == g.row && selCol == g.col)
                    p->setBrush(QColor(QStringLiteral("#F3E0CC")));
                p->drawRect(g.rect);
                if (g.row < b.table.size() && g.col < b.table[g.row].size()
                    && !b.table[g.row][g.col].nested.isEmpty()) {
                    const auto &cell = b.table[g.row][g.col];
                    const int nc = cell.nested.first().size();
                    const qreal icw = g.rect.width() / qMax(1, nc);
                    qreal iy = g.rect.top();
                    p->setPen(QPen(QColor(QStringLiteral("#D2C8B8")), 1));
                    for (const auto &nrow : cell.nested) {
                        qreal rh = 16 * lay.zoom;
                        for (const auto &ncell : nrow) {
                            QString t;
                            for (const auto &r : ncell.runs)
                                t += r.text;
                            rh = qMax(rh, QFontMetricsF(QFont(QStringLiteral("Liberation Serif"), int(10 * lay.zoom))).boundingRect(QRect(0, 0, int(icw - 6), 400),
                                                                 Qt::TextWordWrap, t).height()
                                    + 6);
                        }
                        qreal ix = g.rect.left();
                        for (const auto &ncell : nrow) {
                            QRectF nr(ix, iy, icw, rh);
                            p->setBrush(QColor(QStringLiteral("#FDFBF7")));
                            p->setPen(QPen(QColor(QStringLiteral("#D2C8B8")), 1));
                            p->drawRect(nr);
                            QString t;
                            for (const auto &r : ncell.runs)
                                t += r.text;
                            p->setPen(QColor(QStringLiteral("#1A1F24")));
                            QFont tf(QStringLiteral("Liberation Serif"));
                            tf.setPointSizeF(9 * lay.zoom);
                            p->setFont(tf);
                            p->drawText(nr.adjusted(3, 1, -3, -1), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap, t);
                            ix += icw;
                        }
                        iy += rh;
                    }
                } else {
                    QString t;
                    if (g.row < b.table.size() && g.col < b.table[g.row].size())
                        for (const auto &r : b.table[g.row][g.col].runs)
                            t += r.text;
                    p->setPen(QColor(QStringLiteral("#1A1F24")));
                    QFont tf(QStringLiteral("Liberation Serif"));
                    tf.setPointSizeF(10 * lay.zoom);
                    p->setFont(tf);
                    p->drawText(g.rect.adjusted(4, 2, -4, -2), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap, t);
                }
            }
            continue;
        }
        if (b.type == QLatin1String("image")) {
            QImage img = loadBlockImage(b);
            QRectF box = lb.rect.adjusted(0, 0, 0, -8);
            if (!img.isNull()) {
                QImage sc = img.scaled(box.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                p->drawImage(box.topLeft(), sc);
            } else {
                p->setPen(QPen(QColor(QStringLiteral("#C9C2B6")), 1, Qt::DashLine));
                p->setBrush(QColor(QStringLiteral("#F3EDE3")));
                p->drawRect(box);
                p->setPen(QColor(QStringLiteral("#6B6258")));
                p->drawText(box, Qt::AlignCenter, b.imagePath.isEmpty() ? QStringLiteral("imagem") : b.imagePath);
            }
            continue;
        }
        if (b.borderPt > 0) {
            QPen bp(QColor(b.borderColor.isEmpty() ? QStringLiteral("#B5651D") : b.borderColor),
                    qMax(qreal(0.5), b.borderPt * lay.zoom));
            p->setPen(bp);
            p->setBrush(Qt::NoBrush);
            p->drawRect(lb.rect.adjusted(-2, -1, 2, 1));
        }
        QString raw;
        for (const auto &r : b.runs)
            raw += runDisplayText(doc, r, shown, lay.pages);
        const DropCapInfo dc = dropCapInfo(doc, b, b.runs, lay.zoom);
        const int skip = dc.lines > 0 ? dc.skip : 0;
        QString text = raw.mid(skip);
        if (doc.page.hyphenate)
            text = hyphenateDisplay(text);
        TextRun probe = b.runs.isEmpty() ? TextRun{} : b.runs.first();
        QFont f = fontFor(doc, b, probe, lay.zoom);
        p->setFont(f);
        QColor col(QStringLiteral("#1A1F24"));
        if (const StyleDef *st = styleNamed(doc, b.style); st && !st->color.isEmpty())
            col = QColor(st->color);
        if (!probe.color.isEmpty())
            col = QColor(probe.color);
        if (!lb.marker.isEmpty()) {
            p->setPen(col);
            p->drawText(QPointF(lb.rect.left(), lb.rect.top() + QFontMetricsF(f).ascent()), lb.marker);
        }
        if (selId == lb.id && selStart >= 0 && selEnd > selStart) {
            const QString t = text.isEmpty() ? QString() : text;
            const int n = qMax(1, t.size());
            const qreal x0 = lb.rect.left() + lb.markerWidth + qreal(selStart) / n * (lb.rect.width() - lb.markerWidth);
            const qreal x1 = lb.rect.left() + lb.markerWidth + qreal(selEnd) / n * (lb.rect.width() - lb.markerWidth);
            p->fillRect(QRectF(x0, lb.rect.top(), qMax(2.0, x1 - x0), qMin(lb.rect.height(), 22.0 * lay.zoom)),
                        QColor(QStringLiteral("#B5651D40")));
        }
        if (dc.lines > 0 && !dc.letter.isEmpty()) {
            p->setFont(dc.font);
            p->setPen(col);
            p->drawText(QPointF(lb.rect.left() + lb.markerWidth, lb.rect.top() + QFontMetricsF(dc.font).ascent()),
                        dc.letter);
            p->setFont(f);
        }
        p->setPen(col);
        QTextLayout layout(text.isEmpty() ? QString() : text, f);
        QTextOption opt;
        opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        const StyleDef *st = styleNamed(doc, b.style);
        opt.setAlignment(qtAlign(b.align.isEmpty() && st ? st->align : b.align));
        applyTabs(opt, b, lay.zoom);
        layout.setTextOption(opt);
        QList<QTextLayout::FormatRange> fmts;
        int pos = 0;
        int rawPos = 0;
        for (const auto &r : b.runs) {
            QString disp = runDisplayText(doc, r, shown, lay.pages);
            const int rawLen = disp.size();
            if (rawPos + rawLen <= skip) {
                rawPos += rawLen;
                continue;
            }
            disp = disp.mid(qMax(0, skip - rawPos));
            if (doc.page.hyphenate)
                disp = hyphenateDisplay(disp);
            QTextLayout::FormatRange fr;
            fr.start = pos;
            fr.length = disp.size();
            QTextCharFormat cf;
            cf.setFont(fontFor(doc, b, r, lay.zoom));
            if (r.strike)
                cf.setFontStrikeOut(true);
            if (r.superscript)
                cf.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
            if (r.subscript)
                cf.setVerticalAlignment(QTextCharFormat::AlignSubScript);
            if (!r.color.isEmpty())
                cf.setForeground(QColor(r.color));
            else if (!r.charStyle.isEmpty()) {
                if (const StyleDef *cs = styleNamed(doc, r.charStyle); cs && !cs->color.isEmpty())
                    cf.setForeground(QColor(cs->color));
            } else if (!r.link.isEmpty())
                cf.setForeground(QColor(QStringLiteral("#B5651D")));
            if (!r.highlight.isEmpty())
                cf.setBackground(QColor(r.highlight));
            if (!r.link.isEmpty())
                cf.setFontUnderline(true);
            fr.format = cf;
            fmts.append(fr);
            pos += fr.length;
            rawPos += rawLen;
        }
        if (!fmts.isEmpty())
            layout.setFormats(fmts);
        layoutLines(layout, lb.rect.width() - lb.markerWidth, dc.width, dc.lines);
        layout.draw(p, QPointF(lb.rect.left() + lb.markerWidth, lb.rect.top()));
        int openComments = 0;
        for (const auto &c : b.comments) {
            if (!c.resolved)
                ++openComments;
        }
        if (openComments > 0) {
            const qreal s = 14 * lay.zoom;
            QRectF mark(lay.paperW - lay.marginR + 2, lb.rect.top(), s, s);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(QStringLiteral("#B5651D")));
            p->drawEllipse(mark);
            p->setPen(QColor(QStringLiteral("#FDFBF7")));
            QFont mf(QStringLiteral("IBM Plex Sans"));
            mf.setPointSizeF(8 * lay.zoom);
            mf.setBold(true);
            p->setFont(mf);
            p->drawText(mark, Qt::AlignCenter, QString::number(openComments));
        }
    }

    if (caretOn && !selId.isEmpty()) {
        const auto cr = lay.caret(doc, selId, caretOff, selRow, selCol);
        if (cr.ok && cr.page == page) {
            p->fillRect(cr.rect, QColor(QStringLiteral("#B5651D")));
        }
    }
}

PreviewResult renderPage(const Document &doc, int page, double zoom)
{
    DocumentLayout lay;
    lay.relayout(doc, zoom);
    page = qBound(1, page, lay.pages);
    PreviewResult res;
    res.page = page;
    res.pages = lay.pages;
    res.image = QImage(qMax(1, lay.paperW), qMax(1, lay.paperH), QImage::Format_ARGB32_Premultiplied);
    res.image.fill(QColor(QStringLiteral("#FDFBF7")));
    QPainter p(&res.image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    paintPage(&p, doc, lay, page);
    p.end();
    return res;
}

QByteArray renderPagePng(const Document &doc, int page, double zoom)
{
    const auto r = renderPage(doc, page, zoom);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    r.image.save(&buf, "PNG");
    return bytes;
}

bool exportNativePdf(const Document &doc, const QString &path, QString *error, bool pdfa)
{
    double wMm = 0, hMm = 0;
    doc.page.pageSizeMm(wMm, hMm);
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QSizeF(wMm, hMm), QPageSize::Millimeter));
    writer.setResolution(96);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setTitle(doc.title);
    writer.setCreator(QStringLiteral("AI Office"));
    if (pdfa)
        writer.setPdfVersion(QPagedPaintDevice::PdfVersion_A1b);
    QPainter p(&writer);
    if (!p.isActive()) {
        if (error)
            *error = QStringLiteral("Cannot write PDF ") + path;
        return false;
    }
    DocumentLayout lay;
    lay.relayout(doc, 1.0);
    for (int pg = 1; pg <= lay.pages; ++pg) {
        if (pg > 1)
            writer.newPage();
        const qreal sx = writer.width() / qreal(qMax(1, lay.paperW));
        const qreal sy = writer.height() / qreal(qMax(1, lay.paperH));
        p.save();
        p.scale(sx, sy);
        paintPage(&p, doc, lay, pg);
        p.restore();
    }
    p.end();
    return true;
}

} // namespace aioffice
