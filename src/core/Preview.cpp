#include "core/Preview.h"
#include "core/Hyphenation.h"
#include "core/Qr.h"

#include <QBuffer>
#include <QJsonObject>
#include <QFontMetricsF>
#include <QMap>
#include <QMarginsF>
#include <QPageSize>
#include <QPainter>
#include <QPen>
#include <QPagedPaintDevice>
#include <QPdfWriter>
#include <QSet>
#include <QStringList>
#include <QTextCharFormat>
#include <QTextLayout>
#include <QTextOption>
#include <algorithm>

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

QString hyphenateDisplay(QString t, double zoneMm = 0, const QString &lang = QString())
{
    Q_UNUSED(zoneMm); // real pattern-based hyphenation always applies leftMin/rightMin from the dictionary.
    QString out;
    QString word;
    auto flush = [&]() { out += hyphenateWord(word, lang); word.clear(); };
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

const HeaderFooter &headerForPage(const Document &doc, int physical, int display)
{
    if (physical == 1 && doc.headerFirst.active())
        return doc.headerFirst;
    if (display % 2 == 0 && doc.headerEven.active())
        return doc.headerEven;
    return doc.header;
}

const HeaderFooter &footerForPage(const Document &doc, int physical, int display)
{
    if (physical == 1 && doc.footerFirst.active())
        return doc.footerFirst;
    if (display % 2 == 0 && doc.footerEven.active())
        return doc.footerEven;
    return doc.footer;
}

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
                        qreal zoom, QVector<qreal> *lineHs = nullptr)
{
    QString text;
    for (const auto &r : runs)
        text += runDisplayText(doc, r, 1, 1);
    const DropCapInfo dc = dropCapInfo(doc, b, runs, zoom);
    if (dc.lines > 0 && dc.skip > 0 && dc.skip <= text.size())
        text = text.mid(dc.skip);
    if (doc.page.hyphenate)
        text = hyphenateDisplay(text, doc.page.hyphenZoneMm, b.lang);
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
    if (lineHs) {
        lineHs->clear();
        for (int i = 0; i < layout.lineCount(); ++i)
            lineHs->push_back(qMax(qreal(8), layout.lineAt(i).height()));
        if (lineHs->isEmpty())
            lineHs->push_back(qMax(QFontMetricsF(f).height(), y));
    }
    return qMax(qMax(QFontMetricsF(f).height(), y), dc.height);
}

qreal cellPadPx(const TableCell &cell, qreal zoom)
{
    if (cell.paddingMm > 0)
        return mmToPx(cell.paddingMm, zoom);
    return 4 * zoom;
}

qreal measureNestedHeight(const Document &doc, const Block &b, const TableCell &cell, qreal cw, qreal zoom)
{
    const qreal pad = cellPadPx(cell, zoom);
    if (cell.nested.isEmpty())
        return measureTextHeight(doc, b, cell.runs, qMax(qreal(12), cw - pad * 2), zoom) + pad * 2;
    const int cols = cell.nested.first().size();
    qreal h = 0;
    const qreal icw = qMax(qreal(12), (cw - pad * 2) / qMax(1, cols));
    for (const auto &nrow : cell.nested) {
        qreal rh = 16 * zoom;
        for (const auto &nc : nrow)
            rh = qMax(rh, measureTextHeight(doc, b, nc.runs, icw - 4, zoom) + 4);
        h += rh;
    }
    return h + pad * 2;
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

struct ColGeom {
    int n = 1;
    qreal gap = 0;
    QVector<qreal> x;
    QVector<qreal> w;
    qreal minW = 0;
};

ColGeom columnGeom(const Document &doc, qreal marginL, qreal contentW, qreal zoom)
{
    ColGeom g;
    g.n = qBound(1, doc.page.columns, 3);
    g.gap = mmToPx(doc.page.columnGapMm > 0 ? doc.page.columnGapMm : 5, zoom);
    g.x.resize(g.n);
    g.w.resize(g.n);
    const qreal usable = g.n <= 1 ? contentW : contentW - g.gap * (g.n - 1);
    QVector<double> weights = doc.page.columnWeights;
    while (weights.size() < g.n)
        weights.push_back(1.0);
    if (weights.size() > g.n)
        weights.resize(g.n);
    double sum = 0;
    for (double x : weights)
        sum += qMax(0.01, x);
    qreal acc = marginL;
    g.minW = usable;
    for (int i = 0; i < g.n; ++i) {
        g.x[i] = acc;
        g.w[i] = usable * qMax(0.01, weights[i]) / sum;
        g.minW = qMin(g.minW, g.w[i]);
        acc += g.w[i] + g.gap;
    }
    return g;
}

QString chapterTitleOnPage(const Document &doc, const DocumentLayout &lay, int page)
{
    QString last;
    for (const auto &lb : lay.blocks) {
        if (lb.page > page)
            break;
        const Block *b = doc.findBlock(lb.id);
        if (b && b->type == QLatin1String("heading") && b->level <= 1)
            last = b->plainText().trimmed();
    }
    return last;
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
    const qreal headerBand = (doc.header.active() || doc.headerEven.active() || doc.headerFirst.active())
        ? 22 * zoom
        : 0;
    const qreal footerBand = (doc.footer.active() || doc.footerEven.active() || doc.footerFirst.active())
        ? 22 * zoom
        : 0;
    int fnCount = 0;
    for (const auto &b : doc.body) {
        if (b.type == QLatin1String("footnote"))
            ++fnCount;
    }
    const qreal fnBand = fnCount > 0 ? qMin(qreal(120) * zoom, 28 * zoom * fnCount + 8) : 0;
    contentW = paperW - marginL - marginR;
    contentH = paperH - marginT - marginB - headerBand - footerBand - fnBand;
    const ColGeom cols = columnGeom(doc, marginL, contentW, zoom);
    const int colN = cols.n;
    const qreal colWidth = cols.minW;

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
        QVector<qreal> lineH;
    };
    QVector<Measure> meas;
    for (int i = 0; i < doc.body.size(); ++i) {
        const Block &b = doc.body[i];
        Measure m;
        const StyleDef *st = styleNamed(doc, b.style);
        const qreal beforePt = b.spaceBeforePt >= 0 ? b.spaceBeforePt : (st ? st->spaceBeforePt : 0);
        m.before = beforePt * zoom * kDpi / 72.0;
        if (b.sectionHidden) {
            meas.push_back(m);
            continue;
        }
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
                const int rot = ((b.rotateDeg % 360) + 360) % 360;
                if (rot == 90 || rot == 270)
                    h = w * img.width() / qMax(1, img.height());
                else
                    h = w * img.height() / qMax(1, img.width());
            }
            m.height = h + 8 + (!b.caption.isEmpty() ? 16 * zoom : 0);
            meas.push_back(m);
            continue;
        }
        if (b.type == QLatin1String("shape") || b.type == QLatin1String("chart") || b.type == QLatin1String("qr")
            || b.type == QLatin1String("equation") || b.type == QLatin1String("form")) {
            const qreal wmm = b.imageWidthMm > 0 ? b.imageWidthMm : (b.type == QLatin1String("qr") ? 40.0 : 80.0);
            const qreal hmm = b.heightMm > 0 ? b.heightMm : (b.type == QLatin1String("equation") || b.type == QLatin1String("form") ? 12.0 : 40.0);
            m.height = mmToPx(hmm, zoom) + 8 + (!b.caption.isEmpty() ? 16 * zoom : 0);
            if (b.type == QLatin1String("qr"))
                m.height = mmToPx(wmm, zoom) + 8;
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
        qreal textW = colWidth - m.markerW - indent - mmToPx(b.indentRightMm, zoom);
        if (b.type == QLatin1String("frame") && b.imageWidthMm > 0)
            textW = qMin(textW, mmToPx(b.imageWidthMm, zoom));
        m.height = measureTextHeight(doc, b, b.runs, qMax(qreal(20), textW), zoom, &m.lineH);
        if (b.type == QLatin1String("frame"))
            m.height += 12 * zoom;
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
    auto colX = [&](int c) { return cols.x.value(c, marginL); };
    auto colW = [&](int c) { return cols.w.value(c, contentW); };
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
    QVector<int> pendingEn;
    QSet<int> placedEn;
    for (int i = 0; i < doc.body.size(); ++i) {
        const Block &b = doc.body[i];
        if (b.sectionHidden)
            continue;
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
                if (b.breakKind == QLatin1String("section") && !pendingEn.isEmpty()) {
                    if (y > 0) {
                        col = 0;
                        y = 0;
                        advancePage(false, 0);
                    }
                    for (int ni : pendingEn) {
                        if (placedEn.contains(ni))
                            continue;
                        const Block &en = doc.body[ni];
                        const qreal h = qMax(qreal(16 * zoom),
                                             measureTextHeight(doc, en, en.runs, contentW, zoom) + 8);
                        LaidBlock elb;
                        elb.id = en.id;
                        elb.type = en.type;
                        elb.page = page;
                        elb.rect = QRectF(marginL, marginT + headerBand + y, contentW, h);
                        blocks.push_back(elb);
                        placedEn.insert(ni);
                        y += h;
                    }
                    pendingEn.clear();
                }
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
        if (b.breakBefore && y > 0) {
            col = 0;
            y = 0;
            advancePage(false, 0);
        }
        if (b.type == QLatin1String("table")) {
            y += meas[i].before;
            const qreal indent = mmToPx(b.indentLeftMm, zoom);
            const qreal indentR = mmToPx(b.indentRightMm, zoom);
            const int nRows = b.table.size();
            const bool repeatHdr = b.headerRow && nRows > 1;
            int dataStart = 0;
            auto rowH = [&](int r) { return meas[i].rowH.value(r, 22 * zoom); };
            while (dataStart < nRows) {
                const qreal availW = qMax(qreal(8), colW(col) - indent - indentR);
                const auto tw = tableColWidths(b, availW);
                const qreal remain = contentH - y;
                const qreal headerExtra = (repeatHdr && dataStart > 0) ? rowH(0) : 0;
                if (y > 0 && remain < headerExtra + rowH(dataStart)) {
                    ++col;
                    y = 0;
                    if (col >= colN) {
                        col = 0;
                        advancePage(false, 0);
                    }
                    continue;
                }
                QVector<int> visRows;
                qreal used = 0;
                if (repeatHdr && dataStart > 0) {
                    visRows.push_back(0);
                    used += rowH(0);
                }
                int r = dataStart;
                while (r < nRows) {
                    const qreal rh = rowH(r);
                    if (!visRows.isEmpty() && used + rh > remain)
                        break;
                    visRows.push_back(r);
                    used += rh;
                    ++r;
                }
                if (visRows.isEmpty()) {
                    visRows.push_back(dataStart);
                    used += rowH(dataStart);
                    r = dataStart + 1;
                }
                if (r >= nRows)
                    used += 8;
                LaidBlock tlb;
                tlb.id = b.id;
                tlb.type = b.type;
                tlb.page = page;
                tlb.rect = QRectF(colX(col) + indent, marginT + headerBand + y, availW, used);
                qreal ty = 0;
                for (int vr : visRows) {
                    if (vr < 0 || vr >= b.table.size())
                        continue;
                    qreal x = tlb.rect.left();
                    for (int c = 0; c < b.table[vr].size(); ++c) {
                        const qreal cw = c < tw.size() ? tw[c] : availW;
                        CellGeom g;
                        g.row = vr;
                        g.col = c;
                        g.rect = QRectF(x, tlb.rect.top() + ty, cw, rowH(vr));
                        tlb.cells.push_back(g);
                        x += cw;
                    }
                    ty += rowH(vr);
                }
                blocks.push_back(tlb);
                y += used;
                dataStart = r;
                if (dataStart < nRows) {
                    ++col;
                    y = 0;
                    if (col >= colN) {
                        col = 0;
                        advancePage(false, 0);
                    }
                }
            }
            continue;
        }
        qreal need = meas[i].before + qMax(qreal(12), meas[i].height);
        if (b.keepWithNext && i + 1 < meas.size())
            need += meas[i + 1].before + meas[i + 1].height;
        auto newColOrPage = [&]() {
            ++col;
            y = 0;
            if (col >= colN) {
                col = 0;
                advancePage(false, 0);
            }
        };
        const bool textish = b.type == QLatin1String("paragraph") || b.type == QLatin1String("heading")
            || b.type == QLatin1String("list-item") || b.type == QLatin1String("toc")
            || b.type == QLatin1String("frame");
        const QVector<qreal> &lhs = meas[i].lineH;
        if (textish && !b.keepWithNext && lhs.size() >= 2) {
            y += meas[i].before;
            const int orph = b.orphans > 0 ? b.orphans : 2;
            const int wid = b.widows > 0 ? b.widows : 2;
            int line = 0;
            qreal accY0 = 0;
            const qreal indent = mmToPx(b.indentLeftMm, zoom);
            const qreal indentR = mmToPx(b.indentRightMm, zoom);
            qreal blockW = colW(col) - indent - indentR;
            if (b.type == QLatin1String("frame") && b.imageWidthMm > 0)
                blockW = qMin(blockW, mmToPx(b.imageWidthMm, zoom));
            while (line < lhs.size()) {
                qreal remain = contentH - y;
                if (y > 0 && remain < lhs[line] * orph && (lhs.size() - line) >= orph) {
                    newColOrPage();
                    remain = contentH;
                }
                int take = 0;
                qreal used = 0;
                while (line + take < lhs.size()) {
                    const qreal h = lhs[line + take];
                    if (take > 0 && used + h > remain)
                        break;
                    used += h;
                    ++take;
                }
                if (take == 0) {
                    if (y <= 0 && line < lhs.size()) {
                        take = 1;
                        used = lhs[line];
                    } else {
                        newColOrPage();
                        continue;
                    }
                }
                const int left = int(lhs.size()) - line - take;
                if (left > 0 && left < wid && take > wid) {
                    take -= (wid - left);
                    used = 0;
                    for (int k = 0; k < take; ++k)
                        used += lhs[line + k];
                }
                LaidBlock lb;
                lb.id = b.id;
                lb.type = b.type;
                lb.page = page;
                lb.marker = line == 0 ? meas[i].marker : QString();
                lb.markerWidth = line == 0 ? meas[i].markerW : 0;
                lb.layoutY0 = accY0;
                lb.rect = QRectF(colX(col) + indent, marginT + headerBand + y, blockW, used);
                blocks.push_back(lb);
                if (line == 0) {
                    for (const auto &r : b.runs) {
                        if (r.noteId.isEmpty())
                            continue;
                        const int ni = doc.indexOf(r.noteId);
                        if (ni < 0)
                            continue;
                        if (doc.body[ni].type == QLatin1String("footnote"))
                            fnByPage[page].push_back(ni);
                        else if (doc.body[ni].type == QLatin1String("endnote") && !pendingEn.contains(ni))
                            pendingEn.push_back(ni);
                    }
                }
                y += used;
                for (int k = 0; k < take; ++k)
                    accY0 += lhs[line + k];
                line += take;
                if (line < lhs.size())
                    newColOrPage();
            }
            continue;
        }
        if (y > 0 && y + need > contentH)
            newColOrPage();
        y += meas[i].before;
        LaidBlock lb;
        lb.id = b.id;
        lb.type = b.type;
        lb.page = page;
        lb.marker = meas[i].marker;
        lb.markerWidth = meas[i].markerW;
        const qreal indent = mmToPx(b.indentLeftMm, zoom);
        const qreal indentR = mmToPx(b.indentRightMm, zoom);
        qreal blockW = colW(col) - indent - indentR;
        if (b.type == QLatin1String("frame") && b.imageWidthMm > 0)
            blockW = qMin(blockW, mmToPx(b.imageWidthMm, zoom));
        lb.rect = QRectF(colX(col) + indent, marginT + headerBand + y, blockW, meas[i].height);
        blocks.push_back(lb);
        for (const auto &r : b.runs) {
            if (r.noteId.isEmpty())
                continue;
            const int ni = doc.indexOf(r.noteId);
            if (ni < 0)
                continue;
            if (doc.body[ni].type == QLatin1String("footnote"))
                fnByPage[page].push_back(ni);
            else if (doc.body[ni].type == QLatin1String("endnote") && !pendingEn.contains(ni))
                pendingEn.push_back(ni);
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
    for (int i = 0; i < doc.body.size(); ++i) {
        if (doc.body[i].type == QLatin1String("endnote") && !placedEn.contains(i) && !pendingEn.contains(i))
            pendingEn.push_back(i);
    }
    if (!pendingEn.isEmpty()) {
        advancePage(false, 0);
        qreal ey = 0;
        for (int ni : pendingEn) {
            if (placedEn.contains(ni))
                continue;
            const Block &b = doc.body[ni];
            const qreal h = qMax(qreal(16 * zoom), measureTextHeight(doc, b, b.runs, contentW, zoom) + 8);
            LaidBlock lb;
            lb.id = b.id;
            lb.type = b.type;
            lb.page = page;
            lb.rect = QRectF(marginL, marginT + headerBand + ey, contentW, h);
            blocks.push_back(lb);
            placedEn.insert(ni);
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
            continue;
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

static QString liveDisplayText(const Document &doc, const TextRun &r, int page, int pages,
                                const DocumentLayout &lay)
{
    if (r.field == QLatin1String("xref") && r.xrefKind == QLatin1String("page")) {
        const int physical = lay.pageOf(r.fieldArg);
        return formatPageNumber(lay.displayPage(physical), doc.page.numberFormat);
    }
    return runDisplayText(doc, r, page, pages);
}

void paintPage(QPainter *p, const Document &doc, const DocumentLayout &lay, int page,
               const QString &selId, int selStart, int selEnd, int selRow, int selCol, bool caretOn,
               int caretOff)
{
    QString paper = doc.page.background;
    if (paper.isEmpty()) {
        if (doc.page.tone == QLatin1String("sepia"))
            paper = QStringLiteral("#F4E6D0");
        else if (doc.page.tone == QLatin1String("night"))
            paper = QStringLiteral("#1D1916");
        else
            paper = QStringLiteral("#FDFBF7");
    }
    p->fillRect(0, 0, lay.paperW, lay.paperH, QColor(paper));
    p->fillRect(0, 0, lay.paperW, 3, QColor(QStringLiteral("#B5651D")));
    if (doc.page.gridMm > 0) {
        const qreal step = mmToPx(doc.page.gridMm, lay.zoom);
        p->setPen(QPen(QColor(QStringLiteral("#E8DFD2")), 1, Qt::DotLine));
        for (qreal x = lay.marginL; x < lay.paperW - lay.marginR; x += step)
            p->drawLine(QPointF(x, lay.marginT), QPointF(x, lay.paperH - lay.marginB));
        for (qreal y = lay.marginT; y < lay.paperH - lay.marginB; y += step)
            p->drawLine(QPointF(lay.marginL, y), QPointF(lay.paperW - lay.marginR, y));
    }
    const int shownEarly = lay.displayPage(page);
    if (doc.page.gutterMirror && shownEarly % 2 == 0 && doc.page.gutterMm > 0)
        p->translate(-mmToPx(doc.page.gutterMm, lay.zoom), 0);
    if (!doc.watermark.isEmpty()) {
        p->save();
        p->setOpacity(0.12);
        QFont wf(QStringLiteral("IBM Plex Sans"));
        wf.setPointSizeF(42 * lay.zoom);
        wf.setBold(true);
        p->setFont(wf);
        p->setPen(QColor(QStringLiteral("#2A2420")));
        p->translate(lay.paperW / 2.0, lay.paperH / 2.0);
        p->rotate(-38);
        p->drawText(QRectF(-lay.paperW, -50 * lay.zoom, lay.paperW * 2, 100 * lay.zoom),
                    Qt::AlignCenter, doc.watermark);
        p->restore();
    }
    if (doc.page.borderPt > 0) {
        QPen bp(QColor(doc.page.borderColor.isEmpty() ? QStringLiteral("#B5651D") : doc.page.borderColor),
                qMax(qreal(0.5), doc.page.borderPt * lay.zoom));
        p->setPen(bp);
        p->setBrush(Qt::NoBrush);
        p->drawRect(QRectF(lay.marginL - 4, lay.marginT - 4,
                           lay.contentW + 8, lay.paperH - lay.marginT - lay.marginB + 8));
    }

    const int shown = lay.displayPage(page);
    QMap<QString, int> pageNoteNo;
    if (doc.page.noteRestart) {
        int n = 0;
        for (const auto &lb0 : lay.blocks) {
            if (lb0.page != page)
                continue;
            const Block *nb = doc.findBlock(lb0.id);
            if (nb && nb->type == QLatin1String("footnote"))
                pageNoteNo[lb0.id] = ++n;
        }
    }
    const HeaderFooter &hdr = headerForPage(doc, page, shown);
    const HeaderFooter &ftr = footerForPage(doc, page, shown);
    const bool hasHeader = hdr.active();
    const bool hasFooter = ftr.active();
    auto pageLabel = [&]() {
        return (hdr.pageNumbers || ftr.pageNumbers)
            ? QStringLiteral("Página %1 de %2")
                  .arg(formatPageNumber(shown, doc.page.numberFormat))
                  .arg(lay.pages)
            : QString();
    };
    p->setPen(QColor(QStringLiteral("#6B6258")));
    QFont hf(QStringLiteral("IBM Plex Sans"));
    hf.setPointSizeF(9 * lay.zoom);
    p->setFont(hf);
    if (hasHeader) {
        QString t = hdr.text;
        const QString chapter = chapterTitleOnPage(doc, lay, page);
        t.replace(QStringLiteral("{chapter}"), chapter);
        if (hdr.chapter && !chapter.isEmpty())
            t = t.isEmpty() ? chapter : t + QStringLiteral("  ·  ") + chapter;
        if (hdr.pageNumbers)
            t = t.isEmpty() ? pageLabel() : t + QStringLiteral("  ·  ") + pageLabel();
        p->drawText(QRectF(lay.marginL, 8 * lay.zoom, lay.contentW, 18 * lay.zoom), Qt::AlignLeft | Qt::AlignVCenter, t);
        if (!hdr.imagePath.isEmpty()) {
            QImage himg(hdr.imagePath);
            if (!himg.isNull()) {
                const QImage sc = himg.scaledToHeight(qMax(8, int(16 * lay.zoom)), Qt::SmoothTransformation);
                p->drawImage(QPointF(lay.marginL + lay.contentW - sc.width(), 6 * lay.zoom), sc);
            }
        }
    }
    if (hasFooter) {
        QString t = ftr.text;
        const QString chapter = chapterTitleOnPage(doc, lay, page);
        t.replace(QStringLiteral("{chapter}"), chapter);
        if (ftr.chapter && !chapter.isEmpty())
            t = t.isEmpty() ? chapter : t + QStringLiteral("  ·  ") + chapter;
        if (ftr.pageNumbers)
            t = t.isEmpty() ? pageLabel() : t + QStringLiteral("  ·  ") + pageLabel();
        p->drawText(QRectF(lay.marginL, lay.paperH - lay.marginB + 2, lay.contentW, 18 * lay.zoom),
                    Qt::AlignLeft | Qt::AlignVCenter, t);
    }

    if (doc.page.columnSeparator && doc.page.columns > 1) {
        const ColGeom cols = columnGeom(doc, lay.marginL, lay.contentW, lay.zoom);
        p->setPen(QPen(QColor(QStringLiteral("#C4B8A8")), 1));
        for (int c = 1; c < cols.n; ++c) {
            const qreal x = cols.x[c] - cols.gap / 2;
            p->drawLine(QPointF(x, lay.marginT), QPointF(x, lay.paperH - lay.marginB));
        }
    }

    QMap<QString, QString> chapNo;
    if (doc.page.chapterNumbers) {
        int c[7] = {};
        for (const auto &hb : doc.body) {
            if (hb.type != QLatin1String("heading"))
                continue;
            const int lv = qBound(1, hb.level, 6);
            ++c[lv];
            for (int k = lv + 1; k < 7; ++k)
                c[k] = 0;
            QString s;
            for (int k = 1; k <= lv; ++k) {
                if (!s.isEmpty())
                    s += QLatin1Char('.');
                s += QString::number(qMax(1, c[k]));
            }
            chapNo[hb.id] = s;
        }
    }
    int lineNo = 0;
    bool fnSep = false;

    QVector<LaidBlock> ordered;
    for (const auto &lb : lay.blocks) {
        if (lb.page == page)
            ordered.push_back(lb);
    }
    std::sort(ordered.begin(), ordered.end(), [&](const LaidBlock &a, const LaidBlock &b) {
        const Block *ba = doc.findBlock(a.id);
        const Block *bb = doc.findBlock(b.id);
        return (ba ? ba->zIndex : 0) < (bb ? bb->zIndex : 0);
    });

    for (const auto &lb : ordered) {
        if (lb.page != page)
            continue;
        const Block *bp = doc.findBlock(lb.id);
        if (!bp || bp->type == QLatin1String("break") || bp->sectionHidden)
            continue;
        const Block &b = *bp;
        if (b.type == QLatin1String("table")) {
            for (const auto &g : lb.cells) {
                const bool head = b.headerRow && g.row == 0;
                const TableCell *cellp = (g.row < b.table.size() && g.col < b.table[g.row].size())
                    ? &b.table[g.row][g.col]
                    : nullptr;
                QColor fill = head ? QColor(QStringLiteral("#FBF0E6")) : QColor(QStringLiteral("#FDFBF7"));
                if (head) {
                    if (const StyleDef *st = styleNamed(doc, b.style); st && !st->color.isEmpty()
                        && st->kind == QLatin1String("table"))
                        fill = QColor(st->color);
                }
                if (cellp && !cellp->background.isEmpty())
                    fill = QColor(cellp->background);
                if (selId == lb.id && selRow == g.row && selCol == g.col)
                    fill = QColor(QStringLiteral("#F3E0CC"));
                QPen cellPen(QColor(QStringLiteral("#D2C8B8")), 1);
                if (cellp && cellp->borderPt > 0) {
                    cellPen = QPen(QColor(cellp->borderColor.isEmpty() ? QStringLiteral("#B5651D") : cellp->borderColor),
                                   qMax(qreal(0.5), cellp->borderPt * lay.zoom));
                }
                p->setPen(cellPen);
                p->setBrush(fill);
                p->drawRect(g.rect);
                if (cellp && !cellp->nested.isEmpty()) {
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
                    if (cellp) {
                        for (const auto &r : cellp->runs)
                            t += r.text;
                    }
                    p->setPen(QColor(QStringLiteral("#1A1F24")));
                    QFont tf(QStringLiteral("Liberation Serif"));
                    tf.setPointSizeF(10 * lay.zoom);
                    p->setFont(tf);
                    const qreal pad = cellp ? cellPadPx(*cellp, lay.zoom) : 4 * lay.zoom;
                    Qt::Alignment va = Qt::AlignVCenter;
                    if (cellp) {
                        if (cellp->vAlign == QLatin1String("top"))
                            va = Qt::AlignTop;
                        else if (cellp->vAlign == QLatin1String("bottom"))
                            va = Qt::AlignBottom;
                    }
                    p->drawText(g.rect.adjusted(pad, pad, -pad, -pad), va | Qt::AlignLeft | Qt::TextWordWrap, t);
                }
            }
            continue;
        }
        if (b.type == QLatin1String("image")) {
            QImage img = loadBlockImage(b);
            QRectF box = lb.rect.adjusted(0, 0, 0, -8);
            if (!b.caption.isEmpty())
                box.adjust(0, 0, 0, -16 * lay.zoom);
            if (!img.isNull()) {
                if (b.cropL > 0 || b.cropT > 0 || b.cropR > 0 || b.cropB > 0) {
                    const QRect cr(int(img.width() * qBound(0.0, b.cropL, 0.9)),
                                   int(img.height() * qBound(0.0, b.cropT, 0.9)),
                                   int(img.width() * qBound(0.1, 1.0 - b.cropL - b.cropR, 1.0)),
                                   int(img.height() * qBound(0.1, 1.0 - b.cropT - b.cropB, 1.0)));
                    img = img.copy(cr);
                }
                if (b.flipH)
                    img = img.flipped(Qt::Horizontal);
                if (b.flipV)
                    img = img.flipped(Qt::Vertical);
                p->save();
                p->setOpacity(qBound(0.05, b.opacity, 1.0));
                if (b.shadowPt > 0) {
                    const qreal sh = b.shadowPt * lay.zoom;
                    p->fillRect(box.adjusted(sh, sh, sh, sh), QColor(42, 36, 32, 40));
                }
                QImage sc = img.scaled(box.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                if (b.rotateDeg != 0) {
                    p->translate(box.center());
                    p->rotate(b.rotateDeg);
                    p->translate(-box.center());
                }
                p->drawImage(box.topLeft(), sc);
                p->restore();
            } else {
                p->setPen(QPen(QColor(QStringLiteral("#C9C2B6")), 1, Qt::DashLine));
                p->setBrush(QColor(QStringLiteral("#F3EDE3")));
                p->drawRect(box);
                p->setPen(QColor(QStringLiteral("#6B6258")));
                p->drawText(box, Qt::AlignCenter, b.imagePath.isEmpty() ? QStringLiteral("imagem") : b.imagePath);
            }
            if (!b.caption.isEmpty()) {
                p->setPen(QColor(QStringLiteral("#6B6258")));
                QFont cf(QStringLiteral("IBM Plex Sans"));
                cf.setPointSizeF(8 * lay.zoom);
                p->setFont(cf);
                p->drawText(QRectF(lb.rect.left(), box.bottom(), lb.rect.width(), 16 * lay.zoom),
                            Qt::AlignHCenter | Qt::AlignVCenter, b.caption);
            }
            continue;
        }
        if (b.type == QLatin1String("shape") || b.type == QLatin1String("chart") || b.type == QLatin1String("qr")
            || b.type == QLatin1String("equation") || b.type == QLatin1String("form")) {
            QRectF box = lb.rect.adjusted(0, 0, 0, -4);
            if (b.shadowPt > 0) {
                const qreal sh = b.shadowPt * lay.zoom;
                p->fillRect(box.adjusted(sh, sh, sh, sh), QColor(42, 36, 32, 40));
            }
            if (b.type == QLatin1String("qr")) {
                const QImage qr = encodeQr(b.qrText.isEmpty() ? b.plainText() : b.qrText, 3);
                p->drawImage(box.toRect(), qr);
            } else if (b.type == QLatin1String("shape")) {
                QColor fill(b.fill.isEmpty() ? QStringLiteral("#F3E0CC") : b.fill);
                QPen sp(QColor(b.stroke.isEmpty() ? QStringLiteral("#B5651D") : b.stroke), qMax(1.0, 0.75 * lay.zoom));
                p->setPen(sp);
                p->setBrush(fill);
                const QString k = b.shapeKind.isEmpty() ? QStringLiteral("rect") : b.shapeKind;
                if (k == QLatin1String("ellipse"))
                    p->drawEllipse(box);
                else if (k == QLatin1String("line"))
                    p->drawLine(box.topLeft(), box.bottomRight());
                else
                    p->drawRect(box);
            } else if (b.type == QLatin1String("chart")) {
                p->setPen(QPen(QColor(QStringLiteral("#D2C8B8")), 1));
                p->setBrush(QColor(QStringLiteral("#FDFBF7")));
                p->drawRect(box);
                double maxV = 1;
                QVector<double> vals;
                for (const auto &v : b.data) {
                    const auto o = v.toObject();
                    const double val = o.value(QStringLiteral("value")).toDouble();
                    maxV = qMax(maxV, val);
                    vals.push_back(val);
                }
                const qreal barW = box.width() / qMax(1, vals.size() * 2);
                for (int i = 0; i < vals.size(); ++i) {
                    const qreal h = (box.height() - 16) * vals[i] / maxV;
                    const qreal x = box.left() + barW * (i * 2 + 0.5);
                    p->fillRect(QRectF(x, box.bottom() - 8 - h, barW, h), QColor(QStringLiteral("#B5651D")));
                }
            } else if (b.type == QLatin1String("equation")) {
                p->setPen(QPen(QColor(QStringLiteral("#C4B8A8")), 1));
                p->setBrush(QColor(QStringLiteral("#FDFBF7")));
                p->drawRect(box);
                QFont ef(QStringLiteral("Liberation Serif"));
                ef.setItalic(true);
                ef.setPointSizeF(12 * lay.zoom);
                p->setFont(ef);
                p->setPen(QColor(QStringLiteral("#1A1F24")));
                p->drawText(box.adjusted(8, 0, -28, 0), Qt::AlignVCenter | Qt::AlignLeft, b.plainText());
                if (b.numbered)
                    p->drawText(box.adjusted(0, 0, -8, 0), Qt::AlignVCenter | Qt::AlignRight,
                                QStringLiteral("(%1)").arg(b.level > 0 ? b.level : 1));
            } else {
                p->setPen(QPen(QColor(QStringLiteral("#B5651D")), 1));
                p->setBrush(QColor(QStringLiteral("#FDFBF7")));
                p->drawRect(box);
                QString label = b.plainText();
                if (b.formKind == QLatin1String("checkbox"))
                    label = (b.checked ? QStringLiteral("☑ ") : QStringLiteral("☐ ")) + label;
                p->setPen(QColor(QStringLiteral("#1A1F24")));
                p->drawText(box.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
            }
            continue;
        }
        const qreal borderPt = b.borderPt > 0 ? b.borderPt
            : (b.type == QLatin1String("frame") ? 0.75 : 0);
        if (borderPt > 0) {
            QPen bp(QColor(b.borderColor.isEmpty() ? QStringLiteral("#B5651D") : b.borderColor),
                    qMax(qreal(0.5), borderPt * lay.zoom));
            p->setPen(bp);
            p->setBrush(Qt::NoBrush);
            p->drawRect(lb.rect.adjusted(-2, -1, 2, 1));
        }
        QString raw;
        for (const auto &r : b.runs)
            raw += liveDisplayText(doc, r, shown, lay.pages, lay);
        if (b.type == QLatin1String("footnote") && doc.page.noteRestart && pageNoteNo.contains(b.id))
            raw = QString::number(pageNoteNo.value(b.id)) + QStringLiteral("  ") + raw;
        if (b.type == QLatin1String("heading") && chapNo.contains(b.id))
            raw = chapNo.value(b.id) + QStringLiteral("  ") + raw;
        const DropCapInfo dc = dropCapInfo(doc, b, b.runs, lay.zoom);
        const int skip = dc.lines > 0 ? dc.skip : 0;
        QString text = raw.mid(skip);
        if (doc.page.hyphenate)
            text = hyphenateDisplay(text, doc.page.hyphenZoneMm, b.lang);
        if (b.type == QLatin1String("footnote") && !fnSep) {
            p->setPen(QPen(QColor(QStringLiteral("#C4B8A8")), 1));
            p->drawLine(QPointF(lb.rect.left(), lb.rect.top() - 4),
                        QPointF(lb.rect.left() + qMin(lb.rect.width(), qreal(80 * lay.zoom)), lb.rect.top() - 4));
            fnSep = true;
        }
        if (doc.page.lineNumbers
            && (b.type == QLatin1String("paragraph") || b.type == QLatin1String("heading")
                || b.type == QLatin1String("list-item"))) {
            ++lineNo;
            p->setPen(QColor(QStringLiteral("#A3988C")));
            QFont lf(QStringLiteral("IBM Plex Sans"));
            lf.setPointSizeF(7 * lay.zoom);
            p->setFont(lf);
            p->drawText(QRectF(4, lb.rect.top(), lay.marginL - 8, 14 * lay.zoom),
                        Qt::AlignRight | Qt::AlignVCenter, QString::number(lineNo));
        }
        TextRun probe = b.runs.isEmpty() ? TextRun{} : b.runs.first();
        QFont f = fontFor(doc, b, probe, lay.zoom);
        if (b.lang.startsWith(QLatin1String("ar")) || probe.lang.startsWith(QLatin1String("ar")))
            f.setLetterSpacing(QFont::PercentageSpacing, 112);
        p->setFont(f);
        QColor col(QStringLiteral("#1A1F24"));
        if (doc.page.tone == QLatin1String("night"))
            col = QColor(QStringLiteral("#FBF7F0"));
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
        const bool vert = b.writingMode == QLatin1String("vertical")
            || doc.page.writingMode == QLatin1String("vertical");
        if (vert) {
            p->save();
            p->translate(lb.rect.center());
            p->rotate(90);
            p->translate(-lb.rect.center());
        }
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
            QString disp = liveDisplayText(doc, r, shown, lay.pages, lay);
            if (!r.noteId.isEmpty() && doc.page.noteRestart && pageNoteNo.contains(r.noteId))
                disp = QString::number(pageNoteNo.value(r.noteId));
            const int rawLen = disp.size();
            if (rawPos + rawLen <= skip) {
                rawPos += rawLen;
                continue;
            }
            disp = disp.mid(qMax(0, skip - rawPos));
            if (doc.page.hyphenate)
                disp = hyphenateDisplay(disp, doc.page.hyphenZoneMm, r.lang.isEmpty() ? b.lang : r.lang);
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
        p->save();
        p->setClipRect(lb.rect.adjusted(-2, -1, 2, 1));
        layout.draw(p, QPointF(lb.rect.left() + lb.markerWidth, lb.rect.top() - lb.layoutY0));
        p->restore();
        {
            qreal rx = lb.rect.left() + lb.markerWidth;
            QFont rf = f;
            rf.setPointSizeF(qMax(6.0, f.pointSizeF() * 0.45));
            p->setFont(rf);
            p->setPen(col);
            for (const auto &r : b.runs) {
                const QString d = liveDisplayText(doc, r, shown, lay.pages, lay);
                if (!r.ruby.isEmpty())
                    p->drawText(QPointF(rx, lb.rect.top() - 1), r.ruby);
                rx += QFontMetricsF(f).horizontalAdvance(d);
            }
        }
        if (vert)
            p->restore();
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

    p->setPen(QPen(QColor(QStringLiteral("#B5651D")), qMax(1.0, 0.75 * lay.zoom), Qt::DashLine));
    for (const auto &lb : ordered) {
        const Block *bp = doc.findBlock(lb.id);
        if (!bp || bp->connectTo.isEmpty())
            continue;
        for (const auto &other : ordered) {
            if (other.id == bp->connectTo) {
                p->drawLine(lb.rect.center(), other.rect.center());
                break;
            }
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

bool exportNativePdf(const Document &doc, const QString &path, QString *error, bool pdfa,
                     int fromPage, int toPage, bool booklet, int copies, bool comments)
{
    double wMm = 0, hMm = 0;
    doc.page.pageSizeMm(wMm, hMm);
    DocumentLayout lay;
    lay.relayout(doc, 1.0);
    int lo = fromPage > 0 ? fromPage : 1;
    int hi = toPage > 0 ? toPage : lay.pages;
    lo = qBound(1, lo, qMax(1, lay.pages));
    hi = qBound(lo, hi, qMax(1, lay.pages));
    QVector<int> seq;
    for (int i = lo; i <= hi; ++i)
        seq.push_back(i);
    if (booklet) {
        while (seq.size() % 4 != 0)
            seq.push_back(0);
        QVector<int> order;
        const int n = seq.size();
        for (int s = 0; s < n / 2; s += 2) {
            order.push_back(seq[n - 1 - s]);
            order.push_back(seq[s]);
            if (s + 1 < n / 2) {
                order.push_back(seq[s + 1]);
                order.push_back(seq[n - 2 - s]);
            }
        }
        seq = order;
    }
    QPdfWriter writer(path);
    if (booklet)
        writer.setPageSize(QPageSize(QSizeF(wMm * 2, hMm), QPageSize::Millimeter));
    else
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
    auto paintDocPage = [&](int pg, const QRectF &slot) {
        if (pg < 1 || pg > lay.pages)
            return;
        p.save();
        p.translate(slot.topLeft());
        const qreal sx = slot.width() / qreal(qMax(1, lay.paperW));
        const qreal sy = slot.height() / qreal(qMax(1, lay.paperH));
        p.scale(sx, sy);
        paintPage(&p, doc, lay, pg);
        p.restore();
    };
    bool first = true;
    auto emitSeq = [&]() {
        if (booklet) {
            for (int i = 0; i < seq.size(); i += 2) {
                if (!first)
                    writer.newPage();
                first = false;
                const qreal half = writer.width() / 2.0;
                const qreal hh = writer.height();
                paintDocPage(seq.value(i), QRectF(0, 0, half, hh));
                paintDocPage(seq.value(i + 1), QRectF(half, 0, half, hh));
            }
        } else {
            for (int pg : seq) {
                if (!first)
                    writer.newPage();
                first = false;
                paintDocPage(pg, QRectF(0, 0, writer.width(), writer.height()));
            }
        }
    };
    const int nCopies = qMax(1, copies);
    for (int c = 0; c < nCopies; ++c)
        emitSeq();
    if (comments) {
        QStringList lines;
        for (const auto &b : doc.body) {
            for (const auto &cm : b.comments) {
                if (cm.resolved)
                    continue;
                lines << QStringLiteral("%1 [%2]: %3").arg(b.id, cm.author, cm.text);
            }
        }
        if (!lines.isEmpty()) {
            if (!first)
                writer.newPage();
            first = false;
            p.fillRect(QRectF(0, 0, writer.width(), writer.height()), QColor(QStringLiteral("#FDFBF7")));
            QFont cf(QStringLiteral("IBM Plex Sans"));
            cf.setPointSize(11);
            p.setFont(cf);
            p.setPen(QColor(QStringLiteral("#2A2420")));
            qreal y = 48;
            p.drawText(QPointF(36, y), QStringLiteral("Comentários"));
            y += 28;
            cf.setPointSize(9);
            p.setFont(cf);
            for (const auto &line : lines) {
                p.drawText(QRectF(36, y, writer.width() - 72, 40), Qt::TextWordWrap, line);
                y += 36;
                if (y > writer.height() - 48)
                    break;
            }
        }
    }
    p.end();
    return true;
}

} // namespace aioffice
