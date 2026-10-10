#include "core/Docx.h"

#include "core/Xml.h"
#include "core/Zip.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>

namespace aioffice {
namespace {

QString wAlign(const QString &a)
{
    if (a == QLatin1String("center"))
        return QStringLiteral("center");
    if (a == QLatin1String("right"))
        return QStringLiteral("right");
    if (a == QLatin1String("start"))
        return QStringLiteral("start");
    if (a == QLatin1String("end"))
        return QStringLiteral("end");
    if (a == QLatin1String("justify"))
        return QStringLiteral("both");
    return QStringLiteral("left");
}

struct DocxOut {
    QMap<QString, int> fnIds;
    QMap<QString, int> enIds;
    QMap<QString, QString> linkRels;
    int relN = 2;
    QMap<QString, QString> bmNames; // original id/blockId/name -> exported w:name
    QSet<QString> exportedNames;    // w:name values actually written
    QSet<QString> syntheticBmBlockIds;
    QMap<QString, int> revWId;
    int nextWId = 1;

    int wId(const QString &revId)
    {
        auto it = revWId.find(revId);
        if (it != revWId.end())
            return it.value();
        const int n = nextWId++;
        revWId.insert(revId, n);
        return n;
    }
    QString bm(const QString &key) const
    {
        auto it = bmNames.constFind(key);
        return it == bmNames.cend() ? key : it.value();
    }
};

QString exportedBookmarkName(const QString &raw, int idx)
{
    QString name = raw;
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    if (name.isEmpty() || name[0].isDigit())
        name.prepend(QLatin1Char('_'));
    return name.left(40) + QStringLiteral("_%1").arg(idx);
}

void fillBookmarkNames(DocxOut &o, const Document &doc)
{
    int idx = 0;
    auto remember = [&](const QString &key, const QString &name) {
        if (!key.isEmpty())
            o.bmNames.insert(key, name);
    };
    for (const auto &bm : doc.bookmarks) {
        ++idx;
        const QString name = exportedBookmarkName(bm.name.isEmpty() ? bm.id : bm.name, idx);
        o.exportedNames.insert(name);
        remember(bm.id, name);
        remember(bm.blockId, name);
        remember(bm.name, name);
    }
    for (const auto &b : doc.body) {
        for (const auto &r : b.runs) {
            if (r.field != QLatin1String("xref") || r.fieldArg.isEmpty())
                continue;
            if (o.bmNames.contains(r.fieldArg))
                continue;
            ++idx;
            const QString name = exportedBookmarkName(r.fieldArg, idx);
            o.exportedNames.insert(name);
            remember(r.fieldArg, name);
            o.syntheticBmBlockIds.insert(r.fieldArg);
        }
    }
}

QString runRPr(const TextRun &r)
{
    QString rPr;
    if (r.bold)
        rPr += QLatin1String("<w:b/>");
    if (r.italic)
        rPr += QLatin1String("<w:i/>");
    if (r.underline)
        rPr += QLatin1String("<w:u w:val=\"single\"/>");
    if (r.strike)
        rPr += QLatin1String("<w:strike/>");
    if (r.superscript)
        rPr += QLatin1String("<w:vertAlign w:val=\"superscript\"/>");
    if (r.subscript)
        rPr += QLatin1String("<w:vertAlign w:val=\"subscript\"/>");
    if (r.sizePt > 0)
        rPr += QStringLiteral("<w:sz w:val=\"%1\"/><w:szCs w:val=\"%1\"/>").arg(int(r.sizePt * 2));
    if (!r.font.isEmpty())
        rPr += QStringLiteral("<w:rFonts w:ascii=\"%1\" w:hAnsi=\"%1\"/>").arg(xmlEscape(r.font));
    if (!r.color.isEmpty()) {
        QString c = r.color;
        c.remove(QLatin1Char('#'));
        rPr += QStringLiteral("<w:color w:val=\"%1\"/>").arg(c);
    }
    if (!r.highlight.isEmpty())
        rPr += QLatin1String("<w:highlight w:val=\"yellow\"/>");
    return rPr;
}

// rPrExtra is appended last inside w:rPr (CT_RPr: w:rPrChange is the last child).
// asDeleted emits w:delText instead of w:t (content of a w:del).
QString runsToWml(const QVector<TextRun> &runs, const Document &doc, DocxOut &o,
                  const QString &rPrExtra = QString(), bool asDeleted = false)
{
    QString xml;
    for (const auto &r : runs) {
        QString rPr = runRPr(r);
        if (!rPrExtra.isEmpty())
            rPr += rPrExtra;
        if (!r.field.isEmpty()) {
            QString instr = r.field;
            if (r.field == QLatin1String("page_number"))
                instr = QStringLiteral(" PAGE ");
            else if (r.field == QLatin1String("page_count"))
                instr = QStringLiteral(" NUMPAGES ");
            else if (r.field == QLatin1String("date"))
                instr = QStringLiteral(" DATE ");
            else if (r.field == QLatin1String("title"))
                instr = QStringLiteral(" TITLE ");
            else if (r.field == QLatin1String("author"))
                instr = QStringLiteral(" AUTHOR ");
            else if (r.field == QLatin1String("filename") || r.field == QLatin1String("file_name"))
                instr = QStringLiteral(" FILENAME ");
            else if (r.field == QLatin1String("word_count"))
                instr = QStringLiteral(" NUMWORDS ");
            else if (r.field == QLatin1String("xref")) {
                const QString target = o.bm(r.fieldArg);
                if (r.xrefKind == QLatin1String("page"))
                    instr = QStringLiteral(" PAGEREF %1 ").arg(target);
                else if (r.xrefKind == QLatin1String("heading_number")
                         || r.xrefKind == QLatin1String("category_number"))
                    instr = QStringLiteral(" REF %1 \\n ").arg(target);
                else
                    instr = QStringLiteral(" REF %1 ").arg(target);
            } else if (r.field == QLatin1String("merge") || r.field == QLatin1String("mail_merge"))
                instr = QStringLiteral(" MERGEFIELD %1 ").arg(r.fieldArg);
            else if (r.field == QLatin1String("chapter"))
                instr = QStringLiteral(" STYLEREF 1 ");
            else if (r.field == QLatin1String("var") || r.field == QLatin1String("variable")
                     || r.field == QLatin1String("custom"))
                instr = QStringLiteral(" DOCVARIABLE %1 ").arg(r.fieldArg);
            else if (r.field == QLatin1String("input"))
                instr = QStringLiteral(" FILLIN \"%1\" ").arg(xmlEscape(r.fieldArg));
            else if (r.field == QLatin1String("time"))
                instr = QStringLiteral(" TIME ");
            else if (r.field == QLatin1String("cite") || r.field == QLatin1String("citation"))
                instr = QStringLiteral(" AIOFFICE_CITE %1 ").arg(r.fieldArg);
            const QString vis = xmlEscape(runDisplayText(doc, r));
            xml += QStringLiteral("<w:fldSimple w:instr=\"%1\"><w:r><w:t>%2</w:t></w:r></w:fldSimple>")
                       .arg(xmlEscape(instr), vis);
        } else {
            QString t = xmlEscape(r.text);
            const QString space = r.text.startsWith(QLatin1Char(' ')) || r.text.endsWith(QLatin1Char(' '))
                ? QStringLiteral(" xml:space=\"preserve\"")
                : QString();
            QString runXml = QStringLiteral("<w:r>");
            if (!rPr.isEmpty())
                runXml += QStringLiteral("<w:rPr>%1</w:rPr>").arg(rPr);
            if (asDeleted)
                runXml += QStringLiteral("<w:delText xml:space=\"preserve\">%1</w:delText></w:r>").arg(t);
            else
                runXml += QStringLiteral("<w:t%1>%2</w:t></w:r>").arg(space, t);
            if (!r.link.isEmpty()) {
                if (r.link.startsWith(QLatin1Char('#'))) {
                    runXml = QStringLiteral("<w:hyperlink w:anchor=\"%1\">%2</w:hyperlink>")
                                 .arg(xmlEscape(o.bm(r.link.mid(1))), runXml);
                } else {
                    QString relId = o.linkRels.value(r.link);
                    if (relId.isEmpty()) {
                        ++o.relN;
                        relId = QStringLiteral("rId%1").arg(o.relN);
                        o.linkRels[r.link] = relId;
                    }
                    runXml = QStringLiteral("<w:hyperlink r:id=\"%1\">%2</w:hyperlink>").arg(relId, runXml);
                }
            }
            xml += runXml;
        }
        if (!asDeleted && !r.noteId.isEmpty()) {
            const Block *nb = doc.findBlock(r.noteId);
            if (nb && nb->type == QLatin1String("footnote")) {
                int id = o.fnIds.value(r.noteId, 0);
                if (id == 0) {
                    id = o.fnIds.size() + 1;
                    o.fnIds[r.noteId] = id;
                }
                xml += QStringLiteral(
                    "<w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:footnoteReference w:id=\"%1\"/></w:r>")
                           .arg(id);
            } else if (nb && nb->type == QLatin1String("endnote")) {
                int id = o.enIds.value(r.noteId, 0);
                if (id == 0) {
                    id = o.enIds.size() + 1;
                    o.enIds[r.noteId] = id;
                }
                xml += QStringLiteral(
                    "<w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:endnoteReference w:id=\"%1\"/></w:r>")
                           .arg(id);
            }
        }
    }
    return xml;
}

// D3: insert wraps live text in one w:ins; delete is a w:del at `start` built from deletedRuns
// (the edit model has already removed that text from `runs`); format adds w:rPrChange as the
// last child of each covered run's w:rPr, with the previous w:rPr taken from priorRuns.
struct RevEv {
    int pos = 0;
    int ord = 0; // 0 close, 1 delete point, 2 open — same order LibreOffice writes at one offset
    int seq = 0;
    const Revision *rv = nullptr;
    bool open = false;
    bool point = false;
};

QString revDate(const Revision *rv)
{
    return QDateTime::fromMSecsSinceEpoch(rv->timestamp).toString(Qt::ISODate);
}

QString wrapIns(const Revision *rv, const QString &inner, DocxOut &o)
{
    return QStringLiteral("<w:ins w:id=\"%1\" w:author=\"%2\" w:date=\"%3\">%4</w:ins>")
        .arg(o.wId(rv->id))
        .arg(xmlEscape(rv->author), revDate(rv), inner);
}

QString emitDel(const Revision *rv, const Document &doc, DocxOut &o)
{
    QVector<TextRun> gone = rv->deletedRuns;
    if (gone.isEmpty() && !rv->text.isEmpty()) {
        TextRun fallback;
        fallback.text = rv->text;
        gone.push_back(fallback);
    }
    const QString inner = runsToWml(gone, doc, o, QString(), true);
    return QStringLiteral("<w:del w:id=\"%1\" w:author=\"%2\" w:date=\"%3\">%4</w:del>")
        .arg(o.wId(rv->id))
        .arg(xmlEscape(rv->author), revDate(rv), inner);
}

QString rPrChangeXml(const Revision *fmt, int absOff, int len, DocxOut &o)
{
    QString prior;
    if (!fmt->priorRuns.isEmpty()) {
        const auto slices = splitRuns(fmt->priorRuns, absOff - fmt->start, absOff - fmt->start + len);
        if (!slices.isEmpty())
            prior = runRPr(slices.front());
    }
    const int wid = o.wId(QStringLiteral("%1#%2").arg(fmt->id).arg(absOff));
    return QStringLiteral("<w:rPrChange w:id=\"%1\" w:author=\"%2\" w:date=\"%3\"><w:rPr>%4</w:rPr></w:rPrChange>")
        .arg(wid)
        .arg(xmlEscape(fmt->author), revDate(fmt), prior);
}

QString revisedRunsToWml(const QString &blockId, const QVector<TextRun> &runs, const Document &doc, DocxOut &o)
{
    int total = 0;
    for (const auto &r : runs)
        total += r.text.size();
    QVector<RevEv> evs;
    int seq = 0;
    for (const auto &rv : doc.revisions) {
        if (rv.blockId != blockId)
            continue;
        if (rv.kind == QLatin1String("insert") || rv.kind == QLatin1String("format")) {
            const int s = qBound(0, rv.start, total);
            const int e = qBound(s, rv.end, total);
            evs.push_back(RevEv{s, 2, seq++, &rv, true, false});
            evs.push_back(RevEv{e, 0, seq++, &rv, false, false});
        } else if (rv.kind == QLatin1String("delete")) {
            const int s = qBound(0, rv.start, total);
            evs.push_back(RevEv{s, 1, seq++, &rv, false, true});
        }
    }
    std::sort(evs.begin(), evs.end(), [](const RevEv &a, const RevEv &b) {
        if (a.pos != b.pos)
            return a.pos < b.pos;
        if (a.ord != b.ord)
            return a.ord < b.ord;
        return a.seq < b.seq;
    });

    struct Piece {
        bool del = false;
        int from = 0;
        int to = 0;
        const Revision *delRv = nullptr;
        const Revision *insert = nullptr;
        QVector<const Revision *> formats;
    };
    QVector<Piece> pieces;
    int cursor = 0;
    const Revision *insert = nullptr;
    QVector<const Revision *> formats;
    auto flushText = [&](int to) {
        if (to <= cursor)
            return;
        Piece p;
        p.from = cursor;
        p.to = to;
        p.insert = insert;
        p.formats = formats;
        pieces.push_back(p);
        cursor = to;
    };
    for (const RevEv &ev : evs) {
        if (ev.pos > cursor)
            flushText(ev.pos);
        if (ev.point) {
            Piece p;
            p.del = true;
            p.delRv = ev.rv;
            p.insert = insert;
            pieces.push_back(p);
        } else if (ev.open) {
            if (ev.rv->kind == QLatin1String("insert"))
                insert = ev.rv;
            else
                formats.push_back(ev.rv);
        } else if (ev.rv->kind == QLatin1String("insert")) {
            if (insert == ev.rv)
                insert = nullptr;
        } else {
            formats.erase(std::remove(formats.begin(), formats.end(), ev.rv), formats.end());
        }
    }
    if (cursor < total)
        flushText(total);

    auto emitText = [&](const Piece &p) {
        QString xml;
        const Revision *fmt = p.formats.isEmpty() ? nullptr : p.formats.back();
        if (!fmt) {
            xml += runsToWml(splitRuns(runs, p.from, p.to), doc, o);
            return xml;
        }
        int abs = p.from;
        for (const auto &r : splitRuns(runs, p.from, p.to)) {
            const QString extra = rPrChangeXml(fmt, abs, r.text.size(), o);
            xml += runsToWml(QVector<TextRun>{r}, doc, o, extra, false);
            abs += r.text.size();
        }
        return xml;
    };

    QString out;
    for (int i = 0; i < pieces.size();) {
        const Revision *ins = pieces[i].insert;
        if (!ins) {
            out += pieces[i].del ? emitDel(pieces[i].delRv, doc, o) : emitText(pieces[i]);
            ++i;
            continue;
        }
        QString inner;
        while (i < pieces.size() && pieces[i].insert == ins) {
            inner += pieces[i].del ? emitDel(pieces[i].delRv, doc, o) : emitText(pieces[i]);
            ++i;
        }
        out += wrapIns(ins, inner, o);
    }
    return out;
}

QString bookmarksAroundWml(const Document &doc, const QString &blockId, const QString &inner, DocxOut &o)
{
    QString out = inner;
    QSet<QString> done;
    int idx = 0;
    auto wrap = [&](const QString &key) {
        const QString name = o.bm(key);
        if (name.isEmpty() || done.contains(name))
            return;
        done.insert(name);
        ++idx;
        out = QStringLiteral("<w:bookmarkStart w:id=\"%1\" w:name=\"%2\"/>%3<w:bookmarkEnd w:id=\"%1\"/>")
                  .arg(idx)
                  .arg(xmlEscape(name), out);
    };
    for (const auto &bm : doc.bookmarks) {
        if (bm.blockId == blockId)
            wrap(bm.id);
    }
    if (o.syntheticBmBlockIds.contains(blockId))
        wrap(blockId);
    return out;
}

QString pPr(const Block &b)
{
    QString xml;
    if (b.type == QLatin1String("heading")) {
        const int lv = b.level > 0 ? b.level : 1;
        xml += QStringLiteral("<w:pStyle w:val=\"Heading%1\"/>").arg(lv);
    } else if (b.style == QLatin1String("Title")) {
        xml += QLatin1String("<w:pStyle w:val=\"Title\"/>");
    } else if (b.style == QLatin1String("Quote")) {
        xml += QLatin1String("<w:pStyle w:val=\"Quote\"/>");
    }
    // EG_PPrBase order: pStyle, numPr, bidi, then jc. Strict OOXML rejects any other sequence.
    if (b.type == QLatin1String("list-item")) {
        const int numId = b.listKind == QLatin1String("decimal") ? 1 : 2;
        xml += QStringLiteral("<w:numPr><w:ilvl w:val=\"%1\"/><w:numId w:val=\"%2\"/></w:numPr>")
                   .arg(qMax(0, b.level - 1))
                   .arg(numId);
    }
    if (b.dir == QLatin1String("rtl"))
        xml += QLatin1String("<w:bidi/>");
    else if (b.dir == QLatin1String("ltr"))
        xml += QLatin1String("<w:bidi w:val=\"0\"/>");
    if (!b.align.isEmpty())
        xml += QStringLiteral("<w:jc w:val=\"%1\"/>").arg(wAlign(b.align));
    if (xml.isEmpty())
        return {};
    return QStringLiteral("<w:pPr>%1</w:pPr>").arg(xml);
}

int twipsMm(double mm) { return int(std::lround(mm * 1440.0 / 25.4)); }
double mmTwips(int tw) { return tw * 25.4 / 1440.0; }

} // namespace

bool saveDocx(const Document &doc, const QString &path, QString *error, bool embedJson)
{
    double wMm = 0, hMm = 0;
    doc.page.pageSizeMm(wMm, hMm);
    const int pgW = twipsMm(wMm);
    const int pgH = twipsMm(hMm);
    const int mt = twipsMm(doc.page.marginsMm[0]);
    const int mr = twipsMm(doc.page.marginsMm[1]);
    const int mb = twipsMm(doc.page.marginsMm[2]);
    const int ml = twipsMm(doc.page.marginsMm[3]);
    const QString orient = doc.page.orientation == QLatin1String("landscape")
        ? QStringLiteral(" w:orient=\"landscape\"")
        : QString();

    QString body;
    QVector<ZipEntry> media;
    QString extraRels;
    QString extraTypes;
    int imgN = 0;
    DocxOut o;
    fillBookmarkNames(o, doc);
    for (const auto &b : doc.body) {
        if (b.type == QLatin1String("footnote") || b.type == QLatin1String("endnote"))
            continue;
        if (b.type == QLatin1String("table")) {
            body += QLatin1String("<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/></w:tblPr>");
            for (const auto &row : b.table) {
                body += QLatin1String("<w:tr>");
                for (const auto &cell : row)
                    body += QStringLiteral("<w:tc><w:p>%1</w:p></w:tc>")
                                .arg(runsToWml(cell.runs, doc, o));
                body += QLatin1String("</w:tr>");
            }
            body += QLatin1String("</w:tbl>");
            continue;
        }
        if (b.type == QLatin1String("break")) {
            const QString br = b.breakKind == QLatin1String("column")
                ? QStringLiteral("<w:br w:type=\"column\"/>")
                : QStringLiteral("<w:br w:type=\"page\"/>");
            body += QStringLiteral("<w:p><w:r>%1</w:r></w:p>").arg(br);
            continue;
        }
        if (b.type == QLatin1String("toc")) {
            QString items;
            for (const auto &tid : b.tocTargets) {
                const Block *t = doc.findBlock(tid);
                items += QStringLiteral("<w:p><w:pPr><w:pStyle w:val=\"TOC1\"/></w:pPr><w:r><w:t>%1</w:t></w:r></w:p>")
                             .arg(xmlEscape(t ? t->plainText() : QString()));
            }
            if (items.isEmpty())
                items = QStringLiteral("<w:p><w:r><w:t>%1</w:t></w:r></w:p>").arg(xmlEscape(b.plainText()));
            body += QStringLiteral("<w:sdt><w:sdtPr><w:docPartObj><w:docPartGallery w:val=\"Table of Contents\"/>"
                                   "</w:docPartObj></w:sdtPr><w:sdtContent>%1</w:sdtContent></w:sdt>")
                        .arg(items);
            continue;
        }
        if (b.type == QLatin1String("image")) {
            ++imgN;
            ++o.relN;
            const QString rid = QStringLiteral("rId%1").arg(o.relN);
            const QString name = QStringLiteral("image%1.png").arg(imgN);
            QByteArray bytes = b.imageBytes;
            if (bytes.isEmpty() && !b.imagePath.isEmpty()) {
                QFile f(b.imagePath);
                if (f.open(QIODevice::ReadOnly))
                    bytes = f.readAll();
            }
            media.push_back({QStringLiteral("word/media/") + name, bytes, false});
            extraRels += QStringLiteral(
                "<Relationship Id=\"%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
                "Target=\"media/%2\"/>")
                             .arg(rid, name);
            extraTypes = QLatin1String("<Default Extension=\"png\" ContentType=\"image/png\"/>");
            const int cx = int((b.imageWidthMm > 0 ? b.imageWidthMm : 80) * 36000);
            body += QStringLiteral(
                        "<w:p><w:r><w:drawing xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
                        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
                        "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
                        "<wp:inline><wp:extent cx=\"%1\" cy=\"%2\"/>"
                        "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
                        "<pic:pic><pic:blipFill><a:blip r:embed=\"%3\"/></pic:blipFill>"
                        "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%1\" cy=\"%2\"/></a:xfrm></pic:spPr>"
                        "</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>")
                        .arg(cx)
                        .arg(int(cx * 0.6))
                        .arg(rid);
            continue;
        }
        body += bookmarksAroundWml(doc, b.id,
            QStringLiteral("<w:p>%1%2</w:p>")
                .arg(pPr(b), revisedRunsToWml(b.id, b.runs, doc, o)), o);
    }
    QSet<QString> referencedNotes;
    for (const auto &b : doc.body) {
        for (const auto &r : b.runs)
            if (!r.noteId.isEmpty())
                referencedNotes.insert(r.noteId);
    }
    for (const auto &b : doc.body) {
        if ((b.type == QLatin1String("footnote") || b.type == QLatin1String("endnote"))
            && !referencedNotes.contains(b.id)) {
            TextRun fake;
            fake.noteId = b.id;
            body += QStringLiteral("<w:p>%1</w:p>").arg(runsToWml({fake}, doc, o));
        }
    }
    QString colsSect;
    if (doc.page.columns > 1)
        colsSect = QStringLiteral("<w:cols w:num=\"%1\" w:sep=\"%2\"/>")
                       .arg(doc.page.columns)
                       .arg(doc.page.columnSeparator ? 1 : 0);
    body += QStringLiteral("<w:sectPr><w:pgSz w:w=\"%1\" w:h=\"%2\"%3/>"
                           "<w:pgMar w:top=\"%4\" w:right=\"%5\" w:bottom=\"%6\" w:left=\"%7\"/>%8</w:sectPr>")
                .arg(pgW)
                .arg(pgH)
                .arg(orient)
                .arg(mt)
                .arg(mr)
                .arg(mb)
                .arg(ml)
                .arg(colsSect);

    for (auto it = o.linkRels.constBegin(); it != o.linkRels.constEnd(); ++it) {
        extraRels += QStringLiteral(
            "<Relationship Id=\"%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" "
            "Target=\"%2\" TargetMode=\"External\"/>")
                          .arg(it.value(), xmlEscape(it.key()));
    }

    const QString documentXml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<w:body>%1</w:body></w:document>")
                                    .arg(body);

    const QString numbering = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:abstractNum w:abstractNumId=\"0\"><w:multiLevelType w:val=\"hybridMultilevel\"/>"
        "<w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/><w:numFmt w:val=\"decimal\"/><w:lvlText w:val=\"%1.\"/>"
        "<w:lvlJc w:val=\"left\"/></w:lvl></w:abstractNum>"
        "<w:abstractNum w:abstractNumId=\"1\"><w:multiLevelType w:val=\"hybridMultilevel\"/>"
        "<w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/><w:numFmt w:val=\"bullet\"/><w:lvlText w:val=\"•\"/>"
        "<w:lvlJc w:val=\"left\"/></w:lvl></w:abstractNum>"
        "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num>"
        "<w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num>"
        "</w:numbering>");

    const QString rels = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"word/document.xml\"/>"
        "<Relationship Id=\"rId2\" "
        "Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" "
        "Target=\"docProps/core.xml\"/>"
        "</Relationships>");

    if (!o.fnIds.isEmpty()) {
        extraRels += QLatin1String(
            "<Relationship Id=\"rIdFn\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footnotes\" "
            "Target=\"footnotes.xml\"/>");
        extraTypes += QLatin1String(
            "<Override PartName=\"/word/footnotes.xml\" "
            "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footnotes+xml\"/>");
    }
    if (!o.enIds.isEmpty()) {
        extraRels += QLatin1String(
            "<Relationship Id=\"rIdEn\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/endnotes\" "
            "Target=\"endnotes.xml\"/>");
        extraTypes += QLatin1String(
            "<Override PartName=\"/word/endnotes.xml\" "
            "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.endnotes+xml\"/>");
    }
    if (embedJson) {
        extraTypes += QLatin1String(
            "<Override PartName=\"/word/aioffice.json\" ContentType=\"application/json\"/>");
    }

    const QString contentTypes = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "%1"
        "<Override PartName=\"/word/document.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
        "<Override PartName=\"/word/numbering.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>"
        "<Override PartName=\"/docProps/core.xml\" "
        "ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
        "</Types>")
                               .arg(extraTypes);

    const QString docRels = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
        "Target=\"styles.xml\"/>"
        "<Relationship Id=\"rId2\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering\" "
        "Target=\"numbering.xml\"/>"
        "%1</Relationships>")
                                .arg(extraRels);

    const QString styles = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Heading1\"><w:name w:val=\"heading 1\"/><w:basedOn w:val=\"Normal\"/>"
        "<w:pPr><w:outlineLvl w:val=\"0\"/></w:pPr>"
        "<w:rPr><w:b/><w:sz w:val=\"44\"/></w:rPr></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Heading2\"><w:name w:val=\"heading 2\"/>"
        "<w:pPr><w:outlineLvl w:val=\"1\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"32\"/></w:rPr></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Title\"><w:name w:val=\"Title\"/>"
        "<w:rPr><w:b/><w:sz w:val=\"56\"/></w:rPr></w:style>"
        "</w:styles>");

    const QString core = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
        "<dc:title>%1</dc:title></cp:coreProperties>")
                             .arg(xmlEscape(doc.title));

    QJsonObject extra;
    extra[QStringLiteral("schema")] = 1;
    extra[QStringLiteral("track_changes")] = doc.trackChanges;
    QJsonArray bmA;
    for (const auto &bm : doc.bookmarks)
        bmA.append(bm.toJson());
    extra[QStringLiteral("bookmarks")] = bmA;
    QJsonArray rvA;
    for (const auto &rv : doc.revisions)
        rvA.append(rv.toJson());
    extra[QStringLiteral("revisions")] = rvA;

    auto notePart = [&](const QMap<QString, int> &ids, const QString &tag) {
        QString xml = QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<w:%1 xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">").arg(tag);
        QMap<int, QString> byId;
        for (auto it = ids.begin(); it != ids.end(); ++it)
            byId.insert(it.value(), it.key());
        const QString item = tag.endsWith(QLatin1Char('s')) ? tag.chopped(1) : tag;
        for (auto it = byId.begin(); it != byId.end(); ++it) {
            const Block *nb = doc.findBlock(it.value());
            xml += QStringLiteral("<w:%1 w:id=\"%2\"><w:p><w:r><w:t>%3</w:t></w:r></w:p></w:%1>")
                       .arg(item)
                       .arg(it.key())
                       .arg(xmlEscape(nb ? nb->plainText() : QString()));
        }
        xml += QStringLiteral("</w:%1>").arg(tag);
        return xml;
    };

    QVector<ZipEntry> entries;
    entries.push_back({QStringLiteral("[Content_Types].xml"), contentTypes.toUtf8(), false});
    entries.push_back({QStringLiteral("_rels/.rels"), rels.toUtf8(), false});
    entries.push_back({QStringLiteral("word/_rels/document.xml.rels"), docRels.toUtf8(), false});
    entries.push_back({QStringLiteral("word/document.xml"), documentXml.toUtf8(), false});
    entries.push_back({QStringLiteral("word/styles.xml"), styles.toUtf8(), false});
    entries.push_back({QStringLiteral("word/numbering.xml"), numbering.toUtf8(), false});
    if (embedJson)
        entries.push_back({QStringLiteral("word/aioffice.json"), QJsonDocument(extra).toJson(QJsonDocument::Compact), false});
    if (!o.fnIds.isEmpty())
        entries.push_back({QStringLiteral("word/footnotes.xml"), notePart(o.fnIds, QStringLiteral("footnotes")).toUtf8(), false});
    if (!o.enIds.isEmpty())
        entries.push_back({QStringLiteral("word/endnotes.xml"), notePart(o.enIds, QStringLiteral("endnotes")).toUtf8(), false});
    entries.push_back({QStringLiteral("docProps/core.xml"), core.toUtf8(), false});
    for (const auto &m : media)
        entries.push_back(m);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Cannot write ") + path;
        return false;
    }
    f.write(Zip::pack(entries));
    return true;
}

bool loadDocx(const QByteArray &zipData, Document &doc, QString *error)
{
    QVector<ZipEntry> entries;
    if (!Zip::unpack(zipData, entries, error))
        return false;
    const QByteArray xmlBytes = Zip::file(entries, QStringLiteral("word/document.xml"));
    if (xmlBytes.isEmpty()) {
        if (error)
            *error = QStringLiteral("DOCX missing word/document.xml");
        return false;
    }
    doc = Document::blank();
    doc.body.clear();
    // D6: a non-empty revisions array in word/aioffice.json is the truth.
    // Native w:ins / w:del / w:rPrChange must not add a second copy.
    bool jsonOwnsRevisions = false;
    const auto extraBytes = Zip::file(entries, QStringLiteral("word/aioffice.json"));
    if (!extraBytes.isEmpty()) {
        const auto extra = QJsonDocument::fromJson(extraBytes).object();
        doc.trackChanges = extra.value(QStringLiteral("track_changes")).toBool();
        for (const auto &b : extra.value(QStringLiteral("bookmarks")).toArray())
            doc.bookmarks.push_back(Bookmark::fromJson(b.toObject()));
        const QJsonArray jsonRevisions = extra.value(QStringLiteral("revisions")).toArray();
        jsonOwnsRevisions = !jsonRevisions.isEmpty();
        for (const auto &r : jsonRevisions)
            doc.revisions.push_back(Revision::fromJson(r.toObject()));
    }
    QMap<int, QString> footnoteText;
    QMap<int, QString> endnoteText;
    auto loadNotes = [&](const QString &part, QMap<int, QString> *out) {
        const auto bytes = Zip::file(entries, part);
        if (bytes.isEmpty())
            return;
        QXmlStreamReader nx(bytes);
        int cur = -1;
        QString acc;
        while (!nx.atEnd()) {
            nx.readNext();
            if (nx.isStartElement() && (nx.name() == QLatin1String("footnote") || nx.name() == QLatin1String("endnote"))) {
                cur = nx.attributes().value(QStringLiteral("w:id")).toInt();
                acc.clear();
            } else if (nx.isStartElement() && nx.name() == QLatin1String("t"))
                acc += nx.readElementText();
            else if (nx.isEndElement() && (nx.name() == QLatin1String("footnote") || nx.name() == QLatin1String("endnote"))) {
                if (cur >= 0)
                    out->insert(cur, acc.trimmed());
                cur = -1;
            }
        }
    };
    loadNotes(QStringLiteral("word/footnotes.xml"), &footnoteText);
    loadNotes(QStringLiteral("word/endnotes.xml"), &endnoteText);

    QMap<QString, QString> rels;
    const QByteArray relBytes = Zip::file(entries, QStringLiteral("word/_rels/document.xml.rels"));
    if (!relBytes.isEmpty()) {
        QXmlStreamReader rx(relBytes);
        while (!rx.atEnd()) {
            rx.readNext();
            if (rx.isStartElement() && rx.name() == QLatin1String("Relationship")) {
                rels[rx.attributes().value(QStringLiteral("Id")).toString()] =
                    rx.attributes().value(QStringLiteral("Target")).toString();
            }
        }
    }

    QXmlStreamReader sectScan(xmlBytes);
    while (!sectScan.atEnd()) {
        sectScan.readNext();
        if (!sectScan.isStartElement())
            continue;
        if (sectScan.name() == QLatin1String("pgSz")) {
            const int w = sectScan.attributes().value(QStringLiteral("w:w")).toInt();
            const int h = sectScan.attributes().value(QStringLiteral("w:h")).toInt();
            const auto orient = sectScan.attributes().value(QStringLiteral("w:orient")).toString();
            if (w > 0 && h > 0) {
                const double wMm = mmTwips(w);
                const double hMm = mmTwips(h);
                const double shortMm = qMin(wMm, hMm);
                if (qAbs(shortMm - 215.9) < qAbs(shortMm - 210.0))
                    doc.page.size = QStringLiteral("Letter");
                else
                    doc.page.size = QStringLiteral("A4");
                doc.page.orientation = (!orient.isEmpty() ? orient : (w > h ? QStringLiteral("landscape")
                                                                            : QStringLiteral("portrait")));
            }
        } else if (sectScan.name() == QLatin1String("pgMar")) {
            auto tw = [&](const char *k) {
                return mmTwips(sectScan.attributes().value(QLatin1String(k)).toInt());
            };
            if (sectScan.attributes().hasAttribute(QStringLiteral("w:top"))) {
                doc.page.marginsMm[0] = tw("w:top");
                doc.page.marginsMm[1] = tw("w:right");
                doc.page.marginsMm[2] = tw("w:bottom");
                doc.page.marginsMm[3] = tw("w:left");
            }
        } else if (sectScan.name() == QLatin1String("cols")) {
            const int n = sectScan.attributes().value(QStringLiteral("w:num")).toInt();
            if (n > 0)
                doc.page.columns = qBound(1, n, 3);
            doc.page.columnSeparator = sectScan.attributes().value(QStringLiteral("w:sep")).toString() == QLatin1String("1");
        }
    }

    QXmlStreamReader xml(xmlBytes);
    QVector<TextRun> runs;
    TextRun run;
    QString pStyle;
    QString align;
    QString paraDir;
    int numId = 0;
    int ilvl = 0;
    bool inP = false;
    bool inR = false;
    bool inTbl = false;
    Block tableBlock;
    QVector<TableCell> tableRow;
    bool inCell = false;
    QString blipRid;
    bool skipEmptyP = false;
    auto hasBookmarkNamed = [&](const QString &name) {
        DocxOut names;
        fillBookmarkNames(names, doc);
        if (names.exportedNames.contains(name))
            return true;
        for (const auto &bm : doc.bookmarks) {
            if (bm.name == name || bm.id == name)
                return true;
        }
        return false;
    };
    QVector<int> pendingBookmarkIdx;
    QVector<int> pendingRevisionIdx;
    QString curLinkHref;
    QString curRevAuthor;
    qint64 curRevDate = 0;
    int curRevStartOff = -1;
    bool inDel = false;
    bool inRPrChange = false;
    bool runHasFmtChange = false;
    bool sawPriorRPr = false;
    QString fmtAuthor;
    qint64 fmtDate = 0;
    TextRun delRun;
    TextRun priorRun;
    QVector<TextRun> delAcc;
    auto plainOf = [](const QVector<TextRun> &rs) {
        QString s;
        for (const auto &rr : rs)
            s += rr.text;
        return s;
    };
    auto fmtTarget = [&]() -> TextRun * {
        if (inRPrChange)
            return &priorRun;
        if (inDel)
            return &delRun;
        if (inR)
            return &run;
        return nullptr;
    };
    auto curLen = [&]() {
        int n = 0;
        for (const auto &r : runs)
            n += r.text.size();
        return n;
    };

    auto flushP = [&]() {
        bool empty = true;
        for (const auto &r : runs) {
            if (!r.text.trimmed().isEmpty())
                empty = false;
        }
        if (skipEmptyP && empty && !inCell) {
            runs.clear();
            pStyle.clear();
            align.clear();
            paraDir.clear();
            numId = 0;
            ilvl = 0;
            return;
        }
        Block b;
        if (numId == 1 || numId == 2) {
            b.type = QStringLiteral("list-item");
            b.listKind = numId == 1 ? QStringLiteral("decimal") : QStringLiteral("bullet");
            b.level = ilvl + 1;
        } else if (pStyle.startsWith(QLatin1String("Heading"))) {
            b.type = QStringLiteral("heading");
            b.level = pStyle.mid(7).toInt();
            if (b.level <= 0)
                b.level = 1;
            b.style = QStringLiteral("Heading %1").arg(b.level);
        } else if (pStyle == QLatin1String("Title")) {
            b.type = QStringLiteral("heading");
            b.level = 1;
            b.style = QStringLiteral("Title");
        } else if (pStyle.startsWith(QLatin1String("TOC"))) {
            b.type = QStringLiteral("toc");
            b.style = QStringLiteral("Normal");
        } else {
            b.type = QStringLiteral("paragraph");
            b.style = pStyle.isEmpty() ? QStringLiteral("Normal") : pStyle;
        }
        b.align = align;
        b.dir = paraDir;
        b.runs = runs;
        if (b.runs.isEmpty())
            b.runs.push_back(TextRun{});
        if (inCell) {
            TableCell cell;
            cell.runs = b.runs;
            tableRow.push_back(cell);
        } else {
            doc.assignId(b);
            for (int idx : pendingBookmarkIdx)
                doc.bookmarks[idx].blockId = b.id;
            QVector<int> dropRev;
            for (int idx : pendingRevisionIdx) {
                doc.revisions[idx].blockId = b.id;
                const Revision &rv = doc.revisions[idx];
                for (int j = 0; j < doc.revisions.size(); ++j) {
                    if (j == idx)
                        continue;
                    const Revision &ex = doc.revisions[j];
                    if (ex.blockId == rv.blockId && ex.kind == rv.kind && ex.start == rv.start && ex.end == rv.end) {
                        dropRev.push_back(idx);
                        break;
                    }
                }
            }
            std::sort(dropRev.begin(), dropRev.end());
            for (int i = dropRev.size() - 1; i >= 0; --i)
                doc.revisions.removeAt(dropRev[i]);
            doc.body.push_back(b);
        }
        pendingBookmarkIdx.clear();
        pendingRevisionIdx.clear();
        runs.clear();
        pStyle.clear();
        align.clear();
        paraDir.clear();
        numId = 0;
        ilvl = 0;
    };

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto n = xml.name();
            if (n == QLatin1String("tbl")) {
                inTbl = true;
                tableBlock = Block{};
                tableBlock.type = QStringLiteral("table");
            } else if (n == QLatin1String("tr") && inTbl) {
                tableRow.clear();
            } else if (n == QLatin1String("tc") && inTbl) {
                inCell = true;
                runs.clear();
            } else if (n == QLatin1String("p")) {
                inP = true;
                runs.clear();
                pStyle.clear();
                align.clear();
                paraDir.clear();
                numId = 0;
                inDel = false;
                inRPrChange = false;
                delAcc.clear();
            } else if (n == QLatin1String("pStyle")) {
                pStyle = xml.attributes().value(QStringLiteral("w:val")).toString();
            } else if (n == QLatin1String("numId")) {
                numId = xml.attributes().value(QStringLiteral("w:val")).toInt();
            } else if (n == QLatin1String("ilvl")) {
                ilvl = xml.attributes().value(QStringLiteral("w:val")).toInt();
            } else if (n == QLatin1String("jc")) {
                const auto v = xml.attributes().value(QStringLiteral("w:val")).toString();
                align = (v == QLatin1String("both")) ? QStringLiteral("justify") : v;
            } else if (n == QLatin1String("bidi")) {
                const auto v = xml.attributes().value(QStringLiteral("w:val")).toString();
                paraDir = (v.isEmpty() || v != QLatin1String("0")) ? QStringLiteral("rtl")
                                                                   : QStringLiteral("ltr");
            } else if (n == QLatin1String("rtl") && inR) {
                // run-level RTL flag: read-only, ignored
            } else if (n == QLatin1String("r")) {
                inR = true;
                if (inDel)
                    delRun = TextRun{};
                else {
                    run = TextRun{};
                    runHasFmtChange = false;
                    sawPriorRPr = false;
                    priorRun = TextRun{};
                }
            } else if (n == QLatin1String("rPrChange") && inR && !inDel) {
                inRPrChange = true;
                sawPriorRPr = false;
                priorRun = TextRun{};
                fmtAuthor = xml.attributes().value(QStringLiteral("w:author")).toString();
                fmtDate = QDateTime::fromString(xml.attributes().value(QStringLiteral("w:date")).toString(), Qt::ISODate)
                              .toMSecsSinceEpoch();
                // LibreOffice writes an empty rPrChange as <w:rPrChange .../> with no inner w:rPr.
                if (xml.isEndElement()) {
                    inRPrChange = false;
                    runHasFmtChange = true;
                }
            } else if (n == QLatin1String("rPr") && inRPrChange) {
                sawPriorRPr = true;
            } else if (n == QLatin1String("b") || n == QLatin1String("i") || n == QLatin1String("u")
                       || n == QLatin1String("strike") || n == QLatin1String("vertAlign")
                       || n == QLatin1String("highlight")) {
                if (TextRun *dst = fmtTarget()) {
                    if (n == QLatin1String("b"))
                        dst->bold = true;
                    else if (n == QLatin1String("i"))
                        dst->italic = true;
                    else if (n == QLatin1String("u"))
                        dst->underline = true;
                    else if (n == QLatin1String("strike"))
                        dst->strike = true;
                    else if (n == QLatin1String("vertAlign")) {
                        const auto v = xml.attributes().value(QStringLiteral("w:val")).toString();
                        dst->superscript = (v == QLatin1String("superscript"));
                        dst->subscript = (v == QLatin1String("subscript"));
                    } else if (n == QLatin1String("highlight")) {
                        dst->highlight = QStringLiteral("#FFF59D");
                    }
                }
            } else if ((n == QLatin1String("t") || n == QLatin1String("delText")) && (inR || inDel)) {
                const QString tx = xml.readElementText();
                if (inDel)
                    delRun.text += tx;
                else
                    run.text += tx;
            } else if (n == QLatin1String("hyperlink")) {
                const QString anchor = xml.attributes().value(QStringLiteral("w:anchor")).toString();
                const QString rid = xml.attributes().value(QStringLiteral("r:id")).toString();
                if (!anchor.isEmpty())
                    curLinkHref = QLatin1Char('#') + anchor;
                else if (!rid.isEmpty())
                    curLinkHref = rels.value(rid);
            } else if (n == QLatin1String("bookmarkStart")
                       && !hasBookmarkNamed(xml.attributes().value(QStringLiteral("w:name")).toString())) {
                Bookmark bm;
                bm.name = xml.attributes().value(QStringLiteral("w:name")).toString();
                bm.id = QStringLiteral("bm%1").arg(doc.bookmarks.size() + 1);
                bm.blockId = QStringLiteral("pending");
                bm.offset = curLen();
                doc.bookmarks.push_back(bm);
                pendingBookmarkIdx.push_back(doc.bookmarks.size() - 1);
            } else if (n == QLatin1String("ins") || n == QLatin1String("del")) {
                doc.trackChanges = true;
                curRevAuthor = xml.attributes().value(QStringLiteral("w:author")).toString();
                curRevDate = QDateTime::fromString(xml.attributes().value(QStringLiteral("w:date")).toString(),
                                                    Qt::ISODate)
                                 .toMSecsSinceEpoch();
                curRevStartOff = curLen();
                if (n == QLatin1String("del")) {
                    inDel = true;
                    delAcc.clear();
                }
            } else if (n == QLatin1String("fldSimple")) {
                TextRun fr;
                const QString instrRaw = xml.attributes().value(QStringLiteral("w:instr")).toString().trimmed();
                const QString instr = instrRaw.toUpper();
                if (instr.contains(QLatin1String("PAGEREF"))) {
                    fr.field = QStringLiteral("xref");
                    fr.xrefKind = QStringLiteral("page");
                    fr.fieldArg = instrRaw.section(QLatin1Char(' '), 1, 1);
                } else if (instr.contains(QLatin1String("PAGE")) && !instr.contains(QLatin1String("NUMPAGES")))
                    fr.field = QStringLiteral("page_number");
                else if (instr.contains(QLatin1String("NUMPAGES")))
                    fr.field = QStringLiteral("page_count");
                else if (instr.contains(QLatin1String("DATE")))
                    fr.field = QStringLiteral("date");
                else if (instr.contains(QLatin1String("TITLE")))
                    fr.field = QStringLiteral("title");
                else if (instr.contains(QLatin1String("AUTHOR")))
                    fr.field = QStringLiteral("author");
                else if (instr.contains(QLatin1String("FILENAME")))
                    fr.field = QStringLiteral("filename");
                else if (instr.contains(QLatin1String("NUMWORDS")))
                    fr.field = QStringLiteral("word_count");
                else if (instr.contains(QLatin1String("MERGEFIELD"))) {
                    fr.field = QStringLiteral("merge");
                    fr.fieldArg = instrRaw.section(QLatin1Char(' '), 1, 1);
                } else if (instr.contains(QLatin1String("STYLEREF"))) {
                    fr.field = QStringLiteral("chapter");
                } else if (instr.contains(QLatin1String("DOCVARIABLE"))) {
                    fr.field = QStringLiteral("var");
                    fr.fieldArg = instrRaw.section(QLatin1Char(' '), 1, 1);
                } else if (instr.contains(QLatin1String("FILLIN"))) {
                    fr.field = QStringLiteral("input");
                } else if (instr.contains(QLatin1String("AIOFFICE_CITE"))) {
                    fr.field = QStringLiteral("cite");
                    fr.fieldArg = instrRaw.section(QLatin1Char(' '), 1, 1);
                } else if (instr.contains(QLatin1String("TIME"))) {
                    fr.field = QStringLiteral("time");
                } else if (instr.contains(QLatin1String("REF"))) {
                    fr.field = QStringLiteral("xref");
                    fr.fieldArg = instrRaw.section(QLatin1Char(' '), 1, 1);
                    if (instrRaw.contains(QLatin1String("\\n")) || instrRaw.contains(QLatin1String("\\N")))
                        fr.xrefKind = QStringLiteral("heading_number");
                }
                fr.text = xml.readElementText(QXmlStreamReader::IncludeChildElements);
                runs.push_back(fr);
            } else if (n == QLatin1String("footnoteReference")) {
                const int id = xml.attributes().value(QStringLiteral("w:id")).toInt();
                Block nb;
                nb.type = QStringLiteral("footnote");
                TextRun nr;
                nr.text = footnoteText.value(id);
                nb.runs.push_back(nr);
                doc.assignId(nb);
                run.noteId = nb.id;
                run.superscript = true;
                doc.body.push_back(nb);
            } else if (n == QLatin1String("endnoteReference")) {
                const int id = xml.attributes().value(QStringLiteral("w:id")).toInt();
                Block nb;
                nb.type = QStringLiteral("endnote");
                TextRun nr;
                nr.text = endnoteText.value(id);
                nb.runs.push_back(nr);
                doc.assignId(nb);
                run.noteId = nb.id;
                run.superscript = true;
                doc.body.push_back(nb);
            } else if (n == QLatin1String("blip")) {
                blipRid = xml.attributes().value(QStringLiteral("r:embed")).toString();
            } else if (n == QLatin1String("br")) {
                const auto ty = xml.attributes().value(QStringLiteral("w:type")).toString();
                if (ty == QLatin1String("page") || ty == QLatin1String("column")) {
                    skipEmptyP = true;
                    if (inP)
                        flushP();
                    Block br;
                    br.type = QStringLiteral("break");
                    br.breakKind = ty;
                    doc.assignId(br);
                    doc.body.push_back(br);
                    inP = false;
                    runs.clear();
                }
            }
        } else if (xml.isEndElement()) {
            const auto n = xml.name();
            if (n == QLatin1String("rPrChange")) {
                inRPrChange = false;
                if (inR && !inDel)
                    runHasFmtChange = true;
            } else if (n == QLatin1String("r") && inR) {
                if (inDel) {
                    if (!curLinkHref.isEmpty())
                        delRun.link = curLinkHref;
                    if (!delRun.text.isEmpty() || delRun.bold || delRun.italic || delRun.underline || delRun.strike
                        || !delRun.noteId.isEmpty())
                        delAcc.push_back(delRun);
                } else {
                    if (!curLinkHref.isEmpty())
                        run.link = curLinkHref;
                    const int before = curLen();
                    if (!run.text.isEmpty() || run.bold || run.italic || !run.noteId.isEmpty() || !run.field.isEmpty())
                        runs.push_back(run);
                    if (runHasFmtChange && curLen() > before) {
                        Revision rv;
                        rv.id = QStringLiteral("rev%1").arg(doc.revisions.size() + 1);
                        rv.kind = QStringLiteral("format");
                        rv.author = fmtAuthor;
                        rv.timestamp = fmtDate;
                        rv.blockId = QStringLiteral("pending");
                        rv.start = before;
                        rv.end = curLen();
                        QJsonObject payload;
                        if (run.bold)
                            payload[QStringLiteral("bold")] = true;
                        if (run.italic)
                            payload[QStringLiteral("italic")] = true;
                        if (run.underline)
                            payload[QStringLiteral("underline")] = true;
                        if (run.strike)
                            payload[QStringLiteral("strike")] = true;
                        rv.payload = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
                        if (sawPriorRPr) {
                            priorRun.text = run.text;
                            rv.priorRuns.push_back(priorRun);
                        }
                        doc.trackChanges = true;
                        if (!jsonOwnsRevisions) {
                            doc.revisions.push_back(rv);
                            pendingRevisionIdx.push_back(doc.revisions.size() - 1);
                        }
                    }
                }
                inR = false;
                runHasFmtChange = false;
            } else if (n == QLatin1String("hyperlink")) {
                curLinkHref.clear();
            } else if (n == QLatin1String("ins") && curRevStartOff >= 0) {
                Revision rv;
                rv.id = QStringLiteral("rev%1").arg(doc.revisions.size() + 1);
                rv.kind = QStringLiteral("insert");
                rv.author = curRevAuthor;
                rv.timestamp = curRevDate;
                rv.blockId = QStringLiteral("pending");
                rv.start = curRevStartOff;
                rv.end = curLen();
                rv.text = plainOf(runs).mid(rv.start, qMax(0, rv.end - rv.start));
                if (!jsonOwnsRevisions) {
                    doc.revisions.push_back(rv);
                    pendingRevisionIdx.push_back(doc.revisions.size() - 1);
                }
                curRevStartOff = -1;
            } else if (n == QLatin1String("del") && curRevStartOff >= 0) {
                if (!jsonOwnsRevisions) {
                    Revision rv;
                    rv.id = QStringLiteral("rev%1").arg(doc.revisions.size() + 1);
                    rv.kind = QStringLiteral("delete");
                    rv.author = curRevAuthor;
                    rv.timestamp = curRevDate;
                    rv.blockId = QStringLiteral("pending");
                    rv.start = curRevStartOff;
                    rv.end = curRevStartOff;
                    rv.deletedRuns = delAcc;
                    rv.text = plainOf(delAcc);
                    doc.revisions.push_back(rv);
                    pendingRevisionIdx.push_back(doc.revisions.size() - 1);
                }
                inDel = false;
                delAcc.clear();
                curRevStartOff = -1;
            } else if (n == QLatin1String("drawing") && !blipRid.isEmpty()) {
                QString target = rels.value(blipRid);
                if (target.startsWith(QLatin1String("media/")))
                    target = QStringLiteral("word/") + target;
                Block img;
                img.type = QStringLiteral("image");
                img.imagePath = target;
                img.imageBytes = Zip::file(entries, target);
                doc.assignId(img);
                doc.body.push_back(img);
                blipRid.clear();
                inP = false;
                runs.clear();
            } else if (n == QLatin1String("p")) {
                if (inP)
                    flushP();
                inP = false;
                skipEmptyP = false;
            } else if (n == QLatin1String("tc") && inTbl) {
                inCell = false;
            } else if (n == QLatin1String("tr") && inTbl) {
                tableBlock.table.push_back(tableRow);
            } else if (n == QLatin1String("tbl") && inTbl) {
                doc.assignId(tableBlock);
                doc.body.push_back(tableBlock);
                inTbl = false;
            }
        }
    }
    if (doc.body.isEmpty())
        doc = Document::blank();
    return true;
}

bool loadDocxFile(const QString &path, Document &doc, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot read ") + path;
        return false;
    }
    return loadDocx(f.readAll(), doc, error);
}

} // namespace aioffice
