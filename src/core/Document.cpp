#include "core/Document.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>

namespace aioffice {

QJsonObject TextRun::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("text")] = text;
    o[QStringLiteral("bold")] = bold;
    o[QStringLiteral("italic")] = italic;
    o[QStringLiteral("underline")] = underline;
    if (!font.isEmpty())
        o[QStringLiteral("font")] = font;
    if (sizePt > 0)
        o[QStringLiteral("size_pt")] = sizePt;
    if (!color.isEmpty())
        o[QStringLiteral("color")] = color;
    if (!highlight.isEmpty())
        o[QStringLiteral("highlight")] = highlight;
    if (!link.isEmpty())
        o[QStringLiteral("link")] = link;
    return o;
}

TextRun TextRun::fromJson(const QJsonObject &o)
{
    TextRun r;
    r.text = o.value(QStringLiteral("text")).toString();
    r.bold = o.value(QStringLiteral("bold")).toBool();
    r.italic = o.value(QStringLiteral("italic")).toBool();
    r.underline = o.value(QStringLiteral("underline")).toBool();
    r.font = o.value(QStringLiteral("font")).toString();
    r.sizePt = o.value(QStringLiteral("size_pt")).toDouble();
    r.color = o.value(QStringLiteral("color")).toString();
    r.highlight = o.value(QStringLiteral("highlight")).toString();
    r.link = o.value(QStringLiteral("link")).toString();
    return r;
}

QJsonObject Comment::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("author")] = author;
    o[QStringLiteral("text")] = text;
    o[QStringLiteral("resolved")] = resolved;
    return o;
}

Comment Comment::fromJson(const QJsonObject &o)
{
    Comment c;
    c.id = o.value(QStringLiteral("id")).toString();
    c.author = o.value(QStringLiteral("author")).toString();
    c.text = o.value(QStringLiteral("text")).toString();
    c.resolved = o.value(QStringLiteral("resolved")).toBool();
    return c;
}

QJsonObject HeaderFooter::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("text")] = text;
    o[QStringLiteral("page_numbers")] = pageNumbers;
    return o;
}

HeaderFooter HeaderFooter::fromJson(const QJsonObject &o)
{
    HeaderFooter h;
    h.text = o.value(QStringLiteral("text")).toString();
    h.pageNumbers = o.value(QStringLiteral("page_numbers")).toBool();
    return h;
}

QJsonObject Selection::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("anchor_id")] = anchorId;
    o[QStringLiteral("anchor_off")] = anchorOff;
    o[QStringLiteral("focus_id")] = focusId;
    o[QStringLiteral("focus_off")] = focusOff;
    if (tableRow >= 0)
        o[QStringLiteral("row")] = tableRow;
    if (tableCol >= 0)
        o[QStringLiteral("col")] = tableCol;
    return o;
}

Selection Selection::fromJson(const QJsonObject &o)
{
    Selection s;
    s.anchorId = o.value(QStringLiteral("anchor_id")).toString();
    s.anchorOff = o.value(QStringLiteral("anchor_off")).toInt();
    s.focusId = o.value(QStringLiteral("focus_id")).toString();
    s.focusOff = o.value(QStringLiteral("focus_off")).toInt();
    s.tableRow = o.value(QStringLiteral("row")).toInt(-1);
    s.tableCol = o.value(QStringLiteral("col")).toInt(-1);
    return s;
}

static QVector<TextRun> runsFromJson(const QJsonValue &v)
{
    QVector<TextRun> runs;
    if (v.isArray()) {
        for (const auto &x : v.toArray())
            runs.push_back(TextRun::fromJson(x.toObject()));
    }
    return runs;
}

static QJsonArray runsToJson(const QVector<TextRun> &runs)
{
    QJsonArray a;
    for (const auto &r : runs)
        a.append(r.toJson());
    return a;
}

QJsonObject Block::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("type")] = type;
    if (level > 0)
        o[QStringLiteral("level")] = level;
    if (!style.isEmpty())
        o[QStringLiteral("style")] = style;
    if (!align.isEmpty())
        o[QStringLiteral("align")] = align;
    if (!listKind.isEmpty())
        o[QStringLiteral("list_kind")] = listKind;
    if (!breakKind.isEmpty())
        o[QStringLiteral("break_kind")] = breakKind;
    if (type == QLatin1String("table")) {
        QJsonArray rows;
        for (const auto &row : table) {
            QJsonArray cells;
            for (const auto &cell : row) {
                QJsonObject c;
                c[QStringLiteral("runs")] = runsToJson(cell.runs);
                if (cell.colSpan > 1)
                    c[QStringLiteral("col_span")] = cell.colSpan;
                cells.append(c);
            }
            rows.append(cells);
        }
        o[QStringLiteral("rows")] = rows;
        if (headerRow)
            o[QStringLiteral("header")] = true;
        if (!colWidthsMm.isEmpty()) {
            QJsonArray w;
            for (double mm : colWidthsMm)
                w.append(mm);
            o[QStringLiteral("col_widths_mm")] = w;
        }
    } else if (type == QLatin1String("image")) {
        o[QStringLiteral("path")] = imagePath;
        o[QStringLiteral("width_mm")] = imageWidthMm;
        if (!imageBytes.isEmpty())
            o[QStringLiteral("image_b64")] = QString::fromLatin1(imageBytes.toBase64());
        if (!imageMime.isEmpty())
            o[QStringLiteral("image_mime")] = imageMime;
    } else if (type != QLatin1String("break")) {
        o[QStringLiteral("runs")] = runsToJson(runs);
    }
    if (!comments.isEmpty()) {
        QJsonArray ca;
        for (const auto &c : comments)
            ca.append(c.toJson());
        o[QStringLiteral("comments")] = ca;
    }
    return o;
}

Block Block::fromJson(const QJsonObject &o)
{
    Block b;
    b.id = o.value(QStringLiteral("id")).toString();
    b.type = o.value(QStringLiteral("type")).toString(QStringLiteral("paragraph"));
    b.level = o.value(QStringLiteral("level")).toInt();
    b.style = o.value(QStringLiteral("style")).toString();
    b.align = o.value(QStringLiteral("align")).toString();
    b.listKind = o.value(QStringLiteral("list_kind")).toString();
    b.breakKind = o.value(QStringLiteral("break_kind")).toString();
    b.runs = runsFromJson(o.value(QStringLiteral("runs")));
    if (b.runs.isEmpty() && o.contains(QStringLiteral("text"))) {
        TextRun r;
        r.text = o.value(QStringLiteral("text")).toString();
        b.runs.push_back(r);
    }
    if (o.contains(QStringLiteral("rows"))) {
        for (const auto &rowV : o.value(QStringLiteral("rows")).toArray()) {
            QVector<TableCell> row;
            for (const auto &cellV : rowV.toArray()) {
                TableCell cell;
                cell.runs = runsFromJson(cellV.toObject().value(QStringLiteral("runs")));
                if (cell.runs.isEmpty() && cellV.toObject().contains(QStringLiteral("text"))) {
                    TextRun r;
                    r.text = cellV.toObject().value(QStringLiteral("text")).toString();
                    cell.runs.push_back(r);
                }
                cell.colSpan = qMax(1, cellV.toObject().value(QStringLiteral("col_span")).toInt(1));
                row.push_back(cell);
            }
            b.table.push_back(row);
        }
        b.headerRow = o.value(QStringLiteral("header")).toBool();
        for (const auto &w : o.value(QStringLiteral("col_widths_mm")).toArray())
            b.colWidthsMm.push_back(w.toDouble());
    }
    b.imagePath = o.value(QStringLiteral("path")).toString();
    b.imageWidthMm = o.value(QStringLiteral("width_mm")).toInt(80);
    b.imageMime = o.value(QStringLiteral("image_mime")).toString();
    const QString b64 = o.value(QStringLiteral("image_b64")).toString();
    if (!b64.isEmpty())
        b.imageBytes = QByteArray::fromBase64(b64.toLatin1());
    for (const auto &c : o.value(QStringLiteral("comments")).toArray())
        b.comments.push_back(Comment::fromJson(c.toObject()));
    return b;
}

QString Block::plainText() const
{
    if (type == QLatin1String("table")) {
        QStringList parts;
        for (const auto &row : table) {
            for (const auto &cell : row) {
                QString t;
                for (const auto &r : cell.runs)
                    t += r.text;
                parts << t;
            }
        }
        return parts.join(QLatin1Char(' '));
    }
    QString t;
    for (const auto &r : runs)
        t += r.text;
    return t;
}

QJsonObject StyleDef::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("font")] = font;
    o[QStringLiteral("size_pt")] = sizePt;
    o[QStringLiteral("bold")] = bold;
    o[QStringLiteral("italic")] = italic;
    o[QStringLiteral("color")] = color;
    if (!align.isEmpty())
        o[QStringLiteral("align")] = align;
    o[QStringLiteral("space_before_pt")] = spaceBeforePt;
    o[QStringLiteral("space_after_pt")] = spaceAfterPt;
    return o;
}

StyleDef StyleDef::fromJson(const QJsonObject &o)
{
    StyleDef s;
    s.name = o.value(QStringLiteral("name")).toString();
    s.font = o.value(QStringLiteral("font")).toString(s.font);
    s.sizePt = o.value(QStringLiteral("size_pt")).toDouble(s.sizePt);
    s.bold = o.value(QStringLiteral("bold")).toBool();
    s.italic = o.value(QStringLiteral("italic")).toBool();
    s.color = o.value(QStringLiteral("color")).toString(s.color);
    s.align = o.value(QStringLiteral("align")).toString();
    s.spaceBeforePt = o.value(QStringLiteral("space_before_pt")).toDouble();
    s.spaceAfterPt = o.value(QStringLiteral("space_after_pt")).toDouble(8);
    return s;
}

QJsonObject PageLayout::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("size")] = size;
    o[QStringLiteral("orientation")] = orientation;
    QJsonArray m;
    m.append(marginsMm[0]);
    m.append(marginsMm[1]);
    m.append(marginsMm[2]);
    m.append(marginsMm[3]);
    o[QStringLiteral("margins_mm")] = m;
    return o;
}

PageLayout PageLayout::fromJson(const QJsonObject &o)
{
    PageLayout p;
    p.size = o.value(QStringLiteral("size")).toString(p.size);
    p.orientation = o.value(QStringLiteral("orientation")).toString(p.orientation);
    const auto m = o.value(QStringLiteral("margins_mm")).toArray();
    for (int i = 0; i < 4 && i < m.size(); ++i)
        p.marginsMm[i] = m.at(i).toDouble(p.marginsMm[i]);
    return p;
}

void PageLayout::pageSizeMm(double &w, double &h) const
{
    if (size.compare(QLatin1String("Letter"), Qt::CaseInsensitive) == 0) {
        w = 215.9;
        h = 279.4;
    } else {
        w = 210;
        h = 297;
    }
    if (orientation.compare(QLatin1String("landscape"), Qt::CaseInsensitive) == 0)
        std::swap(w, h);
}

StyleDef defaultStyle(const QString &name)
{
    StyleDef s;
    s.name = name;
    if (name == QLatin1String("Title")) {
        s.sizePt = 28;
        s.bold = true;
        s.spaceAfterPt = 12;
    } else if (name == QLatin1String("Heading 1")) {
        s.sizePt = 22;
        s.bold = true;
        s.spaceBeforePt = 16;
        s.spaceAfterPt = 8;
    } else if (name == QLatin1String("Heading 2")) {
        s.sizePt = 16;
        s.bold = true;
        s.spaceBeforePt = 12;
        s.spaceAfterPt = 6;
    } else if (name == QLatin1String("Heading 3")) {
        s.sizePt = 13;
        s.bold = true;
        s.spaceBeforePt = 10;
        s.spaceAfterPt = 4;
    } else if (name == QLatin1String("Quote")) {
        s.italic = true;
        s.sizePt = 12;
        s.color = QStringLiteral("#4A5560");
    } else {
        s.name = QStringLiteral("Normal");
        s.sizePt = 12;
        s.spaceAfterPt = 8;
    }
    return s;
}

Document Document::blank(const QString &title)
{
    Document d;
    d.title = title.isEmpty() ? QStringLiteral("Sem título") : title;
    d.styles = {
        defaultStyle(QStringLiteral("Normal")),
        defaultStyle(QStringLiteral("Title")),
        defaultStyle(QStringLiteral("Heading 1")),
        defaultStyle(QStringLiteral("Heading 2")),
        defaultStyle(QStringLiteral("Heading 3")),
        defaultStyle(QStringLiteral("Quote")),
    };
    Block p;
    p.type = QStringLiteral("paragraph");
    p.style = QStringLiteral("Normal");
    p.align = QStringLiteral("left");
    d.assignId(p);
    d.body.push_back(p);
    return d;
}

QJsonObject Document::toJson() const
{
    QJsonObject root;
    root[QStringLiteral("schema")] = 1;
    QJsonObject meta;
    meta[QStringLiteral("title")] = title;
    if (!author.isEmpty())
        meta[QStringLiteral("author")] = author;
    meta[QStringLiteral("page")] = page.toJson();
    if (!header.text.isEmpty() || header.pageNumbers)
        meta[QStringLiteral("header")] = header.toJson();
    if (!footer.text.isEmpty() || footer.pageNumbers)
        meta[QStringLiteral("footer")] = footer.toJson();
    root[QStringLiteral("meta")] = meta;
    QJsonArray st;
    for (const auto &s : styles)
        st.append(s.toJson());
    root[QStringLiteral("styles")] = st;
    QJsonArray bodyA;
    for (const auto &b : body)
        bodyA.append(b.toJson());
    root[QStringLiteral("body")] = bodyA;
    return root;
}

Document Document::fromJson(const QJsonObject &o)
{
    Document d;
    const auto meta = o.value(QStringLiteral("meta")).toObject();
    d.title = meta.value(QStringLiteral("title")).toString(QStringLiteral("Sem título"));
    d.author = meta.value(QStringLiteral("author")).toString();
    d.page = PageLayout::fromJson(meta.value(QStringLiteral("page")).toObject());
    d.header = HeaderFooter::fromJson(meta.value(QStringLiteral("header")).toObject());
    d.footer = HeaderFooter::fromJson(meta.value(QStringLiteral("footer")).toObject());
    for (const auto &s : o.value(QStringLiteral("styles")).toArray())
        d.styles.push_back(StyleDef::fromJson(s.toObject()));
    if (d.styles.isEmpty()) {
        d.styles = {
            defaultStyle(QStringLiteral("Normal")),
            defaultStyle(QStringLiteral("Heading 1")),
            defaultStyle(QStringLiteral("Heading 2")),
            defaultStyle(QStringLiteral("Heading 3")),
        };
    }
    for (const auto &b : o.value(QStringLiteral("body")).toArray()) {
        Block blk = Block::fromJson(b.toObject());
        if (blk.id.isEmpty())
            d.assignId(blk);
        d.body.push_back(blk);
    }
    return d;
}

int Document::nextCounter(const QString &prefix) const
{
    int maxN = 0;
    for (const auto &b : body) {
        if (!b.id.startsWith(prefix))
            continue;
        bool ok = false;
        const int n = b.id.mid(prefix.size()).toInt(&ok);
        if (ok && n > maxN)
            maxN = n;
    }
    return maxN + 1;
}

QString Document::assignId(Block &b)
{
    QString prefix = QStringLiteral("p");
    if (b.type == QLatin1String("heading"))
        prefix = QStringLiteral("h");
    else if (b.type == QLatin1String("list-item"))
        prefix = QStringLiteral("l");
    else if (b.type == QLatin1String("table"))
        prefix = QStringLiteral("t");
    else if (b.type == QLatin1String("image"))
        prefix = QStringLiteral("img");
    else if (b.type == QLatin1String("break"))
        prefix = QStringLiteral("br");
    b.id = prefix + QString::number(nextCounter(prefix));
    return b.id;
}

Block *Document::findBlock(const QString &id)
{
    for (auto &b : body) {
        if (b.id == id)
            return &b;
    }
    return nullptr;
}

const Block *Document::findBlock(const QString &id) const
{
    for (const auto &b : body) {
        if (b.id == id)
            return &b;
    }
    return nullptr;
}

int Document::indexOf(const QString &id) const
{
    for (int i = 0; i < body.size(); ++i) {
        if (body[i].id == id)
            return i;
    }
    return -1;
}

int Document::wordCount() const
{
    int n = 0;
    for (const auto &b : body) {
        const auto parts = b.plainText().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        n += parts.size();
    }
    return n;
}

static QString runsPlain(const QVector<TextRun> &runs)
{
    QString t;
    for (const auto &r : runs)
        t += r.text;
    return t;
}

QVector<TextRun> splitRuns(const QVector<TextRun> &runs, int start, int end)
{
    QVector<TextRun> out;
    int pos = 0;
    for (const auto &r : runs) {
        const int a = pos;
        const int b = pos + r.text.size();
        const int from = qMax(start, a);
        const int to = qMin(end, b);
        if (to > from) {
            TextRun n = r;
            n.text = r.text.mid(from - a, to - from);
            out.push_back(n);
        }
        pos = b;
    }
    return out;
}

QVector<TextRun> spliceRuns(QVector<TextRun> runs, int start, int end, const TextRun &insert)
{
    const QString plain = runsPlain(runs);
    const int s = qBound(0, start, plain.size());
    const int e = qBound(s, end, plain.size());
    QVector<TextRun> before = splitRuns(runs, 0, s);
    QVector<TextRun> after = splitRuns(runs, e, plain.size());
    QVector<TextRun> out = before;
    if (!insert.text.isEmpty() || insert.bold || insert.italic || insert.underline)
        out.push_back(insert);
    out += after;
    if (out.isEmpty())
        out.push_back(TextRun{});
    return out;
}

void applyFormatToRange(QVector<TextRun> &runs, int start, int end, const QJsonObject &fmt)
{
    const QString plain = runsPlain(runs);
    const int s = qBound(0, start, plain.size());
    const int e = qBound(s, end, plain.size());
    if (s == e && fmt.isEmpty())
        return;
    auto apply = [&](TextRun &r) {
        if (fmt.contains(QStringLiteral("bold")))
            r.bold = fmt.value(QStringLiteral("bold")).toBool();
        if (fmt.contains(QStringLiteral("italic")))
            r.italic = fmt.value(QStringLiteral("italic")).toBool();
        if (fmt.contains(QStringLiteral("underline")))
            r.underline = fmt.value(QStringLiteral("underline")).toBool();
        if (fmt.contains(QStringLiteral("font")))
            r.font = fmt.value(QStringLiteral("font")).toString();
        if (fmt.contains(QStringLiteral("size_pt")))
            r.sizePt = fmt.value(QStringLiteral("size_pt")).toDouble();
        if (fmt.contains(QStringLiteral("color")))
            r.color = fmt.value(QStringLiteral("color")).toString();
        if (fmt.contains(QStringLiteral("highlight")))
            r.highlight = fmt.value(QStringLiteral("highlight")).toString();
    };
    QVector<TextRun> before = splitRuns(runs, 0, s);
    QVector<TextRun> mid = splitRuns(runs, s, e);
    QVector<TextRun> after = splitRuns(runs, e, plain.size());
    for (auto &r : mid)
        apply(r);
    runs = before + mid + after;
    if (runs.isEmpty())
        runs.push_back(TextRun{});
}

} // namespace aioffice
