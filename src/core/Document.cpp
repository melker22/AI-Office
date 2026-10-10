#include "core/Document.h"

#include "core/Csl.h"
#include "core/FieldExpr.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <functional>

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
    if (smallCaps)
        o[QStringLiteral("small_caps")] = true;
    if (!caps.isEmpty())
        o[QStringLiteral("caps")] = caps;
    if (hidden)
        o[QStringLiteral("hidden")] = true;
    if (!charStyle.isEmpty())
        o[QStringLiteral("char_style")] = charStyle;
    if (!lang.isEmpty())
        o[QStringLiteral("lang")] = lang;
    if (kerning != 0)
        o[QStringLiteral("kerning")] = kerning;
    if (!link.isEmpty())
        o[QStringLiteral("link")] = link;
    if (strike)
        o[QStringLiteral("strike")] = true;
    if (superscript)
        o[QStringLiteral("superscript")] = true;
    if (subscript)
        o[QStringLiteral("subscript")] = true;
    if (!field.isEmpty())
        o[QStringLiteral("field")] = field;
    if (!fieldArg.isEmpty())
        o[QStringLiteral("field_arg")] = fieldArg;
    if (!xrefKind.isEmpty())
        o[QStringLiteral("xref_kind")] = xrefKind;
    if (!noteId.isEmpty())
        o[QStringLiteral("note_id")] = noteId;
    if (!ruby.isEmpty())
        o[QStringLiteral("ruby")] = ruby;
    if (redact)
        o[QStringLiteral("redact")] = true;
    if (!showIf.isEmpty())
        o[QStringLiteral("show_if")] = showIf;
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
    r.smallCaps = o.value(QStringLiteral("small_caps")).toBool();
    r.caps = o.value(QStringLiteral("caps")).toString();
    r.hidden = o.value(QStringLiteral("hidden")).toBool();
    r.charStyle = o.value(QStringLiteral("char_style")).toString();
    r.lang = o.value(QStringLiteral("lang")).toString();
    r.kerning = o.value(QStringLiteral("kerning")).toDouble();
    r.link = o.value(QStringLiteral("link")).toString();
    r.strike = o.value(QStringLiteral("strike")).toBool();
    r.superscript = o.value(QStringLiteral("superscript")).toBool();
    r.subscript = o.value(QStringLiteral("subscript")).toBool();
    r.field = o.value(QStringLiteral("field")).toString();
    r.fieldArg = o.value(QStringLiteral("field_arg")).toString();
    r.xrefKind = o.value(QStringLiteral("xref_kind")).toString();
    r.noteId = o.value(QStringLiteral("note_id")).toString();
    r.ruby = o.value(QStringLiteral("ruby")).toString();
    r.redact = o.value(QStringLiteral("redact")).toBool();
    r.showIf = o.value(QStringLiteral("show_if")).toString();
    return r;
}

QJsonObject Comment::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("author")] = author;
    o[QStringLiteral("text")] = text;
    o[QStringLiteral("resolved")] = resolved;
    if (!parentId.isEmpty())
        o[QStringLiteral("parent_id")] = parentId;
    return o;
}

Comment Comment::fromJson(const QJsonObject &o)
{
    Comment c;
    c.id = o.value(QStringLiteral("id")).toString();
    c.author = o.value(QStringLiteral("author")).toString();
    c.text = o.value(QStringLiteral("text")).toString();
    c.resolved = o.value(QStringLiteral("resolved")).toBool();
    c.parentId = o.value(QStringLiteral("parent_id")).toString();
    return c;
}

QJsonObject BiblioEntry::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    if (!key.isEmpty())
        o[QStringLiteral("key")] = key;
    o[QStringLiteral("author")] = author;
    o[QStringLiteral("title")] = title;
    o[QStringLiteral("year")] = year;
    if (!type.isEmpty() && type != QLatin1String("book"))
        o[QStringLiteral("type")] = type;
    if (!containerTitle.isEmpty())
        o[QStringLiteral("container_title")] = containerTitle;
    if (!publisher.isEmpty())
        o[QStringLiteral("publisher")] = publisher;
    if (!city.isEmpty())
        o[QStringLiteral("city")] = city;
    if (!volume.isEmpty())
        o[QStringLiteral("volume")] = volume;
    if (!issue.isEmpty())
        o[QStringLiteral("issue")] = issue;
    if (!pages.isEmpty())
        o[QStringLiteral("pages")] = pages;
    if (!edition.isEmpty())
        o[QStringLiteral("edition")] = edition;
    if (!editor.isEmpty())
        o[QStringLiteral("editor")] = editor;
    if (!doi.isEmpty())
        o[QStringLiteral("doi")] = doi;
    if (!url.isEmpty())
        o[QStringLiteral("url")] = url;
    if (!accessed.isEmpty())
        o[QStringLiteral("accessed")] = accessed;
    return o;
}

BiblioEntry BiblioEntry::fromJson(const QJsonObject &o)
{
    BiblioEntry e;
    e.id = o.value(QStringLiteral("id")).toString();
    e.key = o.value(QStringLiteral("key")).toString();
    e.author = o.value(QStringLiteral("author")).toString();
    e.title = o.value(QStringLiteral("title")).toString();
    e.year = o.value(QStringLiteral("year")).toString();
    e.type = o.value(QStringLiteral("type")).toString(QStringLiteral("book"));
    e.containerTitle = o.value(QStringLiteral("container_title")).toString();
    e.publisher = o.value(QStringLiteral("publisher")).toString();
    e.city = o.value(QStringLiteral("city")).toString();
    e.volume = o.value(QStringLiteral("volume")).toString();
    e.issue = o.value(QStringLiteral("issue")).toString();
    e.pages = o.value(QStringLiteral("pages")).toString();
    e.edition = o.value(QStringLiteral("edition")).toString();
    e.editor = o.value(QStringLiteral("editor")).toString();
    e.doi = o.value(QStringLiteral("doi")).toString();
    e.url = o.value(QStringLiteral("url")).toString();
    e.accessed = o.value(QStringLiteral("accessed")).toString();
    return e;
}

QJsonObject Bookmark::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("block_id")] = blockId;
    o[QStringLiteral("offset")] = offset;
    return o;
}

Bookmark Bookmark::fromJson(const QJsonObject &o)
{
    Bookmark b;
    b.id = o.value(QStringLiteral("id")).toString();
    b.name = o.value(QStringLiteral("name")).toString();
    b.blockId = o.value(QStringLiteral("block_id")).toString();
    b.offset = o.value(QStringLiteral("offset")).toInt();
    return b;
}

QJsonObject Revision::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("author")] = author;
    o[QStringLiteral("kind")] = kind;
    o[QStringLiteral("block_id")] = blockId;
    o[QStringLiteral("start")] = start;
    o[QStringLiteral("end")] = end;
    o[QStringLiteral("text")] = text;
    o[QStringLiteral("timestamp")] = double(timestamp);
    if (!payload.isEmpty())
        o[QStringLiteral("payload")] = payload;
    if (!deletedRuns.isEmpty()) {
        QJsonArray runs;
        for (const auto &r : deletedRuns)
            runs.push_back(r.toJson());
        o[QStringLiteral("deleted_runs")] = runs;
    }
    if (!priorRuns.isEmpty()) {
        QJsonArray runs;
        for (const auto &r : priorRuns)
            runs.push_back(r.toJson());
        o[QStringLiteral("prior_runs")] = runs;
    }
    return o;
}

Revision Revision::fromJson(const QJsonObject &o)
{
    Revision r;
    r.id = o.value(QStringLiteral("id")).toString();
    r.author = o.value(QStringLiteral("author")).toString();
    r.kind = o.value(QStringLiteral("kind")).toString();
    r.blockId = o.value(QStringLiteral("block_id")).toString();
    r.start = o.value(QStringLiteral("start")).toInt();
    r.end = o.value(QStringLiteral("end")).toInt();
    r.text = o.value(QStringLiteral("text")).toString();
    r.timestamp = qint64(o.value(QStringLiteral("timestamp")).toDouble());
    r.payload = o.value(QStringLiteral("payload")).toString();
    QString reconstructed;
    bool allObjects = true;
    for (const auto &rr : o.value(QStringLiteral("deleted_runs")).toArray()) {
        if (!rr.isObject()) {
            allObjects = false;
            break;
        }
        const TextRun run = TextRun::fromJson(rr.toObject());
        reconstructed += run.text;
        r.deletedRuns.push_back(run);
    }
    if (!allObjects || (!r.deletedRuns.isEmpty() && reconstructed != r.text))
        r.deletedRuns.clear();
    bool priorObjects = true;
    for (const auto &rr : o.value(QStringLiteral("prior_runs")).toArray()) {
        if (!rr.isObject()) {
            priorObjects = false;
            break;
        }
        r.priorRuns.push_back(TextRun::fromJson(rr.toObject()));
    }
    // Format `text` is often empty; prior slices carry the range. Drop the field only when it is not objects.
    if (!priorObjects)
        r.priorRuns.clear();
    // Legacy files stored delete as a range. The canonical point is where the text was (`start`).
    if (r.kind == QLatin1String("delete") && r.end != r.start)
        r.end = r.start;
    return r;
}

QJsonObject HeaderFooter::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("text")] = text;
    o[QStringLiteral("page_numbers")] = pageNumbers;
    if (chapter)
        o[QStringLiteral("chapter")] = true;
    if (!imagePath.isEmpty())
        o[QStringLiteral("image")] = imagePath;
    return o;
}

HeaderFooter HeaderFooter::fromJson(const QJsonObject &o)
{
    HeaderFooter h;
    h.text = o.value(QStringLiteral("text")).toString();
    h.pageNumbers = o.value(QStringLiteral("page_numbers")).toBool();
    h.chapter = o.value(QStringLiteral("chapter")).toBool();
    h.imagePath = o.value(QStringLiteral("image")).toString();
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
    if (!ids.isEmpty()) {
        QJsonArray a;
        for (const auto &id : ids)
            a.append(id);
        o[QStringLiteral("ids")] = a;
    }
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
    for (const auto &v : o.value(QStringLiteral("ids")).toArray())
        s.ids.push_back(v.toString());
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

static QJsonArray nestedToJson(const QVector<QVector<NestedCell>> &nested)
{
    QJsonArray rows;
    for (const auto &row : nested) {
        QJsonArray cells;
        for (const auto &c : row) {
            QJsonObject o;
            o[QStringLiteral("runs")] = runsToJson(c.runs);
            cells.append(o);
        }
        rows.append(cells);
    }
    return rows;
}

static QVector<QVector<NestedCell>> nestedFromJson(const QJsonValue &v)
{
    QVector<QVector<NestedCell>> nested;
    for (const auto &rowV : v.toArray()) {
        QVector<NestedCell> row;
        for (const auto &cellV : rowV.toArray()) {
            NestedCell cell;
            const QJsonObject o = cellV.toObject();
            cell.runs = runsFromJson(o.value(QStringLiteral("runs")));
            if (cell.runs.isEmpty() && o.contains(QStringLiteral("text"))) {
                TextRun r;
                r.text = o.value(QStringLiteral("text")).toString();
                cell.runs.push_back(r);
            }
            row.push_back(cell);
        }
        nested.push_back(row);
    }
    return nested;
}

static QString normalizedBlockDir(QString dir)
{
    if (dir == QLatin1String("auto"))
        dir.clear();
    return dir;
}

bool isValidBlockAlign(const QString &align)
{
    if (align.isEmpty())
        return true;
    return align == QLatin1String("left") || align == QLatin1String("right")
        || align == QLatin1String("center") || align == QLatin1String("justify")
        || align == QLatin1String("start") || align == QLatin1String("end");
}

bool isValidBlockDir(const QString &dir)
{
    const QString d = normalizedBlockDir(dir);
    if (d.isEmpty())
        return true;
    return d == QLatin1String("ltr") || d == QLatin1String("rtl");
}

QString validateBlock(const Block &b)
{
    if (!isValidBlockAlign(b.align))
        return QStringLiteral("invalid align: ") + b.align;
    if (!isValidBlockDir(b.dir))
        return QStringLiteral("invalid dir: ") + b.dir;
    return {};
}

QString validateParagraphFormatArgs(const QJsonObject &args)
{
    if (args.contains(QStringLiteral("align"))) {
        const QString align = args.value(QStringLiteral("align")).toString();
        if (!isValidBlockAlign(align))
            return QStringLiteral("invalid align: ") + align;
    }
    if (args.contains(QStringLiteral("dir"))) {
        const QString dir = args.value(QStringLiteral("dir")).toString();
        if (!isValidBlockDir(dir))
            return QStringLiteral("invalid dir: ") + dir;
    }
    return {};
}

Block duplicateBlock(const Block &src)
{
    return src;
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
    if (!dir.isEmpty() && dir != QLatin1String("auto"))
        o[QStringLiteral("dir")] = dir;
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
                if (cell.rowSpan > 1)
                    c[QStringLiteral("row_span")] = cell.rowSpan;
                if (cell.borderPt > 0)
                    c[QStringLiteral("border_pt")] = cell.borderPt;
                if (!cell.borderColor.isEmpty())
                    c[QStringLiteral("border_color")] = cell.borderColor;
                if (!cell.background.isEmpty())
                    c[QStringLiteral("background")] = cell.background;
                if (!cell.vAlign.isEmpty())
                    c[QStringLiteral("v_align")] = cell.vAlign;
                if (cell.paddingMm > 0)
                    c[QStringLiteral("padding_mm")] = cell.paddingMm;
                if (!cell.formula.isEmpty())
                    c[QStringLiteral("formula")] = cell.formula;
                if (!cell.nested.isEmpty())
                    c[QStringLiteral("nested")] = nestedToJson(cell.nested);
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
        if (!altText.isEmpty())
            o[QStringLiteral("alt")] = altText;
        if (!caption.isEmpty())
            o[QStringLiteral("caption")] = caption;
        if (!captionRuns.isEmpty())
            o[QStringLiteral("caption_runs")] = runsToJson(captionRuns);
        if (!wrap.isEmpty())
            o[QStringLiteral("wrap")] = wrap;
        if (!anchor.isEmpty())
            o[QStringLiteral("anchor")] = anchor;
        if (flipH)
            o[QStringLiteral("flip_h")] = true;
        if (flipV)
            o[QStringLiteral("flip_v")] = true;
        if (cropL > 0 || cropT > 0 || cropR > 0 || cropB > 0) {
            QJsonArray c;
            c.append(cropL);
            c.append(cropT);
            c.append(cropR);
            c.append(cropB);
            o[QStringLiteral("crop")] = c;
        }
    } else if (type == QLatin1String("shape") || type == QLatin1String("chart") || type == QLatin1String("qr")
               || type == QLatin1String("equation") || type == QLatin1String("form")
               || type == QLatin1String("biblio")) {
        o[QStringLiteral("runs")] = runsToJson(runs);
        if (imageWidthMm > 0)
            o[QStringLiteral("width_mm")] = imageWidthMm;
        if (heightMm > 0)
            o[QStringLiteral("height_mm")] = heightMm;
        if (!shapeKind.isEmpty())
            o[QStringLiteral("shape")] = shapeKind;
        if (!fill.isEmpty())
            o[QStringLiteral("fill")] = fill;
        if (!stroke.isEmpty())
            o[QStringLiteral("stroke")] = stroke;
        if (!qrText.isEmpty())
            o[QStringLiteral("qr")] = qrText;
        if (!formKind.isEmpty())
            o[QStringLiteral("form")] = formKind;
        if (!data.isEmpty())
            o[QStringLiteral("data")] = data;
        if (numbered)
            o[QStringLiteral("numbered")] = true;
        if (!connectTo.isEmpty())
            o[QStringLiteral("connect_to")] = connectTo;
    } else if (type != QLatin1String("break")) {
        o[QStringLiteral("runs")] = runsToJson(runs);
        if (type == QLatin1String("frame") && imageWidthMm > 0)
            o[QStringLiteral("width_mm")] = imageWidthMm;
    }
    if (!tocTargets.isEmpty()) {
        QJsonArray t;
        for (const auto &id : tocTargets)
            t.append(id);
        o[QStringLiteral("toc_targets")] = t;
    }
    if (!comments.isEmpty()) {
        QJsonArray ca;
        for (const auto &c : comments)
            ca.append(c.toJson());
        o[QStringLiteral("comments")] = ca;
    }
    if (indentLeftMm != 0)
        o[QStringLiteral("indent_left_mm")] = indentLeftMm;
    if (indentRightMm != 0)
        o[QStringLiteral("indent_right_mm")] = indentRightMm;
    if (indentFirstMm != 0)
        o[QStringLiteral("indent_first_mm")] = indentFirstMm;
    if (spaceBeforePt >= 0)
        o[QStringLiteral("space_before_pt")] = spaceBeforePt;
    if (spaceAfterPt >= 0)
        o[QStringLiteral("space_after_pt")] = spaceAfterPt;
    if (lineHeight != 1.0)
        o[QStringLiteral("line_height")] = lineHeight;
    if (keepWithNext)
        o[QStringLiteral("keep_with_next")] = true;
    if (breakBefore)
        o[QStringLiteral("break_before")] = true;
    if (orphans > 0)
        o[QStringLiteral("orphans")] = orphans;
    if (widows > 0)
        o[QStringLiteral("widows")] = widows;
    if (dropCapLines > 0)
        o[QStringLiteral("drop_cap_lines")] = dropCapLines;
    if (!tabs.isEmpty()) {
        QJsonArray ta;
        for (const auto &t : tabs) {
            QJsonObject to;
            to[QStringLiteral("pos_mm")] = t.posMm;
            if (!t.align.isEmpty() && t.align != QLatin1String("left"))
                to[QStringLiteral("align")] = t.align;
            ta.append(to);
        }
        o[QStringLiteral("tabs")] = ta;
    }
    if (borderPt > 0)
        o[QStringLiteral("border_pt")] = borderPt;
    if (!borderColor.isEmpty())
        o[QStringLiteral("border_color")] = borderColor;
    if (checked)
        o[QStringLiteral("checked")] = true;
    if (listRestart)
        o[QStringLiteral("list_restart")] = true;
    if (!listBullet.isEmpty())
        o[QStringLiteral("list_bullet")] = listBullet;
    if (!lang.isEmpty())
        o[QStringLiteral("lang")] = lang;
    if (pageRestart)
        o[QStringLiteral("page_restart")] = true;
    if (pageNumber > 0)
        o[QStringLiteral("page_number")] = pageNumber;
    if (rotateDeg != 0)
        o[QStringLiteral("rotate")] = rotateDeg;
    if (opacity != 1.0)
        o[QStringLiteral("opacity")] = opacity;
    if (shadowPt > 0)
        o[QStringLiteral("shadow_pt")] = shadowPt;
    if (zIndex != 0)
        o[QStringLiteral("z_index")] = zIndex;
    if (!groupId.isEmpty())
        o[QStringLiteral("group")] = groupId;
    if (!writingMode.isEmpty())
        o[QStringLiteral("writing_mode")] = writingMode;
    if (sectionHidden)
        o[QStringLiteral("section_hidden")] = true;
    if (!hideIf.isEmpty())
        o[QStringLiteral("hide_if")] = hideIf;
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
    b.dir = normalizedBlockDir(o.value(QStringLiteral("dir")).toString());
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
                cell.rowSpan = qMax(1, cellV.toObject().value(QStringLiteral("row_span")).toInt(1));
                cell.borderPt = cellV.toObject().value(QStringLiteral("border_pt")).toDouble();
                cell.borderColor = cellV.toObject().value(QStringLiteral("border_color")).toString();
                cell.background = cellV.toObject().value(QStringLiteral("background")).toString();
                cell.vAlign = cellV.toObject().value(QStringLiteral("v_align")).toString();
                cell.paddingMm = cellV.toObject().value(QStringLiteral("padding_mm")).toDouble();
                cell.formula = cellV.toObject().value(QStringLiteral("formula")).toString();
                cell.nested = nestedFromJson(cellV.toObject().value(QStringLiteral("nested")));
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
    for (const auto &t : o.value(QStringLiteral("toc_targets")).toArray())
        b.tocTargets.push_back(t.toString());
    b.indentLeftMm = o.value(QStringLiteral("indent_left_mm")).toDouble();
    b.indentRightMm = o.value(QStringLiteral("indent_right_mm")).toDouble();
    b.indentFirstMm = o.value(QStringLiteral("indent_first_mm")).toDouble();
    b.spaceBeforePt = o.contains(QStringLiteral("space_before_pt")) ? o.value(QStringLiteral("space_before_pt")).toDouble() : -1;
    b.spaceAfterPt = o.contains(QStringLiteral("space_after_pt")) ? o.value(QStringLiteral("space_after_pt")).toDouble() : -1;
    b.lineHeight = o.value(QStringLiteral("line_height")).toDouble(1.0);
    b.keepWithNext = o.value(QStringLiteral("keep_with_next")).toBool();
    b.breakBefore = o.value(QStringLiteral("break_before")).toBool();
    b.orphans = o.value(QStringLiteral("orphans")).toInt();
    b.widows = o.value(QStringLiteral("widows")).toInt();
    b.dropCapLines = o.value(QStringLiteral("drop_cap_lines")).toInt();
    for (const auto &t : o.value(QStringLiteral("tabs")).toArray()) {
        TabStop ts;
        ts.posMm = t.toObject().value(QStringLiteral("pos_mm")).toDouble();
        ts.align = t.toObject().value(QStringLiteral("align")).toString(QStringLiteral("left"));
        b.tabs.push_back(ts);
    }
    b.borderPt = o.value(QStringLiteral("border_pt")).toDouble();
    b.borderColor = o.value(QStringLiteral("border_color")).toString();
    b.altText = o.value(QStringLiteral("alt")).toString();
    b.caption = o.value(QStringLiteral("caption")).toString();
    for (const auto &rv : o.value(QStringLiteral("caption_runs")).toArray())
        b.captionRuns.push_back(TextRun::fromJson(rv.toObject()));
    b.wrap = o.value(QStringLiteral("wrap")).toString();
    b.anchor = o.value(QStringLiteral("anchor")).toString();
    b.checked = o.value(QStringLiteral("checked")).toBool();
    b.listRestart = o.value(QStringLiteral("list_restart")).toBool();
    b.listBullet = o.value(QStringLiteral("list_bullet")).toString();
    b.lang = o.value(QStringLiteral("lang")).toString();
    b.pageRestart = o.value(QStringLiteral("page_restart")).toBool();
    b.pageNumber = o.value(QStringLiteral("page_number")).toInt();
    b.rotateDeg = o.value(QStringLiteral("rotate")).toInt();
    b.opacity = o.contains(QStringLiteral("opacity")) ? o.value(QStringLiteral("opacity")).toDouble() : 1.0;
    b.flipH = o.value(QStringLiteral("flip_h")).toBool();
    b.flipV = o.value(QStringLiteral("flip_v")).toBool();
    const auto crop = o.value(QStringLiteral("crop")).toArray();
    if (crop.size() >= 4) {
        b.cropL = crop.at(0).toDouble();
        b.cropT = crop.at(1).toDouble();
        b.cropR = crop.at(2).toDouble();
        b.cropB = crop.at(3).toDouble();
    }
    b.shadowPt = o.value(QStringLiteral("shadow_pt")).toDouble();
    b.zIndex = o.value(QStringLiteral("z_index")).toInt();
    b.groupId = o.value(QStringLiteral("group")).toString();
    b.shapeKind = o.value(QStringLiteral("shape")).toString();
    b.fill = o.value(QStringLiteral("fill")).toString();
    b.stroke = o.value(QStringLiteral("stroke")).toString();
    b.heightMm = o.value(QStringLiteral("height_mm")).toDouble();
    b.data = o.value(QStringLiteral("data")).toArray();
    b.qrText = o.value(QStringLiteral("qr")).toString();
    b.formKind = o.value(QStringLiteral("form")).toString();
    b.writingMode = o.value(QStringLiteral("writing_mode")).toString();
    b.sectionHidden = o.value(QStringLiteral("section_hidden")).toBool();
    b.hideIf = o.value(QStringLiteral("hide_if")).toString();
    b.numbered = o.value(QStringLiteral("numbered")).toBool();
    b.connectTo = o.value(QStringLiteral("connect_to")).toString();
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
                for (const auto &nrow : cell.nested) {
                    for (const auto &nc : nrow) {
                        for (const auto &r : nc.runs)
                            t += r.text;
                    }
                }
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
    if (!kind.isEmpty() && kind != QLatin1String("paragraph"))
        o[QStringLiteral("kind")] = kind;
    if (!next.isEmpty())
        o[QStringLiteral("next")] = next;
    if (!listKind.isEmpty())
        o[QStringLiteral("list_kind")] = listKind;
    if (!listBullet.isEmpty())
        o[QStringLiteral("list_bullet")] = listBullet;
    if (!pageSize.isEmpty())
        o[QStringLiteral("page_size")] = pageSize;
    if (!pageOrientation.isEmpty())
        o[QStringLiteral("page_orientation")] = pageOrientation;
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
    s.kind = o.value(QStringLiteral("kind")).toString();
    s.next = o.value(QStringLiteral("next")).toString();
    s.listKind = o.value(QStringLiteral("list_kind")).toString();
    s.listBullet = o.value(QStringLiteral("list_bullet")).toString();
    s.pageSize = o.value(QStringLiteral("page_size")).toString();
    s.pageOrientation = o.value(QStringLiteral("page_orientation")).toString();
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
    if (columns > 1)
        o[QStringLiteral("columns")] = columns;
    if (hyphenate)
        o[QStringLiteral("hyphenate")] = true;
    if (gutterMm > 0)
        o[QStringLiteral("gutter_mm")] = gutterMm;
    if (columnGapMm != 5)
        o[QStringLiteral("column_gap_mm")] = columnGapMm;
    if (columnSeparator)
        o[QStringLiteral("column_separator")] = true;
    if (borderPt > 0)
        o[QStringLiteral("border_pt")] = borderPt;
    if (!borderColor.isEmpty())
        o[QStringLiteral("border_color")] = borderColor;
    if (!background.isEmpty())
        o[QStringLiteral("background")] = background;
    if (!numberFormat.isEmpty() && numberFormat != QLatin1String("arabic"))
        o[QStringLiteral("number_format")] = numberFormat;
    if (numberStart != 1)
        o[QStringLiteral("number_start")] = numberStart;
    if (!columnWeights.isEmpty()) {
        QJsonArray w;
        for (double x : columnWeights)
            w.append(x);
        o[QStringLiteral("column_widths")] = w;
    }
    if (widthMm > 0)
        o[QStringLiteral("width_mm")] = widthMm;
    if (heightMm > 0)
        o[QStringLiteral("height_mm")] = heightMm;
    if (hyphenZoneMm > 0)
        o[QStringLiteral("hyphen_zone_mm")] = hyphenZoneMm;
    if (gutterMirror)
        o[QStringLiteral("gutter_mirror")] = true;
    if (lineNumbers)
        o[QStringLiteral("line_numbers")] = true;
    if (chapterNumbers)
        o[QStringLiteral("chapter_numbers")] = true;
    if (noteRestart)
        o[QStringLiteral("note_restart")] = true;
    if (!tone.isEmpty())
        o[QStringLiteral("tone")] = tone;
    if (!writingMode.isEmpty())
        o[QStringLiteral("writing_mode")] = writingMode;
    if (gridMm > 0)
        o[QStringLiteral("grid_mm")] = gridMm;
    if (booklet)
        o[QStringLiteral("booklet")] = true;
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
    p.columns = qBound(1, o.value(QStringLiteral("columns")).toInt(1), 3);
    p.hyphenate = o.value(QStringLiteral("hyphenate")).toBool();
    p.gutterMm = o.value(QStringLiteral("gutter_mm")).toDouble();
    p.columnGapMm = o.value(QStringLiteral("column_gap_mm")).toDouble(5);
    p.columnSeparator = o.value(QStringLiteral("column_separator")).toBool();
    p.borderPt = o.value(QStringLiteral("border_pt")).toDouble();
    p.borderColor = o.value(QStringLiteral("border_color")).toString();
    p.background = o.value(QStringLiteral("background")).toString();
    p.numberFormat = o.value(QStringLiteral("number_format")).toString();
    p.numberStart = o.contains(QStringLiteral("number_start"))
        ? o.value(QStringLiteral("number_start")).toInt(1)
        : 1;
    for (const auto &w : o.value(QStringLiteral("column_widths")).toArray())
        p.columnWeights.push_back(w.toDouble());
    p.widthMm = o.value(QStringLiteral("width_mm")).toDouble();
    p.heightMm = o.value(QStringLiteral("height_mm")).toDouble();
    p.hyphenZoneMm = o.value(QStringLiteral("hyphen_zone_mm")).toDouble();
    p.gutterMirror = o.value(QStringLiteral("gutter_mirror")).toBool();
    p.lineNumbers = o.value(QStringLiteral("line_numbers")).toBool();
    p.chapterNumbers = o.value(QStringLiteral("chapter_numbers")).toBool();
    p.noteRestart = o.value(QStringLiteral("note_restart")).toBool();
    p.tone = o.value(QStringLiteral("tone")).toString();
    p.writingMode = o.value(QStringLiteral("writing_mode")).toString();
    p.gridMm = o.value(QStringLiteral("grid_mm")).toDouble();
    p.booklet = o.value(QStringLiteral("booklet")).toBool();
    return p;
}

void PageLayout::pageSizeMm(double &w, double &h) const
{
    if (widthMm > 0 && heightMm > 0) {
        w = widthMm;
        h = heightMm;
        if (orientation.compare(QLatin1String("landscape"), Qt::CaseInsensitive) == 0 && widthMm < heightMm)
            std::swap(w, h);
        return;
    }
    const QString s = size.toLower();
    if (s == QLatin1String("letter")) {
        w = 215.9;
        h = 279.4;
    } else if (s == QLatin1String("legal")) {
        w = 215.9;
        h = 355.6;
    } else if (s == QLatin1String("tabloid")) {
        w = 279.4;
        h = 431.8;
    } else if (s == QLatin1String("a3")) {
        w = 297;
        h = 420;
    } else if (s == QLatin1String("a5")) {
        w = 148;
        h = 210;
    } else if (s == QLatin1String("a6")) {
        w = 105;
        h = 148;
    } else if (s == QLatin1String("dl") || s == QLatin1String("envelope") || s == QLatin1String("envelope-dl")) {
        w = 110;
        h = 220;
    } else if (s == QLatin1String("c5")) {
        w = 162;
        h = 229;
    } else if (s == QLatin1String("c6")) {
        w = 114;
        h = 162;
    } else if (s == QLatin1String("b4")) {
        w = 250;
        h = 353;
    } else if (s == QLatin1String("b5")) {
        w = 176;
        h = 250;
    } else if (s == QLatin1String("executive")) {
        w = 184.2;
        h = 266.7;
    } else if (s == QLatin1String("folio")) {
        w = 210;
        h = 330;
    } else if (s == QLatin1String("statement")) {
        w = 139.7;
        h = 215.9;
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
    } else if (name.startsWith(QLatin1String("TOC "))) {
        const int lv = qBound(1, name.mid(4).trimmed().toInt(), 6);
        s.sizePt = qMax(9.0, 13.0 - lv);
        s.spaceAfterPt = 2;
        s.italic = lv > 1;
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
    if (headerEven.active())
        meta[QStringLiteral("header_even")] = headerEven.toJson();
    if (footerEven.active())
        meta[QStringLiteral("footer_even")] = footerEven.toJson();
    if (headerFirst.active())
        meta[QStringLiteral("header_first")] = headerFirst.toJson();
    if (footerFirst.active())
        meta[QStringLiteral("footer_first")] = footerFirst.toJson();
    if (trackChanges)
        meta[QStringLiteral("track_changes")] = true;
    if (readOnly)
        meta[QStringLiteral("read_only")] = true;
    if (!fileName.isEmpty())
        meta[QStringLiteral("file_name")] = fileName;
    if (!subject.isEmpty())
        meta[QStringLiteral("subject")] = subject;
    if (!keywords.isEmpty())
        meta[QStringLiteral("keywords")] = keywords;
    if (!watermark.isEmpty())
        meta[QStringLiteral("watermark")] = watermark;
    if (!vars.isEmpty()) {
        QJsonObject vo;
        for (auto it = vars.begin(); it != vars.end(); ++it)
            vo[it.key()] = it.value();
        meta[QStringLiteral("vars")] = vo;
    }
    if (!autotext.isEmpty()) {
        QJsonObject ao;
        for (auto it = autotext.begin(); it != autotext.end(); ++it)
            ao[it.key()] = it.value();
        meta[QStringLiteral("autotext")] = ao;
    }
    if (!userDic.isEmpty()) {
        QJsonArray a;
        for (const auto &w : userDic)
            a.append(w);
        meta[QStringLiteral("user_dic")] = a;
    }
    if (!imageLibrary.isEmpty()) {
        QJsonArray a;
        for (const auto &p : imageLibrary)
            a.append(p);
        meta[QStringLiteral("image_library")] = a;
    }
    if (!readingOrder.isEmpty()) {
        QJsonArray a;
        for (const auto &id : readingOrder)
            a.append(id);
        meta[QStringLiteral("reading_order")] = a;
    }
    root[QStringLiteral("meta")] = meta;
    QJsonArray st;
    for (const auto &s : styles)
        st.append(s.toJson());
    root[QStringLiteral("styles")] = st;
    QJsonArray bodyA;
    for (const auto &b : body)
        bodyA.append(b.toJson());
    root[QStringLiteral("body")] = bodyA;
    if (!bookmarks.isEmpty()) {
        QJsonArray bm;
        for (const auto &b : bookmarks)
            bm.append(b.toJson());
        root[QStringLiteral("bookmarks")] = bm;
    }
    if (!revisions.isEmpty()) {
        QJsonArray rv;
        for (const auto &r : revisions)
            rv.append(r.toJson());
        root[QStringLiteral("revisions")] = rv;
    }
    if (!autocorrect.isEmpty()) {
        QJsonObject ac;
        for (auto it = autocorrect.begin(); it != autocorrect.end(); ++it)
            ac[it.key()] = it.value();
        root[QStringLiteral("autocorrect")] = ac;
    }
    if (!thesaurus.isEmpty()) {
        QJsonObject th;
        for (auto it = thesaurus.begin(); it != thesaurus.end(); ++it)
            th[it.key()] = it.value();
        root[QStringLiteral("thesaurus")] = th;
    }
    if (!bibliography.isEmpty()) {
        QJsonArray ba;
        for (const auto &e : bibliography)
            ba.append(e.toJson());
        root[QStringLiteral("bibliography")] = ba;
    }
    if (citeStyle != QLatin1String("apa"))
        root[QStringLiteral("cite_style")] = citeStyle;
    return root;
}

namespace {

QJsonValue sortJsonValue(const QJsonValue &v);

QJsonObject sortJsonObject(const QJsonObject &o)
{
    QStringList keys = o.keys();
    keys.sort();
    QJsonObject out;
    for (const QString &k : keys)
        out.insert(k, sortJsonValue(o.value(k)));
    return out;
}

QJsonValue sortJsonValue(const QJsonValue &v)
{
    if (v.isObject())
        return sortJsonObject(v.toObject());
    if (v.isArray()) {
        QJsonArray a;
        for (const auto &x : v.toArray())
            a.append(sortJsonValue(x));
        return a;
    }
    return v;
}

} // namespace

QByteArray Document::toCanonicalJson() const
{
    return QJsonDocument(sortJsonObject(toJson())).toJson(QJsonDocument::Indented);
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
    d.headerEven = HeaderFooter::fromJson(meta.value(QStringLiteral("header_even")).toObject());
    d.footerEven = HeaderFooter::fromJson(meta.value(QStringLiteral("footer_even")).toObject());
    d.headerFirst = HeaderFooter::fromJson(meta.value(QStringLiteral("header_first")).toObject());
    d.footerFirst = HeaderFooter::fromJson(meta.value(QStringLiteral("footer_first")).toObject());
    d.trackChanges = meta.value(QStringLiteral("track_changes")).toBool();
    d.readOnly = meta.value(QStringLiteral("read_only")).toBool();
    d.fileName = meta.value(QStringLiteral("file_name")).toString();
    d.subject = meta.value(QStringLiteral("subject")).toString();
    d.keywords = meta.value(QStringLiteral("keywords")).toString();
    d.watermark = meta.value(QStringLiteral("watermark")).toString();
    const auto vo = meta.value(QStringLiteral("vars")).toObject();
    for (auto it = vo.begin(); it != vo.end(); ++it)
        d.vars.insert(it.key(), it.value().toString());
    const auto ao = meta.value(QStringLiteral("autotext")).toObject();
    for (auto it = ao.begin(); it != ao.end(); ++it)
        d.autotext.insert(it.key(), it.value().toString());
    for (const auto &w : meta.value(QStringLiteral("user_dic")).toArray())
        d.userDic.push_back(w.toString());
    for (const auto &p : meta.value(QStringLiteral("image_library")).toArray())
        d.imageLibrary.push_back(p.toString());
    for (const auto &id : meta.value(QStringLiteral("reading_order")).toArray())
        d.readingOrder.push_back(id.toString());
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
    for (const auto &b : o.value(QStringLiteral("bookmarks")).toArray())
        d.bookmarks.push_back(Bookmark::fromJson(b.toObject()));
    for (const auto &r : o.value(QStringLiteral("revisions")).toArray())
        d.revisions.push_back(Revision::fromJson(r.toObject()));
    const auto ac = o.value(QStringLiteral("autocorrect")).toObject();
    for (auto it = ac.begin(); it != ac.end(); ++it)
        d.autocorrect.insert(it.key(), it.value().toString());
    const auto th = o.value(QStringLiteral("thesaurus")).toObject();
    for (auto it = th.begin(); it != th.end(); ++it)
        d.thesaurus.insert(it.key(), it.value().toString());
    for (const auto &e : o.value(QStringLiteral("bibliography")).toArray())
        d.bibliography.push_back(BiblioEntry::fromJson(e.toObject()));
    d.citeStyle = o.value(QStringLiteral("cite_style")).toString(QStringLiteral("apa"));
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
    else if (b.type == QLatin1String("footnote"))
        prefix = QStringLiteral("fn");
    else if (b.type == QLatin1String("endnote"))
        prefix = QStringLiteral("en");
    else if (b.type == QLatin1String("toc"))
        prefix = QStringLiteral("toc");
    else if (b.type == QLatin1String("frame"))
        prefix = QStringLiteral("fr");
    else if (b.type == QLatin1String("shape"))
        prefix = QStringLiteral("sh");
    else if (b.type == QLatin1String("chart"))
        prefix = QStringLiteral("ch");
    else if (b.type == QLatin1String("qr"))
        prefix = QStringLiteral("qr");
    else if (b.type == QLatin1String("equation"))
        prefix = QStringLiteral("eq");
    else if (b.type == QLatin1String("form"))
        prefix = QStringLiteral("fm");
    else if (b.type == QLatin1String("biblio"))
        prefix = QStringLiteral("bib");
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

QString formatPageNumber(int n, const QString &format)
{
    n = qMax(1, n);
    const QString f = format.toLower();
    if (f == QLatin1String("roman")) {
        static const int vals[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
        static const char *syms[] = {"m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"};
        QString out;
        int x = qMin(n, 3999);
        for (int i = 0; i < 13; ++i) {
            while (x >= vals[i]) {
                out += QLatin1String(syms[i]);
                x -= vals[i];
            }
        }
        return out;
    }
    if (f == QLatin1String("alpha")) {
        QString out;
        int x = n;
        while (x > 0) {
            --x;
            out.prepend(QChar(QLatin1Char('a' + (x % 26))));
            x /= 26;
        }
        return out;
    }
    return QString::number(n);
}

static QString headingNumberOf(const Document &doc, const QString &blockId)
{
    int counters[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (const auto &b : doc.body) {
        if (b.type != QLatin1String("heading"))
            continue;
        const int lvl = qBound(1, b.level > 0 ? b.level : 1, 9);
        ++counters[lvl - 1];
        for (int i = 0; i < lvl - 1; ++i) {
            if (counters[i] == 0)
                counters[i] = 1;
        }
        for (int i = lvl; i < 9; ++i)
            counters[i] = 0;
        if (b.id == blockId) {
            QStringList parts;
            for (int i = 0; i < lvl; ++i)
                parts << QString::number(counters[i]);
            return parts.join(QLatin1Char('.'));
        }
    }
    return {};
}

static int noteOrdinalOf(const Document &doc, const QString &noteId, bool endnote)
{
    int n = 0;
    for (const auto &b : doc.body) {
        for (const auto &run : b.runs) {
            if (run.noteId.isEmpty())
                continue;
            const Block *nb = doc.findBlock(run.noteId);
            if (!nb)
                continue;
            const bool isEnd = nb->type == QLatin1String("endnote");
            if (isEnd != endnote)
                continue;
            ++n;
            if (run.noteId == noteId)
                return n;
        }
    }
    return 0;
}

static int categoryOrdinalOf(const Document &doc, const QString &blockId)
{
    const Block *target = doc.findBlock(blockId);
    if (!target)
        return 0;
    static const QSet<QString> kCategories = {QStringLiteral("image"), QStringLiteral("table"),
                                               QStringLiteral("chart"), QStringLiteral("equation"),
                                               QStringLiteral("qr"), QStringLiteral("form")};
    if (!kCategories.contains(target->type))
        return 0;
    int n = 0;
    for (const auto &b : doc.body) {
        if (b.type != target->type)
            continue;
        ++n;
        if (b.id == blockId)
            return n;
    }
    return 0;
}

static QJsonObject seqFieldJson(const TextRun &r)
{
    const auto doc = QJsonDocument::fromJson(r.fieldArg.toUtf8());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

static QString seqFieldName(const TextRun &r)
{
    return seqFieldJson(r).value(QStringLiteral("name")).toString();
}

// Hidden paragraphs (section or hideIf) leave the layout, the PDF, and these counts (D3).
// hideIf is decided on the unfiltered field context, so this does not recurse.
static bool blockVisibleForStat(const Document &doc, const Block &b, int page, int pages)
{
    if (b.sectionHidden)
        return false;
    return !blockHiddenByExpr(doc, b, page, pages);
}

static int statCount(const Document &doc, const QString &kind, int page, int pages)
{
    if (kind == QLatin1String("pages"))
        return qMax(1, pages);
    if (kind == QLatin1String("words"))
        return doc.wordCount();
    if (kind == QLatin1String("chars")) {
        int n = 0;
        for (const auto &b : doc.body) {
            if (!blockVisibleForStat(doc, b, page, pages))
                continue;
            n += b.plainText().size();
        }
        return n;
    }
    int n = 0;
    for (const auto &b : doc.body) {
        if (!blockVisibleForStat(doc, b, page, pages))
            continue;
        if (kind == QLatin1String("paragraphs") && b.type == QLatin1String("paragraph"))
            ++n;
        else if (kind == QLatin1String("tables") && b.type == QLatin1String("table"))
            ++n;
        else if (kind == QLatin1String("images") && b.type == QLatin1String("image"))
            ++n;
    }
    return n;
}

static void forEachFieldRun(const Document &doc,
                            const std::function<bool(const Block &, const TextRun &, const TextRun *)> &fn)
{
    for (const auto &block : doc.body) {
        for (const auto &run : block.runs) {
            if (fn(block, run, &run))
                return;
        }
        for (const auto &run : block.captionRuns) {
            if (fn(block, run, &run))
                return;
        }
        if (block.type == QLatin1String("table")) {
            for (const auto &row : block.table) {
                for (const auto &cell : row) {
                    for (const auto &run : cell.runs) {
                        if (fn(block, run, &run))
                            return;
                    }
                }
            }
        }
    }
}

static int seqOrdinal(const Document &doc, const QString &seqName, const TextRun *target)
{
    if (seqName.isEmpty() || !target)
        return 0;
    int n = 0;
    int found = 0;
    forEachFieldRun(doc, [&](const Block &, const TextRun &run, const TextRun *ptr) {
        if (run.field != QLatin1String("seq"))
            return false;
        if (seqFieldName(run) != seqName)
            return false;
        ++n;
        if (ptr == target)
            found = n;
        return false;
    });
    if (found == 0 && target && target->field == QLatin1String("seq") && seqFieldName(*target) == seqName)
        return n + 1;
    return found;
}

static int seqNumberOfBlock(const Document &doc, const QString &blockId)
{
    const Block *target = doc.findBlock(blockId);
    if (!target)
        return 0;
    int found = 0;
    forEachFieldRun(doc, [&](const Block &block, const TextRun &run, const TextRun *ptr) {
        if (block.id != blockId || run.field != QLatin1String("seq"))
            return false;
        found = seqOrdinal(doc, seqFieldName(run), ptr);
        return true;
    });
    if (found > 0)
        return found;
    return categoryOrdinalOf(doc, blockId);
}

bool blockHasCaption(const Block &b)
{
    return !b.caption.isEmpty() || !b.captionRuns.isEmpty();
}

QString captionDisplayText(const Document &doc, const Block &b, int page, int pages)
{
    if (b.captionRuns.isEmpty())
        return b.caption;
    QString t;
    for (const auto &r : b.captionRuns)
        t += runDisplayText(doc, r, page, pages, &r);
    return t;
}

QString runDisplayText(const Document &doc, const TextRun &r, int page, int pages, const TextRun *fieldInstance)
{
    if (r.hidden)
        return {};
    if (!r.showIf.isEmpty()) {
        if (showIfIsBareName(r.showIf)) {
            const QString v = doc.vars.value(r.showIf);
            if (v.isEmpty() || v == QLatin1String("0") || v == QLatin1String("false"))
                return {};
        } else {
            const FieldExprResult ev = FieldExpr::evaluate(r.showIf, doc, fieldContextFor(doc, page, pages));
            if (!ev.ok)
                return QStringLiteral("#ERR");
            if (!fieldExprIsTruthy(ev.value))
                return {};
        }
    }
    QString t;
    if (r.redact)
        t = QString(qMax(1, r.text.size()), QChar(0x2588));
    else if (r.field == QLatin1String("page_number"))
        t = formatPageNumber(qMax(1, page), doc.page.numberFormat);
    else if (r.field == QLatin1String("page_count"))
        t = QString::number(qMax(1, pages));
    else if (r.field == QLatin1String("date")) {
        const QString fmt = r.fieldArg.isEmpty() ? QStringLiteral("dd/MM/yyyy") : r.fieldArg;
        t = QDateTime::currentDateTime().toString(fmt);
    } else if (r.field == QLatin1String("title"))
        t = doc.title;
    else if (r.field == QLatin1String("author"))
        t = doc.author.isEmpty() ? QStringLiteral("—") : doc.author;
    else if (r.field == QLatin1String("filename") || r.field == QLatin1String("file_name")
             || r.field == QLatin1String("filepath") || r.field == QLatin1String("file_path")
             || r.field == QLatin1String("path"))
        t = doc.fileName.isEmpty() ? doc.title : doc.fileName;
    else if (r.field == QLatin1String("word_count"))
        t = QString::number(doc.wordCount());
    else if (r.field == QLatin1String("chapter")) {
        for (const auto &b : doc.body) {
            if (b.type == QLatin1String("heading") && b.level <= 1)
                t = b.plainText();
        }
        if (t.isEmpty())
            t = doc.title;
    } else if (r.field == QLatin1String("var") || r.field == QLatin1String("variable")
               || r.field == QLatin1String("custom")) {
        if (r.field == QLatin1String("custom") && !doc.vars.contains(r.fieldArg))
            t = r.fieldArg.isEmpty() ? r.text : r.fieldArg;
        else
            t = doc.vars.value(r.fieldArg, r.fieldArg.isEmpty() ? r.text : r.fieldArg);
    } else if (r.field == QLatin1String("input"))
        t = r.fieldArg.isEmpty() ? (r.text.isEmpty() ? QStringLiteral("[…]") : r.text) : r.fieldArg;
    else if (r.field == QLatin1String("merge") || r.field == QLatin1String("mail_merge")) {
        const QString key = r.fieldArg.isEmpty() ? r.text : r.fieldArg;
        t = key.isEmpty() ? QStringLiteral("«merge»") : QStringLiteral("«%1»").arg(key);
    } else if (r.field == QLatin1String("time"))
        t = QDateTime::currentDateTime().toString(r.fieldArg.isEmpty() ? QStringLiteral("HH:mm") : r.fieldArg);
    else if (r.field == QLatin1String("cite") || r.field == QLatin1String("citation")) {
        const QString key = r.fieldArg.isEmpty() ? r.text : r.fieldArg;
        for (const auto &e : doc.bibliography) {
            if (e.id == key || e.key == key) {
                t = csl::inText(e, doc.citeStyle);
                break;
            }
        }
        if (t.isEmpty())
            t = QStringLiteral("[") + key + QLatin1Char(']');
    } else if (r.field == QLatin1String("doc_info")) {
        const QString key = r.fieldArg;
        if (key == QLatin1String("subject"))
            t = doc.subject;
        else if (key == QLatin1String("keywords"))
            t = doc.keywords;
        else if (key == QLatin1String("created"))
            t = doc.vars.value(QStringLiteral("doc.created"),
                               QDateTime::currentDateTime().toString(Qt::ISODate));
        else if (key == QLatin1String("modified"))
            t = doc.vars.value(QStringLiteral("doc.modified"),
                               QDateTime::currentDateTime().toString(Qt::ISODate));
        else if (key == QLatin1String("revision"))
            t = QString::number(qMax(1, doc.revisions.size()));
    } else if (r.field == QLatin1String("stat")) {
        t = QString::number(statCount(doc, r.fieldArg, page, pages));
    } else if (r.field == QLatin1String("sender")) {
        t = doc.vars.value(QStringLiteral("sender.") + r.fieldArg);
    } else if (r.field == QLatin1String("seq")) {
        const QJsonObject jo = seqFieldJson(r);
        const QString name = jo.value(QStringLiteral("name")).toString();
        const QString format = jo.value(QStringLiteral("format")).toString(QStringLiteral("arabic"));
        const int ord = seqOrdinal(doc, name, fieldInstance);
        t = ord > 0 ? formatPageNumber(ord, format) : QStringLiteral("?");
    } else if (r.field == QLatin1String("input_list")) {
        const QJsonObject jo = QJsonDocument::fromJson(r.fieldArg.toUtf8()).object();
        const QString sel = jo.value(QStringLiteral("selected")).toString();
        if (!sel.isEmpty())
            t = sel;
        else {
            const QJsonArray opts = jo.value(QStringLiteral("options")).toArray();
            t = opts.isEmpty() ? QStringLiteral("—") : opts.at(0).toString();
        }
    } else if (r.field == QLatin1String("xref")) {
        if (r.xrefKind == QLatin1String("heading_number")) {
            t = headingNumberOf(doc, r.fieldArg);
        } else if (r.xrefKind == QLatin1String("footnote_number")) {
            const int n = noteOrdinalOf(doc, r.fieldArg, false);
            t = n > 0 ? QString::number(n) : QString();
        } else if (r.xrefKind == QLatin1String("endnote_number")) {
            const int n = noteOrdinalOf(doc, r.fieldArg, true);
            t = n > 0 ? QString::number(n) : QString();
        } else if (r.xrefKind == QLatin1String("category_number")) {
            const int n = seqNumberOfBlock(doc, r.fieldArg);
            t = n > 0 ? QString::number(n) : QString();
        }
        // "page" is resolved by the layout-aware caller (Preview.cpp); falls through to the
        // target-text lookup below as a placeholder when no layout is available (export, insert time).
        if (t.isEmpty()) {
            const Block *tgt = doc.findBlock(r.fieldArg);
            if (tgt) {
                if (tgt->type == QLatin1String("image") || tgt->type == QLatin1String("table")
                    || tgt->type == QLatin1String("equation") || tgt->type == QLatin1String("chart")) {
                    const QString cap = captionDisplayText(doc, *tgt, page, pages);
                    t = cap.isEmpty() ? tgt->id : cap;
                }
                else if (tgt->type == QLatin1String("list-item"))
                    t = tgt->plainText().left(48);
                else
                    t = tgt->plainText().left(48);
            } else {
                for (const auto &bm : doc.bookmarks) {
                    if (bm.id == r.fieldArg || bm.name == r.fieldArg) {
                        const Block *b = doc.findBlock(bm.blockId);
                        t = b ? b->plainText().left(48) : bm.name;
                        break;
                    }
                }
                if (t.isEmpty())
                    t = r.fieldArg;
            }
        }
    } else if (r.field == QLatin1String("if")) {
        t = evalIfField(doc, r.fieldArg, page, pages);
    } else {
        t = r.text;
    }
    if (r.caps == QLatin1String("upper"))
        return t.toUpper();
    if (r.caps == QLatin1String("lower"))
        return t.toLower();
    if (r.caps == QLatin1String("title")) {
        QString out;
        bool nextUp = true;
        for (const QChar ch : t) {
            out += nextUp ? ch.toUpper() : ch.toLower();
            nextUp = ch.isSpace() || ch == QLatin1Char('-');
        }
        return out;
    }
    return t;
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
    if (!insert.text.isEmpty() || insert.bold || insert.italic || insert.underline || !insert.field.isEmpty())
        out.push_back(insert);
    out += after;
    if (out.isEmpty())
        out.push_back(TextRun{});
    return out;
}

void applyFormatToRun(TextRun &r, const QJsonObject &fmt)
{
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
    if (fmt.contains(QStringLiteral("strike")))
        r.strike = fmt.value(QStringLiteral("strike")).toBool();
    if (fmt.contains(QStringLiteral("superscript")))
        r.superscript = fmt.value(QStringLiteral("superscript")).toBool();
    if (fmt.contains(QStringLiteral("subscript")))
        r.subscript = fmt.value(QStringLiteral("subscript")).toBool();
    if (fmt.contains(QStringLiteral("link")))
        r.link = fmt.value(QStringLiteral("link")).toString();
    if (fmt.contains(QStringLiteral("small_caps")))
        r.smallCaps = fmt.value(QStringLiteral("small_caps")).toBool();
    if (fmt.contains(QStringLiteral("caps")))
        r.caps = fmt.value(QStringLiteral("caps")).toString();
    if (fmt.contains(QStringLiteral("hidden")))
        r.hidden = fmt.value(QStringLiteral("hidden")).toBool();
    if (fmt.contains(QStringLiteral("char_style")))
        r.charStyle = fmt.value(QStringLiteral("char_style")).toString();
    if (fmt.contains(QStringLiteral("lang")))
        r.lang = fmt.value(QStringLiteral("lang")).toString();
    if (fmt.contains(QStringLiteral("kerning")))
        r.kerning = fmt.value(QStringLiteral("kerning")).toDouble();
    if (fmt.contains(QStringLiteral("ruby")))
        r.ruby = fmt.value(QStringLiteral("ruby")).toString();
    if (fmt.contains(QStringLiteral("redact")))
        r.redact = fmt.value(QStringLiteral("redact")).toBool();
    if (fmt.contains(QStringLiteral("show_if")))
        r.showIf = fmt.value(QStringLiteral("show_if")).toString();
}

void clearRunFormat(TextRun &r)
{
    r.bold = false;
    r.italic = false;
    r.underline = false;
    r.strike = false;
    r.superscript = false;
    r.subscript = false;
    r.font.clear();
    r.sizePt = 0;
    r.color.clear();
    r.highlight.clear();
    r.smallCaps = false;
    r.caps.clear();
    r.hidden = false;
    r.charStyle.clear();
    r.lang.clear();
    r.kerning = 0;
    r.link.clear();
}

QJsonObject runFormatJson(const TextRun &r)
{
    QJsonObject o;
    o[QStringLiteral("bold")] = r.bold;
    o[QStringLiteral("italic")] = r.italic;
    o[QStringLiteral("underline")] = r.underline;
    o[QStringLiteral("strike")] = r.strike;
    o[QStringLiteral("superscript")] = r.superscript;
    o[QStringLiteral("subscript")] = r.subscript;
    o[QStringLiteral("font")] = r.font;
    o[QStringLiteral("size_pt")] = r.sizePt;
    o[QStringLiteral("color")] = r.color;
    o[QStringLiteral("highlight")] = r.highlight;
    o[QStringLiteral("small_caps")] = r.smallCaps;
    o[QStringLiteral("caps")] = r.caps;
    o[QStringLiteral("hidden")] = r.hidden;
    o[QStringLiteral("char_style")] = r.charStyle;
    o[QStringLiteral("lang")] = r.lang;
    o[QStringLiteral("kerning")] = r.kerning;
    o[QStringLiteral("link")] = r.link;
    return o;
}

void applyFormatToRange(QVector<TextRun> &runs, int start, int end, const QJsonObject &fmt)
{
    const QString plain = runsPlain(runs);
    const int s = qBound(0, start, plain.size());
    const int e = qBound(s, end, plain.size());
    if (s == e && fmt.isEmpty())
        return;
    QVector<TextRun> before = splitRuns(runs, 0, s);
    QVector<TextRun> mid = splitRuns(runs, s, e);
    QVector<TextRun> after = splitRuns(runs, e, plain.size());
    for (auto &r : mid)
        applyFormatToRun(r, fmt);
    runs = before + mid + after;
    if (runs.isEmpty())
        runs.push_back(TextRun{});
}

} // namespace aioffice
