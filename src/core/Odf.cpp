#include "core/Odf.h"

#include "core/Xml.h"
#include "core/Zip.h"

#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QXmlStreamReader>

namespace aioffice {
namespace {

QString styleForBlock(const Block &b)
{
    if (!b.style.isEmpty())
        return b.style;
    if (b.type == QLatin1String("heading"))
        return QStringLiteral("Heading %1").arg(b.level > 0 ? b.level : 1);
    return QStringLiteral("Normal");
}

QString odfStyleName(QString name)
{
    name.replace(QLatin1Char(' '), QLatin1String("_20_"));
    return name;
}

QString fromOdfStyleName(QString name)
{
    name.replace(QLatin1String("_20_"), QStringLiteral(" "));
    return name;
}

struct SpanCounter {
    int n = 0;
    QString stylesXml;
    QMap<QString, int> seen;
};

QString emitSpan(SpanCounter &sc, const TextRun &r, const Document &doc)
{
    const bool fmt = r.bold || r.italic || r.underline || r.strike || r.superscript || r.subscript
        || !r.font.isEmpty() || r.sizePt > 0 || !r.color.isEmpty() || !r.highlight.isEmpty() || !r.link.isEmpty();
    QString txt = r.text;
    if (txt.isEmpty() && !r.field.isEmpty())
        txt = runDisplayText(doc, r);
    QString inner;
    if (!r.field.isEmpty()) {
        if (r.field == QLatin1String("page_number"))
            inner = QLatin1String("<text:page-number text:select-page=\"current\">1</text:page-number>");
        else if (r.field == QLatin1String("page_count"))
            inner = QLatin1String("<text:page-count>1</text:page-count>");
        else if (r.field == QLatin1String("date"))
            inner = QStringLiteral("<text:date>%1</text:date>").arg(xmlEscape(txt));
        else if (r.field == QLatin1String("title"))
            inner = QStringLiteral("<text:title>%1</text:title>").arg(xmlEscape(txt));
        else if (r.field == QLatin1String("author"))
            inner = QStringLiteral("<text:author-name>%1</text:author-name>").arg(xmlEscape(txt));
        else if (r.field == QLatin1String("filename") || r.field == QLatin1String("file_name"))
            inner = QStringLiteral("<text:file-name>%1</text:file-name>").arg(xmlEscape(txt));
        else if (r.field == QLatin1String("word_count"))
            inner = QStringLiteral("<text:word-count>%1</text:word-count>").arg(xmlEscape(txt));
        else
            inner = QStringLiteral("<text:user-field-get text:name=\"%1\">%2</text:user-field-get>")
                        .arg(xmlEscape(r.field), xmlEscape(txt.isEmpty() ? r.fieldArg : txt));
    } else if (!txt.isEmpty() || fmt) {
        if (!fmt && r.link.isEmpty())
            inner = xmlEscape(txt);
        else {
            QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10")
                              .arg(r.bold)
                              .arg(r.italic)
                              .arg(r.underline)
                              .arg(r.strike)
                              .arg(r.superscript)
                              .arg(r.subscript)
                              .arg(r.font)
                              .arg(r.sizePt)
                              .arg(r.color, r.highlight);
            int id = sc.seen.value(key, 0);
            if (id == 0) {
                id = ++sc.n;
                sc.seen[key] = id;
                QString props;
                if (r.bold)
                    props += QLatin1String("fo:font-weight=\"bold\" ");
                if (r.italic)
                    props += QLatin1String("fo:font-style=\"italic\" ");
                if (r.underline)
                    props += QLatin1String("style:text-underline-style=\"solid\" ");
                if (r.strike)
                    props += QLatin1String("style:text-line-through-style=\"solid\" ");
                if (r.superscript)
                    props += QLatin1String("style:text-position=\"super 58%\" ");
                if (r.subscript)
                    props += QLatin1String("style:text-position=\"sub 58%\" ");
                if (!r.font.isEmpty())
                    props += QStringLiteral("style:font-name=\"%1\" ").arg(xmlEscape(r.font));
                if (r.sizePt > 0)
                    props += QStringLiteral("fo:font-size=\"%1pt\" ").arg(r.sizePt);
                if (!r.color.isEmpty())
                    props += QStringLiteral("fo:color=\"%1\" ").arg(xmlEscape(r.color));
                if (!r.highlight.isEmpty())
                    props += QStringLiteral("fo:background-color=\"%1\" ").arg(xmlEscape(r.highlight));
                sc.stylesXml += QStringLiteral(
                                    "<style:style style:name=\"T%1\" style:family=\"text\"><style:text-properties %2/></style:style>")
                                    .arg(id)
                                    .arg(props);
            }
            inner = QStringLiteral("<text:span text:style-name=\"T%1\">%2</text:span>").arg(id).arg(xmlEscape(txt));
            if (!r.link.isEmpty())
                inner = QStringLiteral("<text:a xlink:href=\"%1\">%2</text:a>").arg(xmlEscape(r.link), inner);
        }
    }
    if (!r.noteId.isEmpty()) {
        const Block *nb = doc.findBlock(r.noteId);
        if (nb) {
            const bool fn = nb->type == QLatin1String("footnote");
            QString body;
            for (const auto &nr : nb->runs)
                body += xmlEscape(nr.text);
            inner += QStringLiteral(
                         "<text:note text:id=\"%1\" text:note-class=\"%2\">"
                         "<text:note-citation>*</text:note-citation>"
                         "<text:note-body><text:p text:style-name=\"Standard\">%3</text:p></text:note-body></text:note>")
                         .arg(xmlEscape(r.noteId),
                              fn ? QStringLiteral("footnote") : QStringLiteral("endnote"),
                              body);
        }
    }
    return inner;
}

QString spansXml(SpanCounter &sc, const QVector<TextRun> &runs, const Document &doc)
{
    QString xml;
    for (const auto &r : runs)
        xml += emitSpan(sc, r, doc);
    return xml;
}

QString imageExt(const Block &b)
{
    if (b.imageMime.contains(QLatin1String("jpeg")) || b.imagePath.endsWith(QLatin1String(".jpg"), Qt::CaseInsensitive)
        || b.imagePath.endsWith(QLatin1String(".jpeg"), Qt::CaseInsensitive))
        return QStringLiteral("jpg");
    return QStringLiteral("png");
}

} // namespace

bool saveOdt(const Document &doc, const QString &path, QString *error)
{
    double wMm = 0, hMm = 0;
    doc.page.pageSizeMm(wMm, hMm);
    QString colsXml;
    if (doc.page.columns > 1)
        colsXml = QStringLiteral("<style:columns fo:column-count=\"%1\" fo:column-gap=\"0.5cm\"/>").arg(doc.page.columns);
    const QString pageLayout = QStringLiteral(
        "<style:page-layout style:name=\"pm1\"><style:page-layout-properties "
        "fo:page-width=\"%1mm\" fo:page-height=\"%2mm\" style:print-orientation=\"%3\" "
        "fo:margin-top=\"%4mm\" fo:margin-right=\"%5mm\" fo:margin-bottom=\"%6mm\" fo:margin-left=\"%7mm\">"
        "%8</style:page-layout-properties></style:page-layout>")
                                   .arg(wMm)
                                   .arg(hMm)
                                   .arg(doc.page.orientation)
                                   .arg(doc.page.marginsMm[0])
                                   .arg(doc.page.marginsMm[1])
                                   .arg(doc.page.marginsMm[2])
                                   .arg(doc.page.marginsMm[3])
                                   .arg(colsXml);

    QString paraStyles;
    for (const auto &s : doc.styles) {
        QString tp;
        tp += QStringLiteral("fo:font-size=\"%1pt\" ").arg(s.sizePt);
        if (s.bold)
            tp += QLatin1String("fo:font-weight=\"bold\" ");
        if (s.italic)
            tp += QLatin1String("fo:font-style=\"italic\" ");
        if (!s.color.isEmpty())
            tp += QStringLiteral("fo:color=\"%1\" ").arg(xmlEscape(s.color));
        QString pp;
        if (!s.align.isEmpty())
            pp += QStringLiteral("fo:text-align=\"%1\" ").arg(s.align);
        pp += QStringLiteral("fo:margin-top=\"%1pt\" fo:margin-bottom=\"%2pt\" ")
                  .arg(s.spaceBeforePt)
                  .arg(s.spaceAfterPt);
        paraStyles += QStringLiteral(
                          "<style:style style:name=\"%1\" style:family=\"paragraph\" style:parent-style-name=\"Standard\">"
                          "<style:paragraph-properties %2/><style:text-properties style:font-name=\"%3\" %4/>"
                          "</style:style>")
                          .arg(odfStyleName(s.name), pp, xmlEscape(s.font), tp);
    }
    paraStyles += QLatin1String(
        "<style:style style:name=\"PBreak\" style:family=\"paragraph\">"
        "<style:paragraph-properties fo:break-before=\"page\"/></style:style>");
    paraStyles += QLatin1String(
        "<style:style style:name=\"PColBreak\" style:family=\"paragraph\">"
        "<style:paragraph-properties fo:break-before=\"column\"/></style:style>");

    SpanCounter sc;
    QString bodyXml;
    QVector<ZipEntry> pictures;
    int imgN = 0;
    int i = 0;
    const auto &body = doc.body;
    auto bookmarksAround = [&](const QString &blockId, const QString &inner) {
        QString out = inner;
        for (const auto &bm : doc.bookmarks) {
            if (bm.blockId != blockId)
                continue;
            out = QStringLiteral("<text:bookmark-start text:name=\"%1\"/>%2<text:bookmark-end text:name=\"%3\"/>")
                      .arg(xmlEscape(bm.name.isEmpty() ? bm.id : bm.name), out,
                           xmlEscape(bm.name.isEmpty() ? bm.id : bm.name));
        }
        return out;
    };
    while (i < body.size()) {
        const Block &b = body[i];
        if (b.type == QLatin1String("footnote") || b.type == QLatin1String("endnote")) {
            ++i;
            continue;
        }
        const QString st = odfStyleName(styleForBlock(b));
        if (b.type == QLatin1String("list-item")) {
            const QString kind = b.listKind;
            bodyXml += QLatin1String("<text:list>");
            while (i < body.size() && body[i].type == QLatin1String("list-item") && body[i].listKind == kind) {
                QString inner = QStringLiteral("<text:p text:style-name=\"%1\">%2</text:p>")
                                    .arg(odfStyleName(styleForBlock(body[i])), spansXml(sc, body[i].runs, doc));
                const int lv = qMax(1, body[i].level);
                for (int n = 1; n < lv; ++n)
                    inner = QStringLiteral("<text:list><text:list-item>%1</text:list-item></text:list>").arg(inner);
                bodyXml += QStringLiteral("<text:list-item>%1</text:list-item>").arg(bookmarksAround(body[i].id, inner));
                ++i;
            }
            bodyXml += QLatin1String("</text:list>");
            continue;
        }
        if (b.type == QLatin1String("heading")) {
            bodyXml += bookmarksAround(b.id,
                QStringLiteral("<text:h text:style-name=\"%1\" text:outline-level=\"%2\">%3</text:h>")
                    .arg(st)
                    .arg(b.level > 0 ? b.level : 1)
                    .arg(spansXml(sc, b.runs, doc)));
        } else if (b.type == QLatin1String("break")) {
            bodyXml += b.breakKind == QLatin1String("column")
                ? QLatin1String("<text:p text:style-name=\"PColBreak\"/>")
                : QLatin1String("<text:p text:style-name=\"PBreak\"/>");
        } else if (b.type == QLatin1String("toc")) {
            QString items;
            for (const auto &tid : b.tocTargets) {
                const Block *t = doc.findBlock(tid);
                items += QStringLiteral("<text:p text:style-name=\"Standard\">%1</text:p>")
                             .arg(xmlEscape(t ? t->plainText() : QString()));
            }
            if (items.isEmpty())
                items = QStringLiteral("<text:p text:style-name=\"Standard\">%1</text:p>")
                            .arg(xmlEscape(b.plainText()));
            bodyXml += QStringLiteral(
                           "<text:table-of-content text:name=\"Table of Contents\" text:protected=\"false\">"
                           "<text:index-body>%1</text:index-body></text:table-of-content>")
                           .arg(items);
        } else if (b.type == QLatin1String("table")) {
            bodyXml += QLatin1String(
                "<table:table xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\">");
            for (const auto &row : b.table) {
                bodyXml += QLatin1String("<table:table-row>");
                for (const auto &cell : row) {
                    QString spanAttr;
                    if (cell.colSpan > 1)
                        spanAttr += QStringLiteral(" table:number-columns-spanned=\"%1\"").arg(cell.colSpan);
                    bodyXml += QStringLiteral(
                        "<table:table-cell%1><text:p text:style-name=\"Standard\">%2</text:p></table:table-cell>")
                                   .arg(spanAttr, spansXml(sc, cell.runs, doc));
                }
                bodyXml += QLatin1String("</table:table-row>");
            }
            bodyXml += QLatin1String("</table:table>");
        } else if (b.type == QLatin1String("image")) {
            ++imgN;
            const QString ext = imageExt(b);
            const QString href = QStringLiteral("Pictures/image%1.%2").arg(imgN).arg(ext);
            QByteArray bytes = b.imageBytes;
            if (bytes.isEmpty() && !b.imagePath.isEmpty()) {
                QFile f(b.imagePath);
                if (f.open(QIODevice::ReadOnly))
                    bytes = f.readAll();
            }
            if (!bytes.isEmpty())
                pictures.push_back({href, bytes, false});
            bodyXml += QStringLiteral(
                           "<text:p text:style-name=\"Standard\"><draw:frame xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
                           "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" svg:width=\"%1mm\" svg:height=\"40mm\">"
                           "<draw:image xmlns:xlink=\"http://www.w3.org/1999/xlink\" xlink:href=\"%2\"/></draw:frame></text:p>")
                           .arg(b.imageWidthMm)
                           .arg(xmlEscape(href));
        } else {
            bodyXml += bookmarksAround(b.id,
                QStringLiteral("<text:p text:style-name=\"%1\">%2</text:p>")
                    .arg(st, spansXml(sc, b.runs, doc)));
        }
        ++i;
    }
    QSet<QString> referencedNotes;
    for (const auto &b : body) {
        for (const auto &r : b.runs)
            if (!r.noteId.isEmpty())
                referencedNotes.insert(r.noteId);
    }
    for (const auto &b : body) {
        if ((b.type == QLatin1String("footnote") || b.type == QLatin1String("endnote"))
            && !referencedNotes.contains(b.id)) {
            TextRun fake;
            fake.noteId = b.id;
            bodyXml += QStringLiteral("<text:p text:style-name=\"Standard\">%1</text:p>").arg(emitSpan(sc, fake, doc));
        }
    }

    const QString stylesXml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<office:document-styles xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
        "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" "
        "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" office:version=\"1.3\">"
        "<office:font-face-decls>"
        "<style:font-face style:name=\"Liberation Serif\" svg:font-family=\"&apos;Liberation Serif&apos;\" "
        "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\"/>"
        "</office:font-face-decls>"
        "<office:styles>"
        "<style:default-style style:family=\"paragraph\">"
        "<style:paragraph-properties fo:text-align=\"left\"/>"
        "<style:text-properties fo:font-size=\"12pt\" style:font-name=\"Liberation Serif\"/>"
        "</style:default-style>"
        "<style:style style:name=\"Standard\" style:family=\"paragraph\"/>"
        "%1</office:styles>"
        "<office:automatic-styles>%2"
        "<style:style style:name=\"Mpm1\" style:family=\"paragraph\" style:master-page-name=\"MP1\"/>"
        "</office:automatic-styles>"
        "<office:master-styles><style:master-page style:name=\"MP1\" style:page-layout-name=\"pm1\"/></office:master-styles>"
        "</office:document-styles>")
                                  .arg(paraStyles, pageLayout);

    const QString contentXml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
        "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
        "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" "
        "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
        "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
        "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" "
        "xmlns:xlink=\"http://www.w3.org/1999/xlink\" office:version=\"1.3\">"
        "<office:automatic-styles>%1</office:automatic-styles>"
        "<office:body><office:text>%2</office:text></office:body></office:document-content>")
                                   .arg(sc.stylesXml, bodyXml);

    QString manifest = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">"
        "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
        "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
        "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>");
    for (const auto &pic : pictures) {
        manifest += QStringLiteral("<manifest:file-entry manifest:full-path=\"%1\" manifest:media-type=\"image/%2\"/>")
                        .arg(xmlEscape(pic.name), pic.name.endsWith(QLatin1String("jpg")) ? QStringLiteral("jpeg")
                                                                                           : QStringLiteral("png"));
    }
    manifest += QLatin1String("</manifest:manifest>");

    QJsonObject extra;
    extra[QStringLiteral("schema")] = 1;
    extra[QStringLiteral("track_changes")] = doc.trackChanges;
    extra[QStringLiteral("columns")] = doc.page.columns;
    extra[QStringLiteral("column_separator")] = doc.page.columnSeparator;
    QJsonArray bmA;
    for (const auto &bm : doc.bookmarks)
        bmA.append(bm.toJson());
    extra[QStringLiteral("bookmarks")] = bmA;
    QJsonArray rvA;
    for (const auto &rv : doc.revisions)
        rvA.append(rv.toJson());
    extra[QStringLiteral("revisions")] = rvA;
    const QString extraJson = QString::fromUtf8(QJsonDocument(extra).toJson(QJsonDocument::Compact));
    const QString meta = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<office:document-meta xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
        "xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" office:version=\"1.3\">"
        "<office:meta><dc:title>%1</dc:title>"
        "<meta:user-defined meta:name=\"aioffice:extra\">%2</meta:user-defined>"
        "</office:meta></office:document-meta>")
                             .arg(xmlEscape(doc.title), xmlEscape(extraJson));

    QVector<ZipEntry> entries;
    entries.push_back({QStringLiteral("mimetype"),
                       QByteArray("application/vnd.oasis.opendocument.text"), true});
    entries.push_back({QStringLiteral("content.xml"), contentXml.toUtf8(), false});
    entries.push_back({QStringLiteral("styles.xml"), stylesXml.toUtf8(), false});
    entries.push_back({QStringLiteral("meta.xml"), meta.toUtf8(), false});
    entries.push_back({QStringLiteral("META-INF/manifest.xml"), manifest.toUtf8(), false});
    for (const auto &pic : pictures)
        entries.push_back(pic);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Cannot write ") + path;
        return false;
    }
    f.write(Zip::pack(entries));
    return true;
}

static void applyTextProps(TextRun &r, const QXmlStreamAttributes &a)
{
    const auto w = a.value(QStringLiteral("fo:font-weight"));
    if (w == QLatin1String("bold") || w == QLatin1String("700"))
        r.bold = true;
    if (a.value(QStringLiteral("fo:font-style")) == QLatin1String("italic"))
        r.italic = true;
    if (!a.value(QStringLiteral("style:text-underline-style")).isEmpty())
        r.underline = true;
    if (!a.value(QStringLiteral("style:text-line-through-style")).isEmpty())
        r.strike = true;
    const QString pos = a.value(QStringLiteral("style:text-position")).toString();
    if (pos.startsWith(QLatin1String("super")))
        r.superscript = true;
    if (pos.startsWith(QLatin1String("sub")))
        r.subscript = true;
    if (!a.value(QStringLiteral("fo:color")).isEmpty())
        r.color = a.value(QStringLiteral("fo:color")).toString();
    if (!a.value(QStringLiteral("fo:font-size")).isEmpty()) {
        QString s = a.value(QStringLiteral("fo:font-size")).toString();
        s.remove(QStringLiteral("pt"));
        r.sizePt = s.toDouble();
    }
    if (!a.value(QStringLiteral("fo:background-color")).isEmpty())
        r.highlight = a.value(QStringLiteral("fo:background-color")).toString();
}

bool loadOdt(const QByteArray &zipData, Document &doc, QString *error)
{
    QVector<ZipEntry> entries;
    if (!Zip::unpack(zipData, entries, error))
        return false;
    const QByteArray content = Zip::file(entries, QStringLiteral("content.xml"));
    if (content.isEmpty()) {
        if (error)
            *error = QStringLiteral("ODT missing content.xml");
        return false;
    }
    doc = Document::blank();
    doc.body.clear();

    const QByteArray stylesBytes = Zip::file(entries, QStringLiteral("styles.xml"));
    if (!stylesBytes.isEmpty()) {
        QXmlStreamReader st(stylesBytes);
        while (!st.atEnd()) {
            st.readNext();
            if (!st.isStartElement())
                continue;
            if (st.name() == QLatin1String("columns")) {
                const int n = st.attributes().value(QStringLiteral("fo:column-count")).toInt();
                if (n > 0)
                    doc.page.columns = qBound(1, n, 3);
                continue;
            }
            if (st.name() != QLatin1String("page-layout-properties"))
                continue;
            const auto a = st.attributes();
            QString w = a.value(QStringLiteral("fo:page-width")).toString();
            QString h = a.value(QStringLiteral("fo:page-height")).toString();
            w.remove(QStringLiteral("mm"));
            h.remove(QStringLiteral("mm"));
            const double ww = w.toDouble();
            const double hh = h.toDouble();
            if (ww > 0 && hh > 0) {
                if (qAbs(ww - 215.9) < 2 || qAbs(hh - 215.9) < 2)
                    doc.page.size = QStringLiteral("Letter");
                else
                    doc.page.size = QStringLiteral("A4");
                doc.page.orientation = (ww > hh) ? QStringLiteral("landscape") : QStringLiteral("portrait");
            }
            auto mm = [&](const QString &k, int i) {
                QString v = a.value(k).toString();
                v.remove(QStringLiteral("mm"));
                if (!v.isEmpty())
                    doc.page.marginsMm[i] = v.toDouble();
            };
            mm(QStringLiteral("fo:margin-top"), 0);
            mm(QStringLiteral("fo:margin-right"), 1);
            mm(QStringLiteral("fo:margin-bottom"), 2);
            mm(QStringLiteral("fo:margin-left"), 3);
        }
    }

    const QByteArray metaBytes = Zip::file(entries, QStringLiteral("meta.xml"));
    if (!metaBytes.isEmpty()) {
        QXmlStreamReader meta(metaBytes);
        while (!meta.atEnd()) {
            meta.readNext();
            if (meta.isStartElement() && meta.name().toString() == QLatin1String("title"))
                doc.title = meta.readElementText();
            else if (meta.isStartElement() && meta.name().toString() == QLatin1String("user-defined")
                     && meta.attributes().value(QStringLiteral("meta:name")) == QLatin1String("aioffice:extra")) {
                const auto extra = QJsonDocument::fromJson(meta.readElementText().toUtf8()).object();
                doc.trackChanges = extra.value(QStringLiteral("track_changes")).toBool();
                if (extra.contains(QStringLiteral("columns")))
                    doc.page.columns = qBound(1, extra.value(QStringLiteral("columns")).toInt(1), 3);
                doc.page.columnSeparator = extra.value(QStringLiteral("column_separator")).toBool();
                for (const auto &b : extra.value(QStringLiteral("bookmarks")).toArray())
                    doc.bookmarks.push_back(Bookmark::fromJson(b.toObject()));
                for (const auto &r : extra.value(QStringLiteral("revisions")).toArray())
                    doc.revisions.push_back(Revision::fromJson(r.toObject()));
            }
        }
    }

    QMap<QString, TextRun> autoText;
    QXmlStreamReader pre(content);
    bool inAuto = false;
    QString curStyle;
    while (!pre.atEnd()) {
        pre.readNext();
        if (pre.isStartElement()) {
            if (pre.name() == QLatin1String("automatic-styles"))
                inAuto = true;
            else if (inAuto && pre.name() == QLatin1String("style"))
                curStyle = pre.attributes().value(QStringLiteral("style:name")).toString();
            else if (inAuto && pre.name() == QLatin1String("text-properties") && !curStyle.isEmpty()) {
                TextRun r;
                applyTextProps(r, pre.attributes());
                autoText[curStyle] = r;
            }
        } else if (pre.isEndElement() && pre.name() == QLatin1String("automatic-styles"))
            inAuto = false;
    }

    QXmlStreamReader xml(content);
    QVector<TextRun> runs;
    TextRun spanRun;
    QString currentType = QStringLiteral("paragraph");
    int currentLevel = 0;
    QString currentStyle;
    QString listKind;
    int listLevel = 0;
    bool inText = false;
    bool inTable = false;
    Block tableBlock;
    QVector<TableCell> tableRow;
    double pendingFrameWidthMm = 0;
    const bool importNativeBm = doc.bookmarks.isEmpty();

    auto startRun = [&]() { spanRun = TextRun{}; };
    auto flushPara = [&]() {
        if (!inText && currentType != QLatin1String("table"))
            return;
        if (!spanRun.text.isEmpty())
            runs.push_back(spanRun);
        Block b;
        b.type = currentType;
        b.level = currentLevel;
        b.style = currentStyle;
        b.listKind = listKind;
        b.runs = runs;
        if (b.runs.isEmpty())
            b.runs.push_back(TextRun{});
        if (b.type == QLatin1String("list-item") && b.level <= 0)
            b.level = qMax(1, listLevel);
        doc.assignId(b);
        doc.body.push_back(b);
        runs.clear();
        startRun();
        inText = false;
        currentType = QStringLiteral("paragraph");
        currentLevel = 0;
        currentStyle.clear();
    };

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto n = xml.name();
            if (n == QLatin1String("list")) {
                ++listLevel;
                listKind = QStringLiteral("decimal");
            } else if (n == QLatin1String("list-item")) {
                currentType = QStringLiteral("list-item");
            } else if (n == QLatin1String("table")) {
                inTable = true;
                tableBlock = Block{};
                tableBlock.type = QStringLiteral("table");
            } else if (n == QLatin1String("table-row") && inTable) {
                tableRow.clear();
            } else if (n == QLatin1String("table-cell") && inTable) {
                runs.clear();
                startRun();
                inText = true;
                currentType = QStringLiteral("paragraph");
            } else if (n == QLatin1String("h")) {
                runs.clear();
                startRun();
                currentType = QStringLiteral("heading");
                currentLevel = xml.attributes().value(QStringLiteral("text:outline-level")).toInt();
                if (currentLevel <= 0)
                    currentLevel = 1;
                currentStyle = fromOdfStyleName(xml.attributes().value(QStringLiteral("text:style-name")).toString());
                inText = true;
            } else if (n == QLatin1String("p")) {
                const QString stn = xml.attributes().value(QStringLiteral("text:style-name")).toString();
                if (stn == QLatin1String("PBreak") || stn == QLatin1String("PColBreak")) {
                    Block br;
                    br.type = QStringLiteral("break");
                    br.breakKind = stn == QLatin1String("PColBreak") ? QStringLiteral("column")
                                                                    : QStringLiteral("page");
                    doc.assignId(br);
                    doc.body.push_back(br);
                    continue;
                }
                if (!inText) {
                    runs.clear();
                    startRun();
                    if (currentType != QLatin1String("list-item"))
                        currentType = QStringLiteral("paragraph");
                    currentStyle = fromOdfStyleName(stn);
                    inText = true;
                }
            } else if (n == QLatin1String("span")) {
                if (!spanRun.text.isEmpty())
                    runs.push_back(spanRun);
                startRun();
                const QString sn = xml.attributes().value(QStringLiteral("text:style-name")).toString();
                if (autoText.contains(sn)) {
                    spanRun.bold = autoText[sn].bold;
                    spanRun.italic = autoText[sn].italic;
                    spanRun.underline = autoText[sn].underline;
                    spanRun.color = autoText[sn].color;
                    spanRun.sizePt = autoText[sn].sizePt;
                    spanRun.font = autoText[sn].font;
                    spanRun.highlight = autoText[sn].highlight;
                    spanRun.strike = autoText[sn].strike;
                    spanRun.superscript = autoText[sn].superscript;
                    spanRun.subscript = autoText[sn].subscript;
                }
            } else if (n == QLatin1String("note")) {
                Block nb;
                nb.id = xml.attributes().value(QStringLiteral("text:id")).toString();
                const QString cls = xml.attributes().value(QStringLiteral("text:note-class")).toString();
                nb.type = cls == QLatin1String("endnote") ? QStringLiteral("endnote") : QStringLiteral("footnote");
                QString txt;
                while (!xml.atEnd()) {
                    xml.readNext();
                    if (xml.isEndElement() && xml.name() == QLatin1String("note"))
                        break;
                    if (xml.isCharacters())
                        txt += xml.text();
                }
                TextRun nr;
                nr.text = txt.trimmed();
                nb.runs.push_back(nr);
                if (nb.id.isEmpty() || doc.indexOf(nb.id) >= 0)
                    doc.assignId(nb);
                if (!spanRun.text.isEmpty() || !spanRun.field.isEmpty())
                    runs.push_back(spanRun);
                startRun();
                spanRun.noteId = nb.id;
                spanRun.superscript = true;
                runs.push_back(spanRun);
                startRun();
                doc.body.push_back(nb);
            } else if (n == QLatin1String("page-number") || n == QLatin1String("page-count")
                       || n == QLatin1String("date") || n == QLatin1String("title")
                       || n == QLatin1String("author-name") || n == QLatin1String("file-name")
                       || n == QLatin1String("word-count") || n == QLatin1String("user-field-get")) {
                if (!spanRun.text.isEmpty() || !spanRun.field.isEmpty())
                    runs.push_back(spanRun);
                startRun();
                if (n == QLatin1String("page-number"))
                    spanRun.field = QStringLiteral("page_number");
                else if (n == QLatin1String("page-count"))
                    spanRun.field = QStringLiteral("page_count");
                else if (n == QLatin1String("date"))
                    spanRun.field = QStringLiteral("date");
                else if (n == QLatin1String("title"))
                    spanRun.field = QStringLiteral("title");
                else if (n == QLatin1String("author-name"))
                    spanRun.field = QStringLiteral("author");
                else if (n == QLatin1String("file-name"))
                    spanRun.field = QStringLiteral("filename");
                else if (n == QLatin1String("word-count"))
                    spanRun.field = QStringLiteral("word_count");
                else {
                    spanRun.field = xml.attributes().value(QStringLiteral("text:name")).toString();
                    if (spanRun.field.isEmpty())
                        spanRun.field = QStringLiteral("custom");
                    spanRun.fieldArg = spanRun.field;
                }
                spanRun.text = xml.readElementText();
                runs.push_back(spanRun);
                startRun();
            } else if (n == QLatin1String("table-of-content")) {
                if (inText)
                    flushPara();
                Block toc;
                toc.type = QStringLiteral("toc");
                QString txt;
                while (!xml.atEnd()) {
                    xml.readNext();
                    if (xml.isEndElement() && xml.name() == QLatin1String("table-of-content"))
                        break;
                    if (xml.isCharacters())
                        txt += xml.text();
                }
                TextRun tr;
                tr.text = txt.simplified();
                toc.runs.push_back(tr);
                doc.assignId(toc);
                doc.body.push_back(toc);
            } else if (n == QLatin1String("bookmark-start") && importNativeBm) {
                Bookmark bm;
                bm.name = xml.attributes().value(QStringLiteral("text:name")).toString();
                bm.id = QStringLiteral("bm%1").arg(doc.bookmarks.size() + 1);
                bm.blockId = QStringLiteral("pending");
                doc.bookmarks.push_back(bm);
            } else if (n == QLatin1String("frame")) {
                QString w = xml.attributes().value(QStringLiteral("svg:width")).toString();
                if (w.isEmpty())
                    w = xml.attributes().value(QStringLiteral("width")).toString();
                w.remove(QStringLiteral("mm"));
                w.remove(QStringLiteral("cm"));
                pendingFrameWidthMm = w.toDouble();
                if (xml.attributes().value(QStringLiteral("svg:width")).toString().contains(QLatin1String("cm")))
                    pendingFrameWidthMm *= 10;
            } else if (n == QLatin1String("image")) {
                QString href = xml.attributes().value(QStringLiteral("href")).toString();
                if (href.isEmpty())
                    href = xml.attributes().value(QStringLiteral("xlink:href")).toString();
                if (!href.isEmpty()) {
                    bool hadText = !spanRun.text.trimmed().isEmpty();
                    for (const auto &r : runs) {
                        if (!r.text.trimmed().isEmpty())
                            hadText = true;
                    }
                    if (hadText)
                        flushPara();
                    else {
                        runs.clear();
                        startRun();
                        inText = false;
                    }
                    Block img;
                    img.type = QStringLiteral("image");
                    img.imagePath = href;
                    img.imageBytes = Zip::file(entries, href);
                    if (img.imageBytes.isEmpty())
                        img.imageBytes = Zip::file(entries, href.startsWith(QLatin1Char('/')) ? href.mid(1) : href);
                    img.imageWidthMm = pendingFrameWidthMm > 0 ? int(qRound(pendingFrameWidthMm)) : 80;
                    pendingFrameWidthMm = 0;
                    doc.assignId(img);
                    doc.body.push_back(img);
                }
            }
        } else if (xml.isCharacters() && inText && !xml.isWhitespace()) {
            spanRun.text += xml.text();
        } else if (xml.isCharacters() && inText) {
            spanRun.text += xml.text();
        } else if (xml.isEndElement()) {
            const auto n = xml.name();
            if (n == QLatin1String("span")) {
                if (!spanRun.text.isEmpty())
                    runs.push_back(spanRun);
                startRun();
            } else if (n == QLatin1String("table-cell") && inTable) {
                if (!spanRun.text.isEmpty())
                    runs.push_back(spanRun);
                TableCell cell;
                cell.runs = runs;
                tableRow.push_back(cell);
                runs.clear();
                startRun();
                inText = false;
            } else if (n == QLatin1String("table-row") && inTable) {
                tableBlock.table.push_back(tableRow);
            } else if (n == QLatin1String("table") && inTable) {
                doc.assignId(tableBlock);
                doc.body.push_back(tableBlock);
                inTable = false;
            } else if (n == QLatin1String("h")) {
                flushPara();
            } else if (n == QLatin1String("p") && inText && !inTable) {
                flushPara();
            } else if (n == QLatin1String("list")) {
                listLevel = qMax(0, listLevel - 1);
                if (listLevel == 0)
                    listKind.clear();
            }
        }
    }
    if (xml.hasError() && doc.body.isEmpty()) {
        if (error)
            *error = QStringLiteral("ODT XML: ") + xml.errorString();
        return false;
    }
    if (doc.body.isEmpty())
        doc = Document::blank(doc.title);
    return true;
}

bool loadOdtFile(const QString &path, Document &doc, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot read ") + path;
        return false;
    }
    return loadOdt(f.readAll(), doc, error);
}

} // namespace aioffice
