#include "core/Preview.h"
#include "core/FieldExpr.h"
#include "core/Hyphenation.h"
#include "core/PdfPostProcess.h"
#include "core/Qr.h"
#include "core/Zip.h"

#include <QBuffer>
#include <QFile>
#include <QTemporaryDir>
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
#include <QGlyphRun>
#include <QHash>
#include <QRawFont>
#include <QTextCharFormat>
#include <QTextLayout>
#include <QTextOption>
#include <QDateTime>
#include <QFileInfo>
#include <QtMath>
#include <algorithm>

namespace aioffice {

bool g_pdfPostProcessDisplayDocTitle = false;

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

// left/right stay physical. start/end follow the resolved paragraph direction.
// AlignAbsolute keeps Qt from mirroring left/right when the paragraph is RTL.
Qt::Alignment qtAlign(const QString &a, Qt::LayoutDirection resolved)
{
    const bool rtl = resolved == Qt::RightToLeft;
    if (a == QLatin1String("center"))
        return Qt::AlignHCenter;
    if (a == QLatin1String("justify"))
        return Qt::AlignJustify;
    if (a == QLatin1String("right") || (a == QLatin1String("end") && !rtl) || (a == QLatin1String("start") && rtl))
        return Qt::AlignRight | Qt::AlignAbsolute;
    return Qt::AlignLeft | Qt::AlignAbsolute;
}

Qt::LayoutDirection blockOptionDirection(const Block &b)
{
    if (b.dir == QLatin1String("rtl"))
        return Qt::RightToLeft;
    if (b.dir == QLatin1String("ltr"))
        return Qt::LeftToRight;
    return Qt::LayoutDirectionAuto;
}

// Same rule QTextEngine uses for LayoutDirectionAuto: first strong character.
Qt::LayoutDirection resolvedParagraphDirection(const Block &b, const QString &text)
{
    const Qt::LayoutDirection opt = blockOptionDirection(b);
    if (opt != Qt::LayoutDirectionAuto)
        return opt;
    return text.isRightToLeft() ? Qt::RightToLeft : Qt::LeftToRight;
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

// Qt subtracts the trailing-space advance from an RTL line's justification
// budget (QTextEngine::leadingSpaceWidth), so the glyphs stop short of the
// right edge by that space. Re-lay the same characters against width+sw.
// Measured on Qt 6.11: setNumColumns(textLength, width+sw) keeps the break
// and the carets then span the original measure (page ~20.00 .. ~189.81).
// A further setPosition(x-sw) clamps on the left and pulls the right edge
// back off the margin, so the line is left where setNumColumns put it.
// Must run before endLayout(). Paint, caret, and hit-test share this layout.
static void hangRtlJustifiedTrailingSpace(QTextLayout &layout)
{
    if ((layout.textOption().alignment() & Qt::AlignJustify) == 0)
        return;
    if (layout.lineCount() < 2)
        return;
    const QString text = layout.text();
    QFontMetricsF fm(layout.font());
    const qreal oneSpace = qMax(qreal(1), fm.horizontalAdvance(QLatin1Char(' ')));
    const int last = layout.lineCount() - 1;
    for (int i = 0; i < last; ++i) {
        QTextLine line = layout.lineAt(i);
        const int from = line.textStart();
        const int len = line.textLength();
        if (len <= 0 || from + len > text.size())
            continue;
        int trail = 0;
        while (trail < len && text.at(from + len - 1 - trail).isSpace())
            ++trail;
        if (trail == 0)
            continue;
        const qreal sw = (line.x() + line.width()) - line.cursorToX(from);
        if (sw <= 0.5 || sw > oneSpace * (trail + 1) + 1.0)
            continue;
        line.setNumColumns(len, line.width() + sw);
    }
}

// Qt's WrapAtWordBoundaryOrAnywhere consumes an entire overlong word before it
// retries that line as WrapAnywhere. On a run with no break (a 100k-character
// word) every line rescans the remainder, which is quadratic. When a short
// probe is already wider than the measure, the retry is certain, and breaking
// that line as WrapAnywhere matches the retry.
static bool unbreakableRunExceedsLine(const QString &text, int from, qreal width, const QFontMetricsF &fm)
{
    if (width <= 0 || from < 0 || from >= text.size())
        return false;
    constexpr int kProbe = 256;
    const int end = qMin(text.size(), from + kProbe);
    int i = from;
    for (; i < end; ++i) {
        const QChar ch = text.at(i);
        if (ch.isSpace() || ch == QChar::LineSeparator || ch == QChar::ParagraphSeparator
            || ch == QChar(0x00AD) || ch == u'\n' || ch == u'\r' || ch == u'\t')
            break;
    }
    if (i == from)
        return false;
    const qreal adv = fm.horizontalAdvance(text.mid(from, i - from));
    return adv > width * 1.5 + 1.0;
}

qreal layoutLines(QTextLayout &layout, qreal width, qreal dropW, int dropLines)
{
    QTextOption opt = layout.textOption();
    const bool preferWord = opt.wrapMode() == QTextOption::WrapAtWordBoundaryOrAnywhere;
    const QFontMetricsF fm(layout.font());
    const QString text = layout.text();
    layout.beginLayout();
    qreal y = 0;
    int lineNo = 0;
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        const bool beside = dropLines > 0 && lineNo < dropLines;
        const qreal w = beside ? qMax(qreal(20), width - dropW) : width;
        const int from = line.textStart();
        const bool anywhere = preferWord && unbreakableRunExceedsLine(text, from, w, fm);
        if (anywhere) {
            opt.setWrapMode(QTextOption::WrapAnywhere);
            layout.setTextOption(opt);
        }
        line.setLineWidth(w);
        if (anywhere) {
            opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            layout.setTextOption(opt);
        }
        line.setPosition(QPointF(beside ? dropW : 0, y));
        y += line.height();
        ++lineNo;
    }
    hangRtlJustifiedTrailingSpace(layout);
    layout.endLayout();
    return y;
}

qreal measureTextHeight(const Document &doc, const Block &b, const QVector<TextRun> &runs, qreal width,
                        qreal zoom, QVector<qreal> *lineHs = nullptr)
{
    BlockTextInput in;
    in.width = width;
    in.zoom = zoom;
    in.paint = false;
    in.fieldPage = 1;
    in.pages = 1;
    in.runs = &runs;
    const BlockTextLayout btl = blockTextLayout(doc, b, in);
    const qreal y = btl.textHeight;
    if (lineHs) {
        lineHs->clear();
        if (btl.layout) {
            for (int i = 0; i < btl.layout->lineCount(); ++i)
                lineHs->push_back(qMax(qreal(8), btl.layout->lineAt(i).height()));
        }
        if (lineHs->isEmpty())
            lineHs->push_back(qMax(QFontMetricsF(btl.font).height(), y));
    }
    return qMax(qMax(QFontMetricsF(btl.font).height(), y), btl.dropHeight);
}

qreal cellPadPx(const TableCell &cell, qreal zoom)
{
    if (cell.paddingMm > 0)
        return mmToPx(cell.paddingMm, zoom);
    return 4 * zoom;
}

// Unformatted cell runs keep the historical cell face (Liberation Serif 10 pt).
// Explicit font or size on the run still wins inside fontFor.
static QVector<TextRun> runsWithCellDefaults(const QVector<TextRun> &runs)
{
    QVector<TextRun> out = runs;
    if (out.isEmpty())
        out.push_back(TextRun{});
    for (TextRun &r : out) {
        if (r.font.isEmpty())
            r.font = QStringLiteral("Liberation Serif");
        if (r.sizePt <= 0)
            r.sizePt = 10;
    }
    return out;
}

// Cells are not paragraphs. A host block carries lang/dir so shaping matches,
// and drops table alignment so text stays left, as the old cell painter did.
static Block cellTextBlock(const Block &table)
{
    Block host;
    host.type = QStringLiteral("paragraph");
    host.style = QStringLiteral("Normal");
    host.lang = table.lang;
    host.dir = table.dir;
    host.id = table.id;
    return host;
}

struct NestedGeom {
    qreal icw = 0;
    qreal textW = 0;
    QVector<qreal> rowH;
};

// One geometry for nested-cell measurement and painting. Text width is the
// padded column minus a 2 px inset on each side (the +4 added to the row).
static NestedGeom nestedGeom(const Document &doc, const Block &host, const TableCell &cell, qreal cw, qreal zoom)
{
    NestedGeom g;
    const qreal pad = cellPadPx(cell, zoom);
    const int cols = cell.nested.isEmpty() ? 1 : qMax(1, cell.nested.first().size());
    g.icw = qMax(qreal(12), (cw - pad * 2) / cols);
    g.textW = qMax(qreal(8), g.icw - 4);
    for (const auto &nrow : cell.nested) {
        qreal rh = 16 * zoom;
        for (const auto &nc : nrow) {
            const QVector<TextRun> runs = runsWithCellDefaults(nc.runs);
            rh = qMax(rh, measureTextHeight(doc, host, runs, g.textW, zoom) + 4);
        }
        g.rowH.push_back(rh);
    }
    return g;
}

qreal measureNestedHeight(const Document &doc, const Block &b, const TableCell &cell, qreal cw, qreal zoom)
{
    const qreal pad = cellPadPx(cell, zoom);
    const Block host = cellTextBlock(b);
    if (cell.nested.isEmpty()) {
        const QVector<TextRun> runs = runsWithCellDefaults(cell.runs);
        return measureTextHeight(doc, host, runs, qMax(qreal(12), cw - pad * 2), zoom) + pad * 2;
    }
    const NestedGeom grid = nestedGeom(doc, host, cell, cw, zoom);
    qreal h = 0;
    for (qreal rh : grid.rowH)
        h += rh;
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
    if (img.isNull() && !b.imagePath.isEmpty()) {
        const QFileInfo fi(b.imagePath);
        if (fi.isFile())
            img.load(b.imagePath);
    }
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
        if (b.sectionHidden || blockHiddenByExpr(doc, b, 1, 1)) {
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
            m.height = h + 8 + (blockHasCaption(b) ? 16 * zoom : 0);
            meas.push_back(m);
            continue;
        }
        if (b.type == QLatin1String("shape") || b.type == QLatin1String("chart") || b.type == QLatin1String("qr")
            || b.type == QLatin1String("equation") || b.type == QLatin1String("form")) {
            const qreal wmm = b.imageWidthMm > 0 ? b.imageWidthMm : (b.type == QLatin1String("qr") ? 40.0 : 80.0);
            const qreal hmm = b.heightMm > 0 ? b.heightMm : (b.type == QLatin1String("equation") || b.type == QLatin1String("form") ? 12.0 : 40.0);
            m.height = mmToPx(hmm, zoom) + 8 + (blockHasCaption(b) ? 16 * zoom : 0);
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
        if (b.sectionHidden || blockHiddenByExpr(doc, b, 1, 1))
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

static bool blockUsesTextLayout(const QString &type)
{
    return type != QLatin1String("table") && type != QLatin1String("image") && type != QLatin1String("shape")
        && type != QLatin1String("chart") && type != QLatin1String("qr") && type != QLatin1String("equation")
        && type != QLatin1String("form") && type != QLatin1String("break");
}

static bool verticalWriting(const Document &doc, const Block &b)
{
    return b.writingMode == QLatin1String("vertical") || doc.page.writingMode == QLatin1String("vertical");
}

// paintPage rotates vertical blocks 90° clockwise around the block center, then
// restores before drawing the caret. Geometry tools speak in that visual space.
static QPointF verticalMap(QPointF p, QPointF center, bool inverse)
{
    const qreal dx = p.x() - center.x();
    const qreal dy = p.y() - center.y();
    if (!inverse)
        return QPointF(center.x() + dy, center.y() - dx);
    return QPointF(center.x() - dy, center.y() + dx);
}

static BlockTextInput laidTextInput(const DocumentLayout &lay, const LaidBlock &lb)
{
    BlockTextInput in;
    in.width = lb.rect.width() - lb.markerWidth;
    in.zoom = lay.zoom;
    in.fieldPage = lay.displayPage(lb.page);
    in.pages = qMax(1, lay.pages);
    in.physicalPage = lb.page;
    in.paint = true;
    in.docLayout = &lay;
    in.fragmentY0 = lb.layoutY0;
    in.fragmentHeight = lb.rect.height();
    return in;
}

// At a direction boundary one x belongs to two logical offsets and
// QTextLine::xToCursor picks either. Prefer the offset whose own caret
// (cursorToX) sits on the clicked x. D11. Cells stay on the linear path.
static int cursorOnLineAtX(const QTextLayout &layout, const QTextLine &line, qreal x)
{
    const int from = line.textStart();
    const int to = qMin(layout.text().size(), from + line.textLength());
    int chosen = qBound(from, line.xToCursor(x), to);
    const qreal chosenX = line.cursorToX(chosen);
    constexpr qreal kFarPx = 0.5;
    if (qAbs(chosenX - x) <= kFarPx)
        return chosen;
    int best = chosen;
    qreal bestDist = qAbs(chosenX - x);
    int p = from;
    while (p <= to) {
        if (layout.isValidCursorPosition(p)) {
            const qreal d = qAbs(line.cursorToX(p) - x);
            if (d < bestDist) {
                bestDist = d;
                best = p;
            }
        }
        if (p >= to)
            break;
        const int n = layout.nextCursorPosition(p, QTextLayout::SkipCharacters);
        if (n <= p)
            break;
        p = n > to ? to : n;
    }
    return best;
}

static bool glyphRtl(const QTextLayout &layout, int displayPos)
{
    const QString t = layout.text();
    if (t.isEmpty())
        return false;
    const int probe = qBound(0, displayPos > 0 ? displayPos - 1 : 0, t.size() - 1);
    const QList<QGlyphRun> runs = layout.glyphRuns(probe, 1);
    if (!runs.isEmpty())
        return runs.first().isRightToLeft();
    const auto dir = t.at(probe).direction();
    return dir == QChar::DirR || dir == QChar::DirAL || dir == QChar::DirRLE || dir == QChar::DirRLO;
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
            if (b && blockUsesTextLayout(lb.type) && lb.rect.width() > 0) {
                const BlockTextLayout btl = blockTextLayout(doc, *b, laidTextInput(*this, lb));
                const bool vert = verticalWriting(doc, *b);
                const QPointF layoutPt = vert ? verticalMap(posInPage, lb.rect.center(), true) : posInPage;
                const qreal originX = lb.rect.left() + lb.markerWidth;
                const qreal originY = lb.rect.top() - lb.layoutY0;
                const qreal localX = layoutPt.x() - originX;
                const qreal localY = layoutPt.y() - originY;
                int disp = 0;
                if (btl.layout && btl.layout->lineCount() > 0) {
                    int idx = btl.layout->lineCount() - 1;
                    for (int i = 0; i < btl.layout->lineCount(); ++i) {
                        const QTextLine line = btl.layout->lineAt(i);
                        const qreal top = line.y();
                        const qreal bot = top + line.height();
                        if (localY + 0.05 >= top && localY < bot - 0.01) {
                            idx = i;
                            break;
                        }
                        if (localY < top) {
                            idx = i;
                            break;
                        }
                    }
                    const QTextLine line = btl.layout->lineAt(idx);
                    disp = cursorOnLineAtX(*btl.layout, line, localX);
                }
                disp = qBound(0, disp, btl.display.size());
                h.offset = qBound(0, btl.toModel(disp), btl.modelLength);
            } else {
                const QString t = b ? b->plainText() : QString();
                const qreal x = posInPage.x() - lb.rect.left() - lb.markerWidth;
                if (!t.isEmpty() && lb.rect.width() > 0)
                    h.offset = qBound(0, int(t.size() * x / qMax(1.0, lb.rect.width() - lb.markerWidth)), t.size());
            }
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
    const Block *block = doc.findBlock(id);
    if (!block || !blockUsesTextLayout(block->type)) {
        for (const auto &lb : blocks) {
            if (lb.id != id)
                continue;
            c.page = lb.page;
            c.ok = true;
            if (block && lb.type == QLatin1String("table") && row >= 0) {
                for (const auto &g : lb.cells) {
                    if (g.row == row && g.col == col) {
                        QString t;
                        if (row < block->table.size() && col < block->table[row].size())
                            for (const auto &r : block->table[row][col].runs)
                                t += r.text;
                        const qreal frac = t.isEmpty() ? 0 : qreal(qBound(0, offset, t.size())) / qMax(1, t.size());
                        const qreal x = g.rect.left() + 4 + frac * (g.rect.width() - 8);
                        c.rect = QRectF(x, g.rect.top() + 2, 1.5 * zoom, g.rect.height() - 4);
                        return c;
                    }
                }
                continue;
            }
            const QString t = block ? block->plainText() : QString();
            const qreal frac = t.isEmpty() ? 0 : qreal(qBound(0, offset, t.size())) / qMax(1, t.size());
            const qreal x = lb.rect.left() + lb.markerWidth + frac * (lb.rect.width() - lb.markerWidth);
            c.rect = QRectF(x, lb.rect.top(), 1.5 * zoom, qMin(lb.rect.height(), 22.0 * zoom));
            return c;
        }
        return c;
    }

    const int modelOff = qBound(0, offset, block->plainText().size());
    const LaidBlock *chosen = nullptr;
    qreal cursorX = 0;
    qreal lineY = 0;
    qreal lineH = 0;
    int lineNo = 0;
    bool rtl = false;
    bool contained = false;
    qreal bestDist = 1e100;
    for (const auto &lb : blocks) {
        if (lb.id != id)
            continue;
        const BlockTextLayout btl = blockTextLayout(doc, *block, laidTextInput(*this, lb));
        const int disp = btl.toDisplay(qBound(0, modelOff, btl.modelLength));
        qreal x = 0;
        qreal y = 0;
        qreal h = QFontMetricsF(btl.font).height();
        int ln = 0;
        bool runRtl = false;
        const int layoutStart = btl.toModel(0);
        if (btl.dropLines > 0 && !btl.dropLetter.isEmpty() && modelOff < layoutStart) {
            x = 0;
            y = 0;
            h = qMax(btl.dropHeight, qreal(1));
        } else if (btl.layout && btl.layout->lineCount() > 0) {
            QTextLine line = btl.layout->lineForTextPosition(qBound(0, disp, btl.display.size()));
            if (!line.isValid())
                line = btl.layout->lineAt(0);
            x = line.cursorToX(qBound(0, disp, btl.display.size()));
            y = line.y();
            h = qMax(qreal(1), line.height());
            ln = line.lineNumber();
            runRtl = glyphRtl(*btl.layout, disp);
        }
        const qreal mid = y + h * 0.5;
        qreal dist = 0;
        if (mid < lb.layoutY0)
            dist = lb.layoutY0 - mid;
        else if (mid > lb.layoutY0 + lb.rect.height())
            dist = mid - (lb.layoutY0 + lb.rect.height());
        const bool inside = dist <= 0.75;
        if (!chosen || inside || (!contained && dist < bestDist)) {
            chosen = &lb;
            cursorX = x;
            lineY = y;
            lineH = h;
            lineNo = ln;
            rtl = runRtl;
            bestDist = dist;
            if (inside) {
                contained = true;
                break;
            }
        }
    }
    if (!chosen)
        return c;
    const bool vert = verticalWriting(doc, *block);
    const qreal originX = chosen->rect.left() + chosen->markerWidth;
    const qreal originY = chosen->rect.top() - chosen->layoutY0;
    QPointF topLeft(originX + cursorX, originY + lineY);
    if (vert)
        topLeft = verticalMap(topLeft, chosen->rect.center(), false);
    c.ok = true;
    c.page = chosen->page;
    c.line = lineNo;
    c.rtl = rtl;
    c.rect = QRectF(topLeft.x(), topLeft.y(), 1.5 * zoom, lineH);
    return c;
}

bool DocumentLayout::caretMove(const Document &doc, const QString &id, int offset, const QString &move,
                               int *outOffset) const
{
    if (outOffset)
        *outOffset = offset;
    const Block *block = doc.findBlock(id);
    if (!block || !blockUsesTextLayout(block->type))
        return false;

    const int modelLen = block->plainText().size();
    const int modelOff = qBound(0, offset, modelLen);
    const LaidBlock *chosenLb = nullptr;
    BlockTextLayout chosen;
    int disp = 0;
    bool contained = false;
    qreal bestDist = 1e100;
    for (const auto &lb : blocks) {
        if (lb.id != id)
            continue;
        BlockTextLayout btl = blockTextLayout(doc, *block, laidTextInput(*this, lb));
        const int d = btl.toDisplay(qBound(0, modelOff, btl.modelLength));
        qreal y = 0;
        qreal h = QFontMetricsF(btl.font).height();
        const int layoutStart = btl.toModel(0);
        if (!(btl.dropLines > 0 && !btl.dropLetter.isEmpty() && modelOff < layoutStart)
            && btl.layout && btl.layout->lineCount() > 0) {
            QTextLine line = btl.layout->lineForTextPosition(qBound(0, d, btl.display.size()));
            if (!line.isValid())
                line = btl.layout->lineAt(0);
            y = line.y();
            h = qMax(qreal(1), line.height());
        }
        const qreal mid = y + h * 0.5;
        qreal dist = 0;
        if (mid < lb.layoutY0)
            dist = lb.layoutY0 - mid;
        else if (mid > lb.layoutY0 + lb.rect.height())
            dist = mid - (lb.layoutY0 + lb.rect.height());
        const bool inside = dist <= 0.75;
        if (!chosenLb || inside || (!contained && dist < bestDist)) {
            chosenLb = &lb;
            chosen = std::move(btl);
            disp = d;
            bestDist = dist;
            if (inside) {
                contained = true;
                break;
            }
        }
    }
    if (!chosenLb || !chosen.layout)
        return false;

    if (move != QLatin1String("left") && move != QLatin1String("right") && move != QLatin1String("next")
        && move != QLatin1String("prev"))
        return false;

    const QTextLayout &layout = *chosen.layout;
    const int n = layout.text().size();
    auto step = [&](int pos) -> int {
        pos = qBound(0, pos, n);
        if (move == QLatin1String("left"))
            return layout.leftCursorPosition(pos);
        if (move == QLatin1String("right"))
            return layout.rightCursorPosition(pos);
        if (move == QLatin1String("next"))
            return layout.nextCursorPosition(pos, QTextLayout::SkipCharacters);
        return layout.previousCursorPosition(pos, QTextLayout::SkipCharacters);
    };

    int model = modelOff;
    const int limit = n + 2;
    for (int guard = 0; guard < limit; ++guard) {
        const int nextDisp = step(disp);
        if (nextDisp == disp)
            break;
        const int nextModel = qBound(0, chosen.toModel(nextDisp), modelLen);
        disp = nextDisp;
        if (nextModel != model) {
            model = nextModel;
            break;
        }
    }
    if (outOffset)
        *outOffset = model;
    return true;
}

// Z-order used by pdfTagMarkersForPage. std::sort isn't stable for equal zIndex; calling it on
// the same input from paint and from export stays deterministic within one run.
static QVector<LaidBlock> orderedBlocksForPage(const Document &doc, const DocumentLayout &lay, int page)
{
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
    return ordered;
}

static QString liveDisplayText(const Document &doc, const TextRun &r, int page, int pages,
                                const DocumentLayout &lay, const TextRun *fieldInstance = nullptr)
{
    if (r.field == QLatin1String("xref") && r.xrefKind == QLatin1String("page")) {
        const int physical = lay.pageOf(r.fieldArg);
        return formatPageNumber(lay.displayPage(physical), doc.page.numberFormat);
    }
    return runDisplayText(doc, r, page, pages, fieldInstance);
}

static QMap<QString, int> footnoteOrdinals(const Document &doc, const DocumentLayout &lay, int page)
{
    QMap<QString, int> pageNoteNo;
    if (!doc.page.noteRestart)
        return pageNoteNo;
    int n = 0;
    for (const auto &lb0 : lay.blocks) {
        if (lb0.page != page)
            continue;
        const Block *nb = doc.findBlock(lb0.id);
        if (nb && nb->type == QLatin1String("footnote"))
            pageNoteNo[lb0.id] = ++n;
    }
    return pageNoteNo;
}

static QMap<QString, QString> chapterLabels(const Document &doc)
{
    QMap<QString, QString> chapNo;
    if (!doc.page.chapterNumbers)
        return chapNo;
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
    return chapNo;
}

static QTextCharFormat runCharFormat(const Document &doc, const Block &b, const TextRun &r, double zoom)
{
    QTextCharFormat cf;
    cf.setFont(fontFor(doc, b, r, zoom));
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
    return cf;
}

int BlockTextLayout::toDisplay(int modelOffset) const
{
    if (modelToDisplay.isEmpty())
        return 0;
    const int m = qBound(0, modelOffset, qMax(0, modelLength));
    if (m < modelToDisplay.size())
        return modelToDisplay.at(m);
    return display.size();
}

int BlockTextLayout::toModel(int displayOffset) const
{
    if (displayToModel.isEmpty())
        return 0;
    int d = qBound(0, displayOffset, displayToModel.size() - 1);
    while (d < displayToModel.size() && displayToModel.at(d) < 0)
        ++d;
    if (d >= displayToModel.size())
        return qMax(0, modelLength);
    return displayToModel.at(d);
}

BlockTextLayout blockTextLayout(const Document &doc, const Block &b, const BlockTextInput &in)
{
    const QVector<TextRun> &runs = in.runs ? *in.runs : b.runs;
    BlockTextLayout out;
    const DropCapInfo dc = dropCapInfo(doc, b, runs, in.zoom);
    out.dropLetter = dc.letter;
    out.dropLines = dc.lines;
    out.dropWidth = dc.width;
    out.dropHeight = dc.height;
    out.dropFont = dc.font;

    struct Piece {
        QString text;
        QVector<int> model;
        bool hyphenate = false;
        QString lang;
        bool format = false;
        QTextCharFormat cf;
    };

    QVector<Piece> pieces;
    int model = 0;
    const bool live = in.paint && in.docLayout;
    QMap<QString, int> noteOrds;
    if (live && doc.page.noteRestart)
        noteOrds = footnoteOrdinals(doc, *in.docLayout, in.physicalPage);

    if (in.paint) {
        QString prefix;
        if (b.type == QLatin1String("heading")) {
            const QMap<QString, QString> chap = chapterLabels(doc);
            if (chap.contains(b.id))
                prefix += chap.value(b.id) + QStringLiteral("  ");
        }
        if (b.type == QLatin1String("footnote") && noteOrds.contains(b.id))
            prefix += QString::number(noteOrds.value(b.id)) + QStringLiteral("  ");
        if (!prefix.isEmpty()) {
            Piece pc;
            pc.text = prefix;
            pc.model = QVector<int>(prefix.size(), -1);
            pc.format = live;
            if (live)
                pc.cf = runCharFormat(doc, b, runs.isEmpty() ? TextRun{} : runs.first(), in.zoom);
            pieces.append(std::move(pc));
        }
    }

    for (const auto &r : runs) {
        QString disp = live ? liveDisplayText(doc, r, in.fieldPage, in.pages, *in.docLayout, &r)
                            : runDisplayText(doc, r, in.fieldPage, in.pages, &r);
        if (live && !r.noteId.isEmpty() && doc.page.noteRestart && noteOrds.contains(r.noteId))
            disp = QString::number(noteOrds.value(r.noteId));
        const int m0 = model;
        model += r.text.size();
        const int D = disp.size();
        const int M = qMax(0, model - m0);
        Piece pc;
        pc.text = disp;
        pc.model.reserve(D);
        for (int d = 0; d < D; ++d)
            pc.model.append((M == 0) ? m0 : m0 + int((qint64(M) * d) / D));
        pc.hyphenate = true;
        pc.lang = r.lang.isEmpty() ? b.lang : r.lang;
        pc.format = live;
        if (live)
            pc.cf = runCharFormat(doc, b, r, in.zoom);
        pieces.append(std::move(pc));
    }
    out.modelLength = model;

    int rawSize = 0;
    for (const Piece &pc : pieces)
        rawSize += pc.text.size();
    int skip = 0;
    if (!in.paint) {
        if (dc.lines > 0 && dc.skip > 0 && dc.skip <= rawSize)
            skip = dc.skip;
    } else if (dc.lines > 0) {
        skip = dc.skip;
    }
    if (skip > 0) {
        int left = qMin(skip, rawSize);
        for (Piece &pc : pieces) {
            if (left <= 0)
                break;
            const int n = qMin(left, pc.text.size());
            if (n <= 0)
                continue;
            pc.text = pc.text.mid(n);
            pc.model = pc.model.mid(n);
            left -= n;
        }
    }

    QString text;
    QVector<int> mapped;
    QList<QTextLayout::FormatRange> fmts;
    for (const Piece &pc : pieces) {
        if (pc.text.isEmpty())
            continue;
        QString shown = pc.text;
        QVector<int> pieceMap = pc.model;
        if (pc.hyphenate && doc.page.hyphenate) {
            shown = hyphenateDisplay(pc.text, doc.page.hyphenZoneMm, pc.lang);
            pieceMap.clear();
            pieceMap.reserve(shown.size());
            int si = 0;
            for (int i = 0; i < shown.size(); ++i) {
                const QChar ch = shown.at(i);
                // Inserted U+00AD is not in the model. Keep si on the next source
                // character so this hyphen and that character share its offset.
                const bool inserted = ch == QChar(0x00AD)
                    && (si >= pc.text.size() || pc.text.at(si) != QChar(0x00AD));
                const int m = (si < pc.model.size()) ? pc.model.at(si) : out.modelLength;
                pieceMap.append(m);
                if (!inserted)
                    ++si;
            }
        }
        if (pc.format && !shown.isEmpty()) {
            QTextLayout::FormatRange fr;
            fr.start = text.size();
            fr.length = shown.size();
            fr.format = pc.cf;
            fmts.append(fr);
        }
        text += shown;
        mapped += pieceMap;
    }
    if (!in.paint && text.isEmpty()) {
        text = QString(QLatin1Char(' '));
        mapped = QVector<int>{-1};
    }
    if (mapped.size() != text.size()) {
        QVector<int> fixed(text.size(), out.modelLength);
        const int n = qMin(mapped.size(), text.size());
        for (int i = 0; i < n; ++i)
            fixed[i] = mapped.at(i);
        mapped = fixed;
    }

    out.display = text;
    out.displayToModel = mapped;
    out.displayToModel.append(out.modelLength);
    out.modelToDisplay.resize(out.modelLength + 1);
    int scan = 0;
    const int nMap = out.displayToModel.size();
    for (int m = 0; m <= out.modelLength; ++m) {
        while (scan < nMap && out.displayToModel.at(scan) < m)
            ++scan;
        out.modelToDisplay[m] = (scan < nMap) ? scan : text.size();
    }

    TextRun probe = runs.isEmpty() ? TextRun{} : runs.first();
    QFont f = fontFor(doc, b, probe, in.zoom);
    if (in.paint && (b.lang.startsWith(QLatin1String("ar")) || probe.lang.startsWith(QLatin1String("ar"))))
        f.setLetterSpacing(QFont::PercentageSpacing, 112);
    out.font = f;

    auto layout = std::make_unique<QTextLayout>(text, f);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    opt.setTextDirection(blockOptionDirection(b));
    if (in.paint) {
        const StyleDef *st = styleNamed(doc, b.style);
        const QString align = b.align.isEmpty() && st ? st->align : b.align;
        opt.setAlignment(qtAlign(align, resolvedParagraphDirection(b, text)));
    }
    applyTabs(opt, b, in.zoom);
    layout->setTextOption(opt);
    if (!fmts.isEmpty())
        layout->setFormats(fmts);

    out.textHeight = layoutLines(*layout, in.width, dc.width, dc.lines);
    if (in.fragmentHeight >= 0 && layout->lineCount() > 0) {
        const qreal y0 = in.fragmentY0;
        const qreal y1 = y0 + in.fragmentHeight;
        for (int i = 0; i < layout->lineCount(); ++i) {
            const QTextLine line = layout->lineAt(i);
            if (line.y() + line.height() > y0 + 0.01 && line.y() < y1 - 0.01) {
                out.pageSkipDisplay = line.textStart();
                out.pageSkipModel = out.toModel(line.textStart());
                break;
            }
        }
    }
    out.layout = std::move(layout);
    return out;
}

// Glyphs QPdfWriter embeds are subset indexes in first-seen order per face
// (index 0 is .notdef). Shaped Arabic ligatures are absent from the cmap
// ToUnicode Qt writes, so export records the Unicode of each new glyph here
// and pdfPostProcessFixToUnicode fills only the missing CIDs.
struct PdfFaceCapture {
    QVector<quint32> rawGids;
    QHash<quint32, int> rawToCid;
    QVector<QString> unicode;
};

static bool g_pdfCapture = false;
static QHash<QString, PdfFaceCapture> g_pdfFaces;

static QString pdfFaceKeyFromRaw(const QRawFont &font)
{
    QString key;
    const QString family = font.familyName();
    for (const QChar c : family) {
        if (c.isLetterOrNumber())
            key += c.toLower();
    }
    if (key.isEmpty())
        return {};
    if (font.weight() >= QFont::Bold)
        key += QStringLiteral("bold");
    if (font.style() != QFont::StyleNormal)
        key += QStringLiteral("italic");
    return key;
}

static void pdfNoteGlyphRuns(const QList<QGlyphRun> &runs)
{
    if (!g_pdfCapture)
        return;
    for (const QGlyphRun &run : runs) {
        const QRawFont font = run.rawFont();
        if (!font.isValid())
            continue;
        const QString key = pdfFaceKeyFromRaw(font);
        if (key.isEmpty())
            continue;
        PdfFaceCapture &face = g_pdfFaces[key];
        if (face.rawGids.isEmpty()) {
            face.rawGids.push_back(0);
            face.rawToCid.insert(0, 0);
            face.unicode.push_back(QString());
        }
        const QList<quint32> gids = run.glyphIndexes();
        const QList<qsizetype> indexes = run.stringIndexes();
        const QString src = run.sourceString();
        QVector<int> starts;
        starts.reserve(indexes.size());
        for (qsizetype s : indexes) {
            if (s >= 0)
                starts.push_back(int(s));
        }
        std::sort(starts.begin(), starts.end());
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        QHash<int, QString> cluster;
        for (int i = 0; i < starts.size(); ++i) {
            const int a = starts[i];
            if (a > src.size())
                continue;
            int b = (i + 1 < starts.size()) ? starts[i + 1] : src.size();
            if (b < a)
                b = a;
            if (b > src.size())
                b = src.size();
            cluster.insert(a, src.mid(a, b - a));
        }
        for (int i = 0; i < gids.size(); ++i) {
            const quint32 gid = gids[i];
            int cid = face.rawToCid.value(gid, -1);
            if (cid < 0) {
                cid = face.rawGids.size();
                face.rawGids.push_back(gid);
                face.rawToCid.insert(gid, cid);
                face.unicode.push_back(QString());
            }
            if (cid <= 0 || i >= indexes.size())
                continue;
            const QString text = cluster.value(int(indexes[i]));
            if (text.size() > face.unicode[cid].size())
                face.unicode[cid] = text;
        }
    }
}

static void pdfNoteTextLine(const QTextLine &line)
{
    if (!g_pdfCapture || !line.isValid())
        return;
    const auto flags = QTextLayout::RetrieveGlyphIndexes | QTextLayout::RetrieveStringIndexes
        | QTextLayout::RetrieveString;
    pdfNoteGlyphRuns(line.glyphRuns(-1, -1, flags));
}

static void pdfNotePainterText(const QFont &font, const QString &text, qreal wrapWidth, QPaintDevice *device)
{
    if (!g_pdfCapture || text.isEmpty())
        return;
    QTextLayout layout(text, font, device);
    QTextOption opt;
    opt.setWrapMode(wrapWidth > 0 ? QTextOption::WordWrap : QTextOption::NoWrap);
    layout.setTextOption(opt);
    layout.beginLayout();
    const qreal width = wrapWidth > 0 ? wrapWidth : 1000000.0;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(width);
    }
    layout.endLayout();
    for (int i = 0; i < layout.lineCount(); ++i)
        pdfNoteTextLine(layout.lineAt(i));
}

static void pdfDrawText(QPainter *p, const QPointF &pos, const QString &text)
{
    pdfNotePainterText(p->font(), text, -1, p->device());
    QPainter *painter = p;
    painter->drawText(pos, text);
}

static void pdfDrawText(QPainter *p, const QRectF &rect, int flags, const QString &text)
{
    const qreal wrap = (flags & Qt::TextWordWrap) ? rect.width() : -1;
    pdfNotePainterText(p->font(), text, wrap, p->device());
    QPainter *painter = p;
    painter->drawText(rect, flags, text);
}

static void pdfDrawLine(QPainter *p, const QTextLine &line, const QPointF &origin)
{
    pdfNoteTextLine(line);
    line.draw(p, origin);
}

static void pdfCaptureBegin()
{
    g_pdfFaces.clear();
    g_pdfCapture = true;
}

static QVector<PdfSubsetUnicode> pdfCaptureEnd()
{
    g_pdfCapture = false;
    QVector<PdfSubsetUnicode> out;
    for (auto it = g_pdfFaces.constBegin(); it != g_pdfFaces.constEnd(); ++it) {
        const PdfFaceCapture &face = it.value();
        for (int cid = 1; cid < face.unicode.size(); ++cid) {
            if (face.unicode[cid].isEmpty())
                continue;
            PdfSubsetUnicode mapped;
            mapped.faceKey = it.key();
            mapped.cid = cid;
            mapped.unicode = face.unicode[cid];
            out.push_back(mapped);
        }
    }
    g_pdfFaces.clear();
    return out;
}

static bool lineInFragment(const QTextLine &line, const LaidBlock &lb);

static QFont rubyFont(const QFont &blockFont)
{
    QFont rf = blockFont;
    rf.setPointSizeF(qMax(6.0, blockFont.pointSizeF() * 0.45));
    return rf;
}

// Baseline point of one run's ruby, or false when that run has no ruby on this fragment.
static bool rubyDrawPoint(const BlockTextLayout &btl, const Block &b, const LaidBlock &lb, int runIndex,
                          const QFont &rf, QPointF *out)
{
    if (!btl.layout || runIndex < 0 || runIndex >= b.runs.size())
        return false;
    const TextRun &r = b.runs.at(runIndex);
    if (r.ruby.isEmpty())
        return false;
    int model = 0;
    for (int i = 0; i < runIndex; ++i)
        model += b.runs.at(i).text.size();
    const int m0 = model;
    const int m1 = model + r.text.size();
    const int d0 = btl.toDisplay(m0);
    const int d1 = btl.toDisplay(m1);
    QTextLine line = btl.layout->lineForTextPosition(qBound(0, d0, btl.display.size()));
    if (!line.isValid() || !lineInFragment(line, lb))
        return false;
    const int lineLast = line.textStart() + line.textLength();
    const int a = qBound(line.textStart(), d0, lineLast);
    const int c = qBound(line.textStart(), qMax(d0, d1), lineLast);
    const qreal x0 = line.cursorToX(a);
    const qreal x1 = line.cursorToX(c);
    const qreal left = qMin(x0, x1);
    const qreal span = qAbs(x1 - x0);
    const qreal rw = QFontMetricsF(rf).horizontalAdvance(r.ruby);
    const qreal x = left + (span - rw) / 2.0;
    const qreal originX = lb.rect.left() + lb.markerWidth;
    const qreal originY = lb.rect.top() - lb.layoutY0;
    if (out)
        *out = QPointF(originX + x, originY + line.y() - 1);
    return true;
}

// Ruby sits on the line that holds the run, centered between cursorToX of the
// run's display start and end (clamped to that line when the run wraps).
static void paintRuby(QPainter *p, const BlockTextLayout &btl, const Block &b, const LaidBlock &lb, const QColor &col)
{
    if (!btl.layout)
        return;
    const QFont rf = rubyFont(btl.font);
    bool prepared = false;
    for (int i = 0; i < b.runs.size(); ++i) {
        QPointF pos;
        if (!rubyDrawPoint(btl, b, lb, i, rf, &pos))
            continue;
        if (!prepared) {
            p->setFont(rf);
            p->setPen(col);
            prepared = true;
        }
        pdfDrawText(p, pos, b.runs.at(i).ruby);
    }
}

static QString documentLanguage(const Document &doc)
{
    for (const auto &b : doc.body) {
        if (!b.lang.trimmed().isEmpty())
            return b.lang.trimmed();
        for (const auto &r : b.runs) {
            if (!r.lang.trimmed().isEmpty())
                return r.lang.trimmed();
        }
        for (const auto &row : b.table)
            for (const auto &cell : row)
                for (const auto &r : cell.runs)
                    if (!r.lang.trimmed().isEmpty())
                        return r.lang.trimmed();
    }
    return QStringLiteral("pt-PT");
}

static bool foreignLang(const QString &lang, const QString &docLang)
{
    return !lang.isEmpty() && lang.compare(docLang, Qt::CaseInsensitive) != 0;
}

static PdfTagRole roleForBlock(const Block &b)
{
    if (b.type == QLatin1String("heading"))
        return PdfTagRole::Heading;
    if (b.type == QLatin1String("list-item"))
        return PdfTagRole::ListItem;
    if (b.type == QLatin1String("table"))
        return PdfTagRole::TableCell;
    // A text frame is a figure so reading_order is visible in the structure tree
    // (Figure before P). Its glyphs stay on the shaped text path below.
    if (b.type == QLatin1String("image") || b.type == QLatin1String("shape") || b.type == QLatin1String("chart")
        || b.type == QLatin1String("qr") || b.type == QLatin1String("equation") || b.type == QLatin1String("form")
        || b.type == QLatin1String("frame"))
        return PdfTagRole::Figure;
    if (b.type == QLatin1String("footnote") || b.type == QLatin1String("endnote"))
        return PdfTagRole::Note;
    if (b.type == QLatin1String("toc"))
        return PdfTagRole::TocItem;
    return PdfTagRole::Paragraph;
}

struct RunPiece {
    QString lang;
    int d0 = 0;
    int d1 = 0;
};

static QVector<RunPiece> foreignRunPieces(const Block &b, const BlockTextLayout &btl, const QString &docLang)
{
    QVector<RunPiece> pieces;
    bool any = false;
    int model = 0;
    for (const auto &r : b.runs) {
        const int m0 = model;
        model += r.text.size();
        QString lang = r.lang.isEmpty() ? b.lang : r.lang;
        if (!foreignLang(lang, docLang))
            lang.clear();
        else
            any = true;
        const int d0 = btl.toDisplay(m0);
        const int d1 = btl.toDisplay(model);
        if (d1 <= d0)
            continue;
        if (!pieces.isEmpty() && pieces.back().lang == lang && pieces.back().d1 == d0)
            pieces.back().d1 = d1;
        else
            pieces.push_back(RunPiece{lang, d0, d1});
    }
    if (!any)
        return {};
    return pieces;
}

static bool lineInFragment(const QTextLine &line, const LaidBlock &lb)
{
    const qreal y0 = lb.layoutY0;
    const qreal y1 = lb.layoutY0 + lb.rect.height();
    return line.y() + line.height() > y0 + 0.01 && line.y() < y1 - 0.01;
}

static QStringList pieceLangsFor(const QVector<RunPiece> &pieces, const BlockTextLayout &btl, const LaidBlock &lb)
{
    QStringList langs;
    if (pieces.isEmpty() || !btl.layout)
        return langs;
    for (int i = 0; i < btl.layout->lineCount(); ++i) {
        const QTextLine line = btl.layout->lineAt(i);
        if (!lineInFragment(line, lb))
            continue;
        const int ls = line.textStart();
        const int le = ls + line.textLength();
        for (const auto &pc : pieces) {
            if (qMax(pc.d0, ls) < qMin(pc.d1, le))
                langs.push_back(pc.lang);
        }
    }
    return langs;
}

struct LangSlice {
    int paintSlice = 1; // 1 glyph run, 2 drop cap, 3 ruby
    QString lang;
    int line = -1;
    int from = -1;
    int to = -1;
    int glyph = 0;
    int ruby = -1;
};

static QTextLayout::GlyphRunRetrievalFlags glyphRetrievalFlags()
{
    return QTextLayout::RetrieveGlyphIndexes | QTextLayout::RetrieveGlyphPositions
        | QTextLayout::RetrieveStringIndexes | QTextLayout::RetrieveString;
}

static QList<QGlyphRun> glyphRunsForRange(const QTextLine &line, int from, int len)
{
    QList<QGlyphRun> out;
    if (!line.isValid() || len <= 0)
        return out;
    const QList<QGlyphRun> all = line.glyphRuns(from, len, glyphRetrievalFlags());
    for (const QGlyphRun &gr : all) {
        if (!gr.glyphIndexes().isEmpty())
            out.push_back(gr);
    }
    return out;
}

static QString runLang(const Block &b, const TextRun &r)
{
    return (r.lang.isEmpty() ? b.lang : r.lang).trimmed();
}

// One clip per shaped slice when this fragment has a run in another language.
// Empty means the fragment is painted as a normal QTextLayout (no /Span split).
static QVector<LangSlice> foreignLangSlices(const Block &b, const BlockTextLayout &btl, const LaidBlock &lb,
                                            const QString &docLang)
{
    QVector<LangSlice> slices;
    const QVector<RunPiece> pieces = foreignRunPieces(b, btl, docLang);
    if (pieceLangsFor(pieces, btl, lb).isEmpty())
        return slices;

    if (btl.dropLines > 0 && !btl.dropLetter.isEmpty()) {
        LangSlice s;
        s.paintSlice = 2;
        if (!b.runs.isEmpty()) {
            const QString lang = runLang(b, b.runs.first());
            if (foreignLang(lang, docLang))
                s.lang = lang;
        }
        slices.push_back(s);
    }
    if (btl.layout) {
        for (int li = 0; li < btl.layout->lineCount(); ++li) {
            const QTextLine line = btl.layout->lineAt(li);
            if (!lineInFragment(line, lb))
                continue;
            const int ls = line.textStart();
            const int le = ls + line.textLength();
            for (const auto &pc : pieces) {
                const int a = qMax(pc.d0, ls);
                const int bend = qMin(pc.d1, le);
                if (a >= bend)
                    continue;
                const QList<QGlyphRun> runs = glyphRunsForRange(line, a, bend - a);
                for (int gi = 0; gi < runs.size(); ++gi) {
                    LangSlice s;
                    s.paintSlice = 1;
                    s.lang = pc.lang;
                    s.line = li;
                    s.from = a;
                    s.to = bend;
                    s.glyph = gi;
                    slices.push_back(s);
                }
            }
        }
    }
    const QFont rf = rubyFont(btl.font);
    for (int ri = 0; ri < b.runs.size(); ++ri) {
        if (!rubyDrawPoint(btl, b, lb, ri, rf, nullptr))
            continue;
        LangSlice s;
        s.paintSlice = 3;
        s.ruby = ri;
        const QString lang = runLang(b, b.runs.at(ri));
        if (foreignLang(lang, docLang))
            s.lang = lang;
        slices.push_back(s);
    }
    return slices;
}

static QRectF pageArtifactClip(const DocumentLayout &lay)
{
    return QRectF(-8, -8, lay.paperW + 16, lay.paperH + 16);
}

static const LaidBlock *laidFragment(const QVector<LaidBlock> &ordered, const PdfTagMarker &m)
{
    int seen = 0;
    for (const auto &lb : ordered) {
        if (lb.id != m.blockId)
            continue;
        if (seen == m.fragment)
            return &lb;
        ++seen;
    }
    return nullptr;
}

// One ordered list of setClipRect calls for this page. paintPage and the PDF
// post-processor both consume it, so the clip painted Nth is the marker tagged Nth.
static QVector<PdfTagMarker> pdfTagMarkersForPage(const Document &doc, const DocumentLayout &lay, int page)
{
    const QString docLang = documentLanguage(doc);
    QMap<QString, int> order;
    for (int i = 0; i < doc.body.size(); ++i)
        order.insert(doc.body[i].id, i);

    auto art = [&](int pass) {
        PdfTagMarker m;
        m.role = PdfTagRole::Artifact;
        m.artifactPass = pass;
        m.clip = pageArtifactClip(lay);
        return m;
    };

    QVector<PdfTagMarker> out;
    out.push_back(art(0));
    out.push_back(art(1));

    QMap<QString, int> frag;
    const QVector<LaidBlock> ordered = orderedBlocksForPage(doc, lay, page);
    for (const auto &lb : ordered) {
        const Block *bp = doc.findBlock(lb.id);
        if (!bp || bp->type == QLatin1String("break") || bp->sectionHidden || blockHiddenByExpr(doc, *bp, 1, 1))
            continue;
        const Block &b = *bp;
        const int fragment = frag.value(lb.id, 0);
        frag.insert(lb.id, fragment + 1);
        const PdfTagRole role = roleForBlock(b);
        const int docOrder = order.value(lb.id, 0);

        if (b.type == QLatin1String("table")) {
            for (const auto &g : lb.cells) {
                PdfTagMarker m;
                m.role = PdfTagRole::TableCell;
                m.blockId = lb.id;
                m.docOrder = docOrder;
                m.fragment = fragment;
                m.row = g.row;
                m.col = g.col;
                m.headerCell = b.headerRow && g.row == 0;
                if (g.row >= 0 && g.row < b.table.size() && g.col >= 0 && g.col < b.table[g.row].size()) {
                    const int colsLeft = qMax(1, b.table[g.row].size() - g.col);
                    const int rowsLeft = qMax(1, b.table.size() - g.row);
                    m.colSpan = qBound(1, b.table[g.row][g.col].colSpan, colsLeft);
                    m.rowSpan = qBound(1, b.table[g.row][g.col].rowSpan, rowsLeft);
                }
                m.clip = g.rect.adjusted(-3, -3, 3, 3);
                out.push_back(m);
            }
            continue;
        }
        // Frames keep the text-marker path (shaped runs, ruby, language spans).
        if (role == PdfTagRole::Figure && b.type != QLatin1String("frame")) {
            PdfTagMarker m;
            m.role = PdfTagRole::Figure;
            m.blockId = lb.id;
            m.docOrder = docOrder;
            m.fragment = fragment;
            m.alt = b.altText;
            const qreal sh = b.shadowPt > 0 ? b.shadowPt * lay.zoom : 0;
            m.clip = lb.rect.adjusted(-4, -4, sh + 6, sh + 6);
            out.push_back(m);
            continue;
        }
        if (b.type == QLatin1String("toc")) {
            const BlockTextLayout btl = blockTextLayout(doc, b, laidTextInput(lay, lb));
            int model = 0;
            for (int ei = 0; ei < b.runs.size(); ++ei) {
                const int m0 = model;
                model += b.runs[ei].text.size();
                if (!btl.layout)
                    continue;
                const int d0 = btl.toDisplay(m0);
                const int d1 = btl.toDisplay(model);
                if (d1 <= d0)
                    continue;
                QRectF rect;
                bool any = false;
                for (int li = 0; li < btl.layout->lineCount(); ++li) {
                    const QTextLine line = btl.layout->lineAt(li);
                    if (!lineInFragment(line, lb))
                        continue;
                    const int ls = line.textStart();
                    const int le = ls + line.textLength();
                    if (qMax(d0, ls) >= qMin(d1, le))
                        continue;
                    const QRectF lr(lb.rect.left() - 4, lb.rect.top() + (line.y() - lb.layoutY0) - 2, lb.rect.width() + 8,
                                    line.height() + 4);
                    rect = any ? rect.united(lr) : lr;
                    any = true;
                }
                if (!any)
                    continue;
                PdfTagMarker m;
                m.role = PdfTagRole::TocItem;
                m.blockId = lb.id;
                m.docOrder = docOrder;
                m.fragment = fragment;
                m.tocEntry = ei;
                m.textFrom = d0;
                m.textTo = d1;
                m.clip = rect;
                out.push_back(m);
            }
            continue;
        }

        PdfTagMarker m;
        m.role = role;
        m.blockId = lb.id;
        m.docOrder = docOrder;
        m.fragment = fragment;
        m.level = qBound(1, b.level > 0 ? b.level : 1, 6);
        m.listLabel = role == PdfTagRole::ListItem && !lb.marker.isEmpty();
        if (role == PdfTagRole::Figure)
            m.alt = b.altText.isEmpty() ? b.plainText().trimmed() : b.altText;
        qreal top = 2;
        for (const auto &r : b.runs) {
            if (!r.ruby.isEmpty()) {
                top = 16 * lay.zoom;
                break;
            }
        }
        m.clip = lb.rect.adjusted(-4, -top, 4, 4);
        const BlockTextLayout btl = blockTextLayout(doc, b, laidTextInput(lay, lb));
        const QVector<LangSlice> slices = foreignLangSlices(b, btl, lb, docLang);
        if (slices.isEmpty()) {
            out.push_back(m);
            continue;
        }
        // /Lang wraps each slice of the layout. The clip list, not a second
        // draw with the block font, is what marks the span.
        for (int si = 0; si < slices.size(); ++si) {
            PdfTagMarker sm = m;
            const LangSlice &s = slices.at(si);
            sm.paintSlice = s.paintSlice;
            sm.sliceLine = s.line;
            sm.textFrom = s.from;
            sm.textTo = s.to;
            sm.sliceGlyph = s.glyph;
            sm.sliceRuby = s.ruby;
            sm.paintChrome = si == 0;
            sm.listLabel = sm.paintChrome && role == PdfTagRole::ListItem && !lb.marker.isEmpty();
            if (!s.lang.isEmpty())
                sm.pieceLangs = QStringList{s.lang};
            out.push_back(sm);
        }
    }
    out.push_back(art(2));
    return out;
}

static void paintArtifactPass(QPainter *p, const Document &doc, const DocumentLayout &lay, int page, int pass,
                               const QVector<LaidBlock> &ordered, bool caretOn, const QString &selId, int caretOff,
                               int selRow, int selCol)
{
    if (pass == 0) {
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
        return;
    }

    if (pass == 1) {
        if (!doc.watermark.isEmpty()) {
            const qreal oldOp = p->opacity();
            const QFont oldF = p->font();
            const QPen oldPen = p->pen();
            p->setOpacity(0.12);
            QFont wf(QStringLiteral("IBM Plex Sans"));
            wf.setPointSizeF(42 * lay.zoom);
            wf.setBold(true);
            p->setFont(wf);
            p->setPen(QColor(QStringLiteral("#2A2420")));
            p->translate(lay.paperW / 2.0, lay.paperH / 2.0);
            p->rotate(-38);
            pdfDrawText(p, QRectF(-lay.paperW, -50 * lay.zoom, lay.paperW * 2, 100 * lay.zoom), Qt::AlignCenter,
                        doc.watermark);
            p->rotate(38);
            p->translate(-lay.paperW / 2.0, -lay.paperH / 2.0);
            p->setOpacity(oldOp);
            p->setFont(oldF);
            p->setPen(oldPen);
        }
        if (doc.page.borderPt > 0) {
            QPen bp(QColor(doc.page.borderColor.isEmpty() ? QStringLiteral("#B5651D") : doc.page.borderColor),
                    qMax(qreal(0.5), doc.page.borderPt * lay.zoom));
            p->setPen(bp);
            p->setBrush(Qt::NoBrush);
            p->drawRect(QRectF(lay.marginL - 4, lay.marginT - 4, lay.contentW + 8,
                               lay.paperH - lay.marginT - lay.marginB + 8));
        }

        const int shown = lay.displayPage(page);
        const HeaderFooter &hdr = headerForPage(doc, page, shown);
        const HeaderFooter &ftr = footerForPage(doc, page, shown);
        auto pageLabel = [&]() {
            return (hdr.pageNumbers || ftr.pageNumbers)
                ? QStringLiteral("Página %1 de %2").arg(formatPageNumber(shown, doc.page.numberFormat)).arg(lay.pages)
                : QString();
        };
        p->setPen(QColor(QStringLiteral("#6B6258")));
        QFont hf(QStringLiteral("IBM Plex Sans"));
        hf.setPointSizeF(9 * lay.zoom);
        p->setFont(hf);
        if (hdr.active()) {
            QString t = hdr.text;
            const QString chapter = chapterTitleOnPage(doc, lay, page);
            t.replace(QStringLiteral("{chapter}"), chapter);
            if (hdr.chapter && !chapter.isEmpty())
                t = t.isEmpty() ? chapter : t + QStringLiteral("  ·  ") + chapter;
            if (hdr.pageNumbers)
                t = t.isEmpty() ? pageLabel() : t + QStringLiteral("  ·  ") + pageLabel();
            pdfDrawText(p, QRectF(lay.marginL, 8 * lay.zoom, lay.contentW, 18 * lay.zoom), Qt::AlignLeft | Qt::AlignVCenter,
                        t);
            if (!hdr.imagePath.isEmpty()) {
                QImage himg;
                const QFileInfo hfi(hdr.imagePath);
                if (hfi.isFile())
                    himg = QImage(hdr.imagePath);
                if (!himg.isNull()) {
                    const QImage sc = himg.scaledToHeight(qMax(8, int(16 * lay.zoom)), Qt::SmoothTransformation);
                    p->drawImage(QPointF(lay.marginL + lay.contentW - sc.width(), 6 * lay.zoom), sc);
                }
            }
        }
        if (ftr.active()) {
            QString t = ftr.text;
            const QString chapter = chapterTitleOnPage(doc, lay, page);
            t.replace(QStringLiteral("{chapter}"), chapter);
            if (ftr.chapter && !chapter.isEmpty())
                t = t.isEmpty() ? chapter : t + QStringLiteral("  ·  ") + chapter;
            if (ftr.pageNumbers)
                t = t.isEmpty() ? pageLabel() : t + QStringLiteral("  ·  ") + pageLabel();
            pdfDrawText(p, QRectF(lay.marginL, lay.paperH - lay.marginB + 2, lay.contentW, 18 * lay.zoom),
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

        int lineNo = 0;
        const LaidBlock *firstNote = nullptr;
        for (const auto &lb : ordered) {
            const Block *bp = doc.findBlock(lb.id);
            if (!bp || bp->sectionHidden || blockHiddenByExpr(doc, *bp, 1, 1))
                continue;
            if (!firstNote && bp->type == QLatin1String("footnote"))
                firstNote = &lb;
            if (doc.page.lineNumbers
                && (bp->type == QLatin1String("paragraph") || bp->type == QLatin1String("heading")
                    || bp->type == QLatin1String("list-item"))) {
                ++lineNo;
                p->setPen(QColor(QStringLiteral("#A3988C")));
                QFont lf(QStringLiteral("IBM Plex Sans"));
                lf.setPointSizeF(7 * lay.zoom);
                p->setFont(lf);
                pdfDrawText(p, QRectF(4, lb.rect.top(), lay.marginL - 8, 14 * lay.zoom), Qt::AlignRight | Qt::AlignVCenter,
                            QString::number(lineNo));
            }
        }
        if (firstNote) {
            p->setPen(QPen(QColor(QStringLiteral("#C4B8A8")), 1));
            p->drawLine(QPointF(firstNote->rect.left(), firstNote->rect.top() - 4),
                        QPointF(firstNote->rect.left() + qMin(firstNote->rect.width(), qreal(80 * lay.zoom)),
                                firstNote->rect.top() - 4));
        }
        return;
    }

    for (const auto &lb : ordered) {
        const Block *bp = doc.findBlock(lb.id);
        if (!bp)
            continue;
        int openComments = 0;
        for (const auto &c : bp->comments) {
            if (!c.resolved)
                ++openComments;
        }
        if (openComments <= 0)
            continue;
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
        pdfDrawText(p, mark, Qt::AlignCenter, QString::number(openComments));
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
        if (cr.ok && cr.page == page)
            p->fillRect(cr.rect, QColor(QStringLiteral("#B5651D")));
    }
}

// One rectangle per line that intersects the model range. Offsets are model
// offsets; QTextLine positions are display offsets (blockTextLayout::toDisplay).
static void paintLineSelection(QPainter *p, const BlockTextLayout &btl, const QPointF &origin, int modelStart,
                               int modelEnd, const LaidBlock *fragment)
{
    if (!btl.layout || modelEnd <= modelStart)
        return;
    const int d0 = btl.toDisplay(modelStart);
    const int d1 = btl.toDisplay(modelEnd);
    if (d1 <= d0)
        return;
    const QColor sel(QStringLiteral("#B5651D40"));
    for (int li = 0; li < btl.layout->lineCount(); ++li) {
        const QTextLine line = btl.layout->lineAt(li);
        if (fragment && !lineInFragment(line, *fragment))
            continue;
        const int ls = line.textStart();
        const int le = ls + line.textLength();
        const int a = qMax(d0, ls);
        const int bend = qMin(d1, le);
        if (a >= bend)
            continue;
        qreal x0 = line.cursorToX(a);
        qreal x1 = line.cursorToX(bend);
        if (x1 < x0)
            std::swap(x0, x1);
        p->fillRect(QRectF(origin.x() + x0, origin.y() + line.y(), qMax(qreal(1), x1 - x0),
                           qMax(qreal(1), line.height())),
                    sel);
    }
}

static BlockTextLayout layoutCellText(const Document &doc, const Block &host, const DocumentLayout &lay,
                                       const LaidBlock &lb, const QVector<TextRun> &runs, qreal width)
{
    BlockTextInput in;
    in.width = qMax(qreal(8), width);
    in.zoom = lay.zoom;
    in.fieldPage = lay.displayPage(lb.page);
    in.pages = qMax(1, lay.pages);
    in.physicalPage = lb.page;
    in.paint = true;
    in.docLayout = &lay;
    in.runs = &runs;
    return blockTextLayout(doc, host, in);
}

static void paintCellText(QPainter *p, const BlockTextLayout &btl, const QPointF &origin, int selStart, int selEnd)
{
    if (selEnd > selStart)
        paintLineSelection(p, btl, origin, selStart, selEnd, nullptr);
    p->setPen(QColor(QStringLiteral("#1A1F24")));
    p->setFont(btl.font);
    if (!btl.layout)
        return;
    for (int li = 0; li < btl.layout->lineCount(); ++li)
        pdfDrawLine(p, btl.layout->lineAt(li), origin);
}

static void paintTableCellMarker(QPainter *p, const Document &doc, const DocumentLayout &lay, const LaidBlock &lb,
                                  const Block &b, const PdfTagMarker &m, const QString &selId, int selStart, int selEnd,
                                  int selRow, int selCol)
{
    const CellGeom *gp = nullptr;
    for (const auto &g : lb.cells) {
        if (g.row == m.row && g.col == m.col) {
            gp = &g;
            break;
        }
    }
    if (!gp)
        return;
    const CellGeom &g = *gp;
    const bool head = b.headerRow && g.row == 0;
    const TableCell *cellp = (g.row < b.table.size() && g.col < b.table[g.row].size()) ? &b.table[g.row][g.col] : nullptr;
    QColor fill = head ? QColor(QStringLiteral("#FBF0E6")) : QColor(QStringLiteral("#FDFBF7"));
    if (head) {
        if (const StyleDef *st = styleNamed(doc, b.style); st && !st->color.isEmpty() && st->kind == QLatin1String("table"))
            fill = QColor(st->color);
    }
    if (cellp && !cellp->background.isEmpty())
        fill = QColor(cellp->background);
    const bool thisCell = selId == lb.id && selRow == g.row && selCol == g.col;
    if (thisCell)
        fill = QColor(QStringLiteral("#F3E0CC"));
    QPen cellPen(QColor(QStringLiteral("#D2C8B8")), 1);
    if (cellp && cellp->borderPt > 0) {
        cellPen = QPen(QColor(cellp->borderColor.isEmpty() ? QStringLiteral("#B5651D") : cellp->borderColor),
                       qMax(qreal(0.5), cellp->borderPt * lay.zoom));
    }
    p->setPen(cellPen);
    p->setBrush(fill);
    p->drawRect(g.rect);
    const Block host = cellTextBlock(b);
    if (cellp && !cellp->nested.isEmpty()) {
        const NestedGeom grid = nestedGeom(doc, host, *cellp, g.rect.width(), lay.zoom);
        const qreal pad = cellPadPx(*cellp, lay.zoom);
        qreal iy = g.rect.top() + pad;
        const qreal ix0 = g.rect.left() + pad;
        for (int ri = 0; ri < cellp->nested.size(); ++ri) {
            const qreal rh = ri < grid.rowH.size() ? grid.rowH.at(ri) : 16 * lay.zoom;
            qreal ix = ix0;
            const auto &nrow = cellp->nested.at(ri);
            for (int ci = 0; ci < nrow.size(); ++ci) {
                const QRectF nr(ix, iy, grid.icw, rh);
                p->setBrush(QColor(QStringLiteral("#FDFBF7")));
                p->setPen(QPen(QColor(QStringLiteral("#D2C8B8")), 1));
                p->drawRect(nr);
                const QVector<TextRun> runs = runsWithCellDefaults(nrow.at(ci).runs);
                const BlockTextLayout btl = layoutCellText(doc, host, lay, lb, runs, grid.textW);
                paintCellText(p, btl, QPointF(nr.left() + 2, nr.top() + 2), -1, -1);
                ix += grid.icw;
            }
            iy += rh;
        }
        return;
    }
    const TableCell emptyCell;
    const TableCell &cell = cellp ? *cellp : emptyCell;
    const qreal pad = cellPadPx(cell, lay.zoom);
    const qreal textW = qMax(qreal(12), g.rect.width() - pad * 2);
    const QVector<TextRun> runs = runsWithCellDefaults(cell.runs);
    const BlockTextLayout btl = layoutCellText(doc, host, lay, lb, runs, textW);
    qreal y = g.rect.top() + pad;
    const qreal innerH = qMax(qreal(0), g.rect.height() - pad * 2);
    if (cell.vAlign == QLatin1String("bottom"))
        y += qMax(qreal(0), innerH - btl.textHeight);
    else if (cell.vAlign != QLatin1String("top"))
        y += qMax(qreal(0), (innerH - btl.textHeight) / 2);
    paintCellText(p, btl, QPointF(g.rect.left() + pad, y), thisCell ? selStart : -1, thisCell ? selEnd : -1);
}

static void paintFigureMarker(QPainter *p, const Document &doc, const DocumentLayout &lay, const LaidBlock &lb,
                               const Block &b)
{
    if (b.type == QLatin1String("image")) {
        QImage img = loadBlockImage(b);
        QRectF box = lb.rect.adjusted(0, 0, 0, -8);
        if (blockHasCaption(b))
            box.adjust(0, 0, 0, -16 * lay.zoom);
        if (!img.isNull()) {
            if (b.cropL > 0 || b.cropT > 0 || b.cropR > 0 || b.cropB > 0) {
                const QRect cr(int(img.width() * qBound(0.0, b.cropL, 0.9)), int(img.height() * qBound(0.0, b.cropT, 0.9)),
                               int(img.width() * qBound(0.1, 1.0 - b.cropL - b.cropR, 1.0)),
                               int(img.height() * qBound(0.1, 1.0 - b.cropT - b.cropB, 1.0)));
                img = img.copy(cr);
            }
            if (b.flipH)
                img = img.mirrored(true, false);
            if (b.flipV)
                img = img.mirrored(false, true);
            const qreal oldOp = p->opacity();
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
            if (b.rotateDeg != 0) {
                p->translate(box.center());
                p->rotate(-b.rotateDeg);
                p->translate(-box.center());
            }
            p->setOpacity(oldOp);
        } else {
            p->setPen(QPen(QColor(QStringLiteral("#C9C2B6")), 1, Qt::DashLine));
            p->setBrush(QColor(QStringLiteral("#F3EDE3")));
            p->drawRect(box);
            p->setPen(QColor(QStringLiteral("#6B6258")));
            pdfDrawText(p, box, Qt::AlignCenter, b.imagePath.isEmpty() ? QStringLiteral("imagem") : b.imagePath);
        }
        if (blockHasCaption(b)) {
            p->setPen(QColor(QStringLiteral("#6B6258")));
            QFont cf(QStringLiteral("IBM Plex Sans"));
            cf.setPointSizeF(8 * lay.zoom);
            p->setFont(cf);
            const QString cap = captionDisplayText(doc, b, lb.page, lay.pages);
            pdfDrawText(p, QRectF(lb.rect.left(), box.bottom(), lb.rect.width(), 16 * lay.zoom),
                        Qt::AlignHCenter | Qt::AlignVCenter, cap);
        }
        return;
    }

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
        pdfDrawText(p, box.adjusted(8, 0, -28, 0), Qt::AlignVCenter | Qt::AlignLeft, b.plainText());
        if (b.numbered)
            pdfDrawText(p, box.adjusted(0, 0, -8, 0), Qt::AlignVCenter | Qt::AlignRight,
                        QStringLiteral("(%1)").arg(b.level > 0 ? b.level : 1));
    } else {
        p->setPen(QPen(QColor(QStringLiteral("#B5651D")), 1));
        p->setBrush(QColor(QStringLiteral("#FDFBF7")));
        p->drawRect(box);
        QString label = b.plainText();
        if (b.formKind == QLatin1String("checkbox"))
            label = (b.checked ? QStringLiteral("☑ ") : QStringLiteral("☐ ")) + label;
        p->setPen(QColor(QStringLiteral("#1A1F24")));
        pdfDrawText(p, box.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
    }
}

static QTextCharFormat formatAtDisplay(const QTextLayout *layout, int pos)
{
    if (!layout)
        return {};
    const auto fmts = layout->formats();
    for (const auto &fr : fmts) {
        if (pos >= fr.start && pos < fr.start + fr.length)
            return fr.format;
    }
    return {};
}

static qreal formatBaselineShift(const QTextCharFormat &format, const QRawFont &font)
{
    const auto valign = format.verticalAlignment();
    const bool super = valign == QTextCharFormat::AlignSuperScript;
    const bool sub = valign == QTextCharFormat::AlignSubScript;
    const qreal baseLineOffset = format.baselineOffset() / 100.0;
    if (!super && !sub && qAbs(baseLineOffset) < 0.0001)
        return 0;
    const qreal height = font.ascent() + font.descent();
    qreal delta = -height * baseLineOffset;
    if (sub)
        delta += height * (format.subScriptBaseline() / 100.0);
    else if (super)
        delta -= height * (format.superScriptBaseline() / 100.0);
    return delta;
}

static bool formatUnderline(const QTextCharFormat &format)
{
    return format.fontUnderline() || format.font().underline();
}

static bool formatStrike(const QTextCharFormat &format)
{
    return format.fontStrikeOut() || format.font().strikeOut();
}

static void paintSpanDecorations(QPainter *p, const QTextLine &line, int from, int to, const QPointF &origin,
                                 const QTextCharFormat &format)
{
    if (from >= to || (!formatUnderline(format) && !formatStrike(format)))
        return;
    qreal x0 = line.cursorToX(from);
    qreal x1 = line.cursorToX(to);
    if (x1 < x0)
        std::swap(x0, x1);
    if (qAbs(x1 - x0) < 0.5)
        return;
    const qreal thick = qMax(qreal(1), format.font().pointSizeF() / 18.0);
    const qreal baseline = origin.y() + line.y() + line.ascent();
    QPen pen = p->pen();
    pen.setWidthF(thick);
    pen.setCapStyle(Qt::FlatCap);
    p->setPen(pen);
    const qreal left = origin.x() + x0;
    const qreal right = origin.x() + x1;
    if (formatStrike(format)) {
        const qreal y = baseline - qMax(line.ascent() * 0.32, qreal(1));
        p->drawLine(QPointF(left, y), QPointF(right, y));
    }
    if (formatUnderline(format)) {
        const qreal y = baseline + thick + 1;
        p->drawLine(QPointF(left, y), QPointF(right, y));
    }
}

static void paintShapedGlyph(QPainter *p, const QGlyphRun &gr, const QPointF &origin, const QTextCharFormat &format,
                             const QColor &fallback)
{
    const QBrush fg = format.foreground();
    p->setPen(fg.style() == Qt::NoBrush ? QPen(fallback) : QPen(fg, 0));
    const qreal dy = gr.rawFont().isValid() ? formatBaselineShift(format, gr.rawFont()) : 0;
    pdfNoteGlyphRuns(QList<QGlyphRun>{gr});
    p->drawGlyphRun(origin + QPointF(0, dy), gr);
}

// Glyph runs of [from, to) on one line. Font comes from the run; color, highlight,
// underline, and strike come from the format range, not the block's first run.
static void paintDisplayRange(QPainter *p, const BlockTextLayout &btl, const QTextLine &line, int from, int to,
                              const QPointF &origin, const QColor &fallback)
{
    if (!btl.layout || from >= to)
        return;
    const auto fmts = btl.layout->formats();
    for (const auto &fr : fmts) {
        const int a = qMax(from, fr.start);
        const int b = qMin(to, fr.start + fr.length);
        if (a >= b)
            continue;
        const QBrush bg = fr.format.background();
        if (bg.style() == Qt::NoBrush)
            continue;
        qreal x0 = line.cursorToX(a);
        qreal x1 = line.cursorToX(b);
        if (x1 < x0)
            std::swap(x0, x1);
        p->fillRect(QRectF(origin.x() + x0, origin.y() + line.y(), qMax(qreal(1), x1 - x0),
                           qMax(qreal(1), line.height())),
                    bg);
    }
    const QList<QGlyphRun> runs = glyphRunsForRange(line, from, to - from);
    for (const QGlyphRun &gr : runs) {
        int pos = from;
        const QList<qsizetype> idxs = gr.stringIndexes();
        if (!idxs.isEmpty() && idxs.first() >= 0)
            pos = int(idxs.first());
        paintShapedGlyph(p, gr, origin, formatAtDisplay(btl.layout.get(), pos), fallback);
    }
    for (const auto &fr : fmts) {
        const int a = qMax(from, fr.start);
        const int b = qMin(to, fr.start + fr.length);
        if (a >= b)
            continue;
        const QBrush fg = fr.format.foreground();
        p->setPen(fg.style() == Qt::NoBrush ? QPen(fallback) : QPen(fg, 0));
        paintSpanDecorations(p, line, a, b, origin, fr.format);
    }
}

// Draw one glyph run QTextLayout already shaped, with that run's font and color.
static void paintGlyphSlice(QPainter *p, const BlockTextLayout &btl, const QPointF &origin, const PdfTagMarker &m,
                            const QColor &fallback)
{
    if (!btl.layout || m.sliceLine < 0 || m.sliceLine >= btl.layout->lineCount())
        return;
    const QTextLine line = btl.layout->lineAt(m.sliceLine);
    const QList<QGlyphRun> runs = glyphRunsForRange(line, m.textFrom, m.textTo - m.textFrom);
    if (m.sliceGlyph < 0 || m.sliceGlyph >= runs.size())
        return;
    const QGlyphRun &gr = runs.at(m.sliceGlyph);
    int pos = m.textFrom;
    int end = m.textFrom + 1;
    const QList<qsizetype> idxs = gr.stringIndexes();
    if (!idxs.isEmpty()) {
        int lo = -1;
        int hi = -1;
        for (qsizetype ix : idxs) {
            if (ix < 0)
                continue;
            if (lo < 0 || int(ix) < lo)
                lo = int(ix);
            if (int(ix) > hi)
                hi = int(ix);
        }
        if (lo >= 0) {
            pos = lo;
            end = hi + 1;
        }
    }
    const QTextCharFormat format = formatAtDisplay(btl.layout.get(), pos);
    const QBrush bg = format.background();
    if (bg.style() != Qt::NoBrush) {
        QRectF br = gr.boundingRect();
        br.translate(origin);
        p->fillRect(br, bg);
    }
    paintShapedGlyph(p, gr, origin, format, fallback);
    const QBrush fg = format.foreground();
    p->setPen(fg.style() == Qt::NoBrush ? QPen(fallback) : QPen(fg, 0));
    paintSpanDecorations(p, line, pos, end, origin, format);
}

static void paintTextMarker(QPainter *p, const Document &doc, const DocumentLayout &lay, const LaidBlock &lb,
                             const Block &b, const PdfTagMarker &m, const QString &docLang, const QString &selId,
                             int selStart, int selEnd)
{
    Q_UNUSED(docLang);
    const qreal borderPt = b.borderPt > 0 ? b.borderPt : (b.type == QLatin1String("frame") ? 0.75 : 0);
    const BlockTextLayout btl = blockTextLayout(doc, b, laidTextInput(lay, lb));
    const QFont &f = btl.font;
    TextRun probe = b.runs.isEmpty() ? TextRun{} : b.runs.first();
    p->setFont(f);
    QColor col(QStringLiteral("#1A1F24"));
    if (doc.page.tone == QLatin1String("night"))
        col = QColor(QStringLiteral("#FBF7F0"));
    if (const StyleDef *st = styleNamed(doc, b.style); st && !st->color.isEmpty())
        col = QColor(st->color);
    if (!probe.color.isEmpty())
        col = QColor(probe.color);

    const bool whole = m.paintSlice == 0;
    const bool chrome = whole || m.paintChrome;
    if (chrome && !lb.marker.isEmpty()) {
        p->setPen(col);
        pdfDrawText(p, QPointF(lb.rect.left(), lb.rect.top() + QFontMetricsF(f).ascent()), lb.marker);
    }
    if (chrome && borderPt > 0) {
        QPen bp(QColor(b.borderColor.isEmpty() ? QStringLiteral("#B5651D") : b.borderColor),
                qMax(qreal(0.5), borderPt * lay.zoom));
        p->setPen(bp);
        p->setBrush(Qt::NoBrush);
        p->drawRect(lb.rect.adjusted(-2, -1, 2, 1));
    }
    const QPointF origin(lb.rect.left() + lb.markerWidth, lb.rect.top() - lb.layoutY0);
    // tocEntry >= 0 is one marker per TOC run. Only the first paints the selection
    // so overlapping clips do not stack the selection color.
    if (chrome && m.tocEntry <= 0 && selId == lb.id && selEnd > selStart)
        paintLineSelection(p, btl, origin, selStart, selEnd, &lb);
    if (!whole) {
        if (m.paintSlice == 2 && btl.dropLines > 0 && !btl.dropLetter.isEmpty()) {
            p->setFont(btl.dropFont);
            p->setPen(col);
            pdfDrawText(p, QPointF(lb.rect.left() + lb.markerWidth, lb.rect.top() + QFontMetricsF(btl.dropFont).ascent()),
                        btl.dropLetter);
        } else if (m.paintSlice == 3 && m.sliceRuby >= 0 && m.sliceRuby < b.runs.size()) {
            const QFont rf = rubyFont(btl.font);
            QPointF pos;
            if (rubyDrawPoint(btl, b, lb, m.sliceRuby, rf, &pos)) {
                p->setFont(rf);
                p->setPen(col);
                pdfDrawText(p, pos, b.runs.at(m.sliceRuby).ruby);
            }
        } else if (m.paintSlice == 1) {
            paintGlyphSlice(p, btl, origin, m, col);
        }
        return;
    }

    p->setPen(col);
    p->setFont(f);
    if (btl.dropLines > 0 && !btl.dropLetter.isEmpty()) {
        p->setFont(btl.dropFont);
        p->setPen(col);
        pdfDrawText(p, QPointF(lb.rect.left() + lb.markerWidth, lb.rect.top() + QFontMetricsF(btl.dropFont).ascent()),
                    btl.dropLetter);
        p->setFont(f);
    }
    if (btl.layout) {
        for (int li = 0; li < btl.layout->lineCount(); ++li) {
            const QTextLine line = btl.layout->lineAt(li);
            if (!lineInFragment(line, lb))
                continue;
            if (m.textFrom >= 0) {
                const int ls = line.textStart();
                const int le = ls + line.textLength();
                const int limit = m.textTo < 0 ? le : m.textTo;
                const int a = qMax(m.textFrom, ls);
                const int bend = qMin(limit, le);
                if (a >= bend)
                    continue;
                paintDisplayRange(p, btl, line, a, bend, origin, col);
                continue;
            }
            pdfDrawLine(p, line, origin);
        }
    }
    paintRuby(p, btl, b, lb, col);
}

void paintPage(QPainter *p, const Document &doc, const DocumentLayout &lay, int page, const QString &selId,
               int selStart, int selEnd, int selRow, int selCol, bool caretOn, int caretOff)
{
    const QString docLang = documentLanguage(doc);
    const QVector<PdfTagMarker> markers = pdfTagMarkersForPage(doc, lay, page);
    const QVector<LaidBlock> ordered = orderedBlocksForPage(doc, lay, page);
    bool gutterDone = false;
    for (const auto &m : markers) {
        if (!gutterDone && m.artifactPass != 0) {
            gutterDone = true;
            const int shownEarly = lay.displayPage(page);
            if (doc.page.gutterMirror && shownEarly % 2 == 0 && doc.page.gutterMm > 0)
                p->translate(-mmToPx(doc.page.gutterMm, lay.zoom), 0);
        }
        const Block *bp = m.blockId.isEmpty() ? nullptr : doc.findBlock(m.blockId);
        const LaidBlock *lb = bp ? laidFragment(ordered, m) : nullptr;
        const bool vert = bp && lb && verticalWriting(doc, *bp)
            && (m.role == PdfTagRole::Paragraph || m.role == PdfTagRole::Heading || m.role == PdfTagRole::ListItem
                || m.role == PdfTagRole::Note || m.role == PdfTagRole::TocItem
                || bp->type == QLatin1String("frame"));
        p->save();
        if (vert) {
            p->translate(lb->rect.center());
            p->rotate(90);
            p->translate(-lb->rect.center());
        }
        p->setClipRect(m.clip);
        if (m.role == PdfTagRole::Artifact) {
            paintArtifactPass(p, doc, lay, page, m.artifactPass, ordered, caretOn, selId, caretOff, selRow, selCol);
        } else if (m.role == PdfTagRole::TableCell && lb && bp) {
            paintTableCellMarker(p, doc, lay, *lb, *bp, m, selId, selStart, selEnd, selRow, selCol);
        } else if (m.role == PdfTagRole::Figure && lb && bp && bp->type != QLatin1String("frame")) {
            paintFigureMarker(p, doc, lay, *lb, *bp);
        } else if (lb && bp) {
            paintTextMarker(p, doc, lay, *lb, *bp, m, docLang, selId, selStart, selEnd);
        }
        p->restore();
    }
}

PreviewResult renderPage(const Document &doc, int page, double zoom, const QString &selId, int selStart, int selEnd,
                         int selRow, int selCol)
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
    paintPage(&p, doc, lay, page, selId, selStart, selEnd, selRow, selCol);
    p.end();
    return res;
}

QByteArray renderPagePng(const Document &doc, int page, double zoom, const QString &selId, int selStart, int selEnd,
                         int selRow, int selCol)
{
    const auto r = renderPage(doc, page, zoom, selId, selStart, selEnd, selRow, selCol);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    r.image.save(&buf, "PNG");
    return bytes;
}

QByteArray renderBlockPng(const Document &doc, const Block &block)
{
    // The EPUB figure also writes <figcaption>. Painting the caption into the
    // PNG would show it twice. Page preview still uses the original block.
    Block painted = block;
    painted.caption.clear();
    painted.captionRuns.clear();
    const qreal zoom = 2.0;
    qreal wmm = block.imageWidthMm > 0 ? block.imageWidthMm : (block.type == QLatin1String("qr") ? 40.0 : 80.0);
    qreal hmm = block.heightMm > 0 ? block.heightMm : (block.type == QLatin1String("qr") ? wmm : 40.0);
    if (block.type == QLatin1String("qr"))
        hmm = wmm;
    if (block.type == QLatin1String("image")) {
        wmm = block.imageWidthMm > 0 ? block.imageWidthMm : 80.0;
        const QImage img = loadBlockImage(block);
        if (!img.isNull() && img.width() > 0 && img.height() > 0) {
            qreal iw = img.width();
            qreal ih = img.height();
            if (block.cropL > 0 || block.cropT > 0 || block.cropR > 0 || block.cropB > 0) {
                iw *= qBound(0.1, 1.0 - block.cropL - block.cropR, 1.0);
                ih *= qBound(0.1, 1.0 - block.cropT - block.cropB, 1.0);
            }
            const int rot = ((block.rotateDeg % 360) + 360) % 360;
            if (rot == 90 || rot == 270)
                std::swap(iw, ih);
            hmm = wmm * ih / qMax(qreal(1), iw);
        } else {
            hmm = wmm;
        }
    }
    const int innerW = qMax(8, int(qCeil(mmToPx(wmm, zoom))));
    const int innerH = qMax(8, int(qCeil(mmToPx(hmm, zoom))));
    const qreal shadow = block.shadowPt > 0 ? block.shadowPt * zoom : 0;
    const int pad = int(qCeil(qMax(qreal(6), shadow + 6)));
    QImage image(innerW + pad * 2, innerH + pad * 2, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(QStringLiteral("#FDFBF7")));
    DocumentLayout lay;
    lay.zoom = zoom;
    lay.pages = 1;
    lay.paperW = image.width();
    lay.paperH = image.height();
    lay.contentW = innerW;
    lay.contentH = innerH;
    LaidBlock lb;
    lb.id = block.id;
    lb.type = block.type;
    lb.page = 1;
    lb.rect = QRectF(pad, pad, innerW, innerH);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    paintFigureMarker(&painter, doc, lay, lb, painted);
    painter.end();
    QByteArray bytes;
    QBuffer buf(&bytes);
    if (!buf.open(QIODevice::WriteOnly) || !image.save(&buf, "PNG"))
        return {};
    return bytes;
}

static QString xmlText(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if (u < 0x20 && u != 0x9 && u != 0xA)
            continue;
        if (c == QLatin1Char('&'))
            out += QStringLiteral("&amp;");
        else if (c == QLatin1Char('<'))
            out += QStringLiteral("&lt;");
        else if (c == QLatin1Char('>'))
            out += QStringLiteral("&gt;");
        else
            out += c;
    }
    return out;
}

static QString xmpStamp(const QDateTime &dt)
{
    const QDateTime local = dt.toLocalTime();
    const int off = local.offsetFromUtc();
    const int a = qAbs(off);
    const QChar sign = off >= 0 ? QLatin1Char('+') : QLatin1Char('-');
    return local.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")) + sign
        + QStringLiteral("%1:%2").arg(a / 3600, 2, 10, QLatin1Char('0')).arg((a % 3600) / 60, 2, 10, QLatin1Char('0'));
}

static QString pdfStamp(const QDateTime &dt)
{
    const QDateTime local = dt.toLocalTime();
    const int off = local.offsetFromUtc();
    const int a = qAbs(off);
    const QChar sign = off >= 0 ? QLatin1Char('+') : QLatin1Char('-');
    return QStringLiteral("D:") + local.toString(QStringLiteral("yyyyMMddHHmmss")) + sign
        + QStringLiteral("%1'%2'").arg(a / 3600, 2, 10, QLatin1Char('0')).arg((a % 3600) / 60, 2, 10, QLatin1Char('0'));
}

static QDateTime pdfMetaInstant(const QString &raw, const QDateTime &fallback)
{
    const QString t = raw.trimmed();
    if (t.isEmpty())
        return fallback;
    QDateTime dt = QDateTime::fromString(t, Qt::ISODate);
    if (!dt.isValid())
        dt = QDateTime::fromString(t, Qt::ISODateWithMs);
    return dt.isValid() ? dt : fallback;
}

static PdfDocumentInfoMeta pdfInfoMeta(const Document &doc)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime created = pdfMetaInstant(doc.vars.value(QStringLiteral("doc.created")), now);
    const QString modifiedRaw = doc.vars.value(QStringLiteral("doc.modified")).trimmed();
    const QDateTime modified = modifiedRaw.isEmpty() ? created : pdfMetaInstant(modifiedRaw, created);
    PdfDocumentInfoMeta m;
    m.title = doc.title;
    m.author = doc.author;
    m.subject = doc.subject;
    m.keywords = doc.keywords;
    m.pdfCreationDate = pdfStamp(created);
    m.pdfModDate = pdfStamp(modified);
    m.xmpCreationDate = xmpStamp(created);
    m.xmpModDate = xmpStamp(modified);
    return m;
}

// Hand-written XMP. QXmlStreamWriter renames namespaces to n1/n4, which veraPDF rejects
// (pdfaid/pdfuaid/pdfaSchema prefixes are part of the rule). Value types are the PDF/A RDF
// forms: dc:title and dc:description are language alternatives, dc:creator is a sequence.
// dc:language is omitted; catalog /Lang carries the document language.
static QByteArray buildPdfDocumentXmp(bool pdfa, bool ua, const PdfDocumentInfoMeta &meta)
{
    QByteArray out;
    out.append("<?xpacket begin=\"");
    out.append(char(0xEF));
    out.append(char(0xBB));
    out.append(char(0xBF));
    out.append("\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n");
    out.append("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n");
    out.append("<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n");
    out.append("<rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
               "xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\" xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"");
    if (pdfa)
        out.append(" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\"");
    if (ua)
        out.append(" xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\"");
    out.append(">\n");
    out.append("<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">");
    out.append(xmlText(meta.title).toUtf8());
    out.append("</rdf:li></rdf:Alt></dc:title>\n");
    if (!meta.author.isEmpty()) {
        out.append("<dc:creator><rdf:Seq><rdf:li>");
        out.append(xmlText(meta.author).toUtf8());
        out.append("</rdf:li></rdf:Seq></dc:creator>\n");
    }
    if (!meta.subject.isEmpty()) {
        out.append("<dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">");
        out.append(xmlText(meta.subject).toUtf8());
        out.append("</rdf:li></rdf:Alt></dc:description>\n");
    }
    out.append("<pdf:Producer>AI Office</pdf:Producer>\n");
    if (!meta.keywords.isEmpty()) {
        out.append("<pdf:Keywords>");
        out.append(xmlText(meta.keywords).toUtf8());
        out.append("</pdf:Keywords>\n");
    }
    out.append("<xmp:CreatorTool>AI Office</xmp:CreatorTool>\n<xmp:CreateDate>");
    out.append(meta.xmpCreationDate.toUtf8());
    out.append("</xmp:CreateDate>\n<xmp:ModifyDate>");
    out.append(meta.xmpModDate.toUtf8());
    out.append("</xmp:ModifyDate>\n");
    if (pdfa)
        out.append("<pdfaid:part>1</pdfaid:part>\n<pdfaid:conformance>B</pdfaid:conformance>\n");
    if (ua)
        out.append("<pdfuaid:part>1</pdfuaid:part>\n");
    out.append("</rdf:Description>\n");
    if (ua && pdfa) {
        out.append("<rdf:Description rdf:about=\"\" "
                   "xmlns:pdfaExtension=\"http://www.aiim.org/pdfa/ns/extension/\" "
                   "xmlns:pdfaSchema=\"http://www.aiim.org/pdfa/ns/schema#\" "
                   "xmlns:pdfaProperty=\"http://www.aiim.org/pdfa/ns/property#\">\n"
                   "<pdfaExtension:schemas><rdf:Bag><rdf:li rdf:parseType=\"Resource\">\n"
                   "<pdfaSchema:schema>PDF/UA identification schema</pdfaSchema:schema>\n"
                   "<pdfaSchema:namespaceURI>http://www.aiim.org/pdfua/ns/id/</pdfaSchema:namespaceURI>\n"
                   "<pdfaSchema:prefix>pdfuaid</pdfaSchema:prefix>\n"
                   "<pdfaSchema:property><rdf:Seq><rdf:li rdf:parseType=\"Resource\">\n"
                   "<pdfaProperty:name>part</pdfaProperty:name>\n"
                   "<pdfaProperty:valueType>Integer</pdfaProperty:valueType>\n"
                   "<pdfaProperty:category>internal</pdfaProperty:category>\n"
                   "<pdfaProperty:description>Part of PDF/UA standard</pdfaProperty:description>\n"
                   "</rdf:li></rdf:Seq></pdfaSchema:property>\n"
                   "</rdf:li></rdf:Bag></pdfaExtension:schemas>\n"
                   "</rdf:Description>\n");
    }
    out.append("</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>\n");
    return out;
}

static Document documentForPdfaPaint(const Document &doc, QStringList *warnings, bool *paintOpaqueWatermark)
{
    Document d = doc;
    *paintOpaqueWatermark = false;
    if (!doc.watermark.isEmpty()) {
        if (warnings)
            warnings->append(QStringLiteral("PDF/A: flattened translucent watermark to opaque"));
        d.watermark.clear();
        *paintOpaqueWatermark = true;
    }
    for (auto &b : d.body) {
        if (b.type == QLatin1String("image") && b.opacity < 0.999) {
            if (warnings)
                warnings->append(QStringLiteral("PDF/A: flattened image %1 opacity to opaque").arg(b.id));
            b.opacity = 1.0;
        }
        if (b.shadowPt > 0.0) {
            if (warnings)
                warnings->append(QStringLiteral("PDF/A: removed drop shadow on %1").arg(b.id));
            b.shadowPt = 0.0;
        }
    }
    return d;
}

static void paintOpaqueWatermarkOverlay(QPainter *p, const DocumentLayout &lay, const QString &text)
{
    if (text.isEmpty())
        return;
    p->save();
    QFont wf(QStringLiteral("IBM Plex Sans"));
    wf.setPointSizeF(42 * lay.zoom);
    wf.setBold(true);
    p->setFont(wf);
    p->setPen(QColor(QStringLiteral("#2A2420")));
    p->translate(lay.paperW / 2.0, lay.paperH / 2.0);
    p->rotate(-38);
    pdfDrawText(p, QRectF(-lay.paperW, -50 * lay.zoom, lay.paperW * 2, 100 * lay.zoom), Qt::AlignCenter, text);
    p->restore();
}

bool exportNativePdf(const Document &doc, const QString &path, QString *error, bool pdfa,
                     int fromPage, int toPage, bool booklet, int copies, bool comments, bool ua,
                     QStringList *warnings)
{
    pdfPostProcessSetDisplayDocTitle(ua);
    const PdfDocumentInfoMeta pdfInfo = pdfInfoMeta(doc);
    pdfPostProcessSetDocumentInfo(pdfInfo);
    const QString watermarkForOpaque = doc.watermark;
    bool paintOpaqueWatermark = false;
    const Document paintDoc = pdfa ? documentForPdfaPaint(doc, warnings, &paintOpaqueWatermark) : doc;

    double wMm = 0, hMm = 0;
    paintDoc.page.pageSizeMm(wMm, hMm);
    DocumentLayout lay;
    lay.relayout(paintDoc, 1.0);
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
    // QPdfWriter truncates the path it opens. Render into a private file and
    // publish with QSaveFile only after post-process succeeds.
    QTemporaryDir stageDir;
    if (!stageDir.isValid()) {
        if (error)
            *error = QStringLiteral("Cannot write PDF ") + path;
        return false;
    }
    const QString staged = stageDir.filePath(QStringLiteral("export.pdf"));
    QPdfWriter writer(staged);
    if (booklet)
        writer.setPageSize(QPageSize(QSizeF(wMm * 2, hMm), QPageSize::Millimeter));
    else
        writer.setPageSize(QPageSize(QSizeF(wMm, hMm), QPageSize::Millimeter));
    writer.setResolution(96);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setTitle(doc.title);
    writer.setCreator(QStringLiteral("AI Office"));
    writer.setAuthor(doc.author);
    writer.setDocumentXmpMetadata(buildPdfDocumentXmp(pdfa, ua, pdfInfo));
    if (pdfa)
        writer.setPdfVersion(QPagedPaintDevice::PdfVersion_A1b);
    QPainter p(&writer);
    if (!p.isActive()) {
        if (error)
            *error = QStringLiteral("Cannot write PDF ") + path;
        return false;
    }
    pdfCaptureBegin();
    auto paintDocPage = [&](int pg, const QRectF &slot) {
        if (pg < 1 || pg > lay.pages)
            return;
        p.save();
        p.translate(slot.topLeft());
        const qreal sx = slot.width() / qreal(qMax(1, lay.paperW));
        const qreal sy = slot.height() / qreal(qMax(1, lay.paperH));
        p.scale(sx, sy);
        paintPage(&p, paintDoc, lay, pg);
        if (pdfa && paintOpaqueWatermark)
            paintOpaqueWatermarkOverlay(&p, lay, watermarkForOpaque);
        p.restore();
    };
    bool first = true;
    QVector<int> physPageDocPage; // physical PDF page index -> doc page number (0 = n/a, e.g. booklet halves)
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
                physPageDocPage.push_back(0); // two logical pages share one physical page: ambiguous
            }
        } else {
            for (int pg : seq) {
                if (!first)
                    writer.newPage();
                first = false;
                paintDocPage(pg, QRectF(0, 0, writer.width(), writer.height()));
                physPageDocPage.push_back(pg);
            }
        }
    };
    const int nCopies = qMax(1, copies);
    for (int c = 0; c < nCopies; ++c)
        emitSeq();
    if (comments && booklet) {
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
            pdfDrawText(&p, QPointF(36, y), QStringLiteral("Comentários"));
            y += 28;
            cf.setPointSize(9);
            p.setFont(cf);
            for (const auto &line : lines) {
                pdfDrawText(&p, QRectF(36, y, writer.width() - 72, 40), Qt::TextWordWrap, line);
                y += 36;
                if (y > writer.height() - 48)
                    break;
            }
        }
    }
    p.end();
    const QVector<PdfSubsetUnicode> subsetUnicode = pdfCaptureEnd();

    const double pageWidthPt = wMm * 72.0 / 25.4;
    const double pageHeightPt = hMm * 72.0 / 25.4;
    QVector<PdfCommentAnnot> annots;
    if (comments && !booklet) {
        QSet<QString> seen;
        for (const auto &lb : lay.blocks) {
            if (seen.contains(lb.id))
                continue;
            const Block *b = doc.findBlock(lb.id);
            if (!b || b->comments.isEmpty())
                continue;
            seen.insert(lb.id);
            const double xPt = (lb.rect.left() / qMax(1, lay.paperW)) * pageWidthPt;
            const double yTopDownPt = (lb.rect.top() / qMax(1, lay.paperH)) * pageHeightPt;
            const double uryPt = pageHeightPt - yTopDownPt;
            for (const auto &cm : b->comments) {
                if (cm.resolved)
                    continue;
                for (int phys = 0; phys < physPageDocPage.size(); ++phys) {
                    if (physPageDocPage[phys] != lb.page)
                        continue;
                    PdfCommentAnnot a;
                    a.physicalPage = phys;
                    a.xPt = xPt;
                    a.yPt = uryPt;
                    a.author = cm.author;
                    a.text = cm.text;
                    annots.push_back(a);
                }
            }
        }
    }
    QString postErr;
    if (!pdfPostProcessAddExtras(staged, annots, physPageDocPage.size(), &postErr)) {
        if (error)
            *error = postErr;
        return false;
    }

    // The same marker list paintPage clips with. Booklet halves share a physical page
    // (doc page 0 here) and keep the page-level fallback.
    QVector<QVector<PdfTagMarker>> perPageMarkers(physPageDocPage.size());
    const int pagesPerCopy = qMax(1, physPageDocPage.size() / nCopies);
    for (int phys = 0; phys < physPageDocPage.size(); ++phys) {
        const int docPage = physPageDocPage[phys];
        if (docPage >= 1)
            perPageMarkers[phys] = pdfTagMarkersForPage(doc, lay, docPage);
        const int copyIndex = phys / pagesPerCopy;
        for (PdfTagMarker &marker : perPageMarkers[phys])
            marker.copyIndex = copyIndex;
    }
    QString tagErr;
    if (!pdfPostProcessTagStructure(staged, perPageMarkers, documentLanguage(doc), doc.readingOrder, &tagErr)) {
        if (error)
            *error = tagErr;
        return false;
    }
    QString unicodeErr;
    if (!pdfPostProcessFixToUnicode(staged, subsetUnicode, &unicodeErr)) {
        if (error)
            *error = unicodeErr;
        return false;
    }
    QFile produced(staged);
    if (!produced.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot write PDF ") + path;
        return false;
    }
    const QByteArray bytes = produced.readAll();
    produced.close();
    if (bytes.isEmpty()) {
        if (error)
            *error = QStringLiteral("Cannot write PDF ") + path;
        return false;
    }
    return writeFileAtomic(path, bytes, error);
}

void pdfPostProcessSetDisplayDocTitle(bool enable) { g_pdfPostProcessDisplayDocTitle = enable; }
bool pdfPostProcessPendingDisplayDocTitle() { return g_pdfPostProcessDisplayDocTitle; }

static PdfDocumentInfoMeta g_pdfDocumentInfo;
static bool g_pdfDocumentInfoSet = false;

void pdfPostProcessSetDocumentInfo(const PdfDocumentInfoMeta &info)
{
    g_pdfDocumentInfo = info;
    g_pdfDocumentInfoSet = true;
}

bool pdfPostProcessPendingDocumentInfo(PdfDocumentInfoMeta *out)
{
    if (!g_pdfDocumentInfoSet || !out)
        return false;
    *out = g_pdfDocumentInfo;
    return true;
}

} // namespace aioffice
