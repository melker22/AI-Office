#include "core/Odf.h"

#include "core/Xml.h"
#include "core/Zip.h"

#include <QFile>
#include <QImage>
#include <QMap>
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

QString emitSpan(SpanCounter &sc, const TextRun &r)
{
    if (r.text.isEmpty())
        return {};
    const bool fmt = r.bold || r.italic || r.underline || !r.font.isEmpty() || r.sizePt > 0
        || !r.color.isEmpty();
    if (!fmt)
        return xmlEscape(r.text);
    QString key = QStringLiteral("%1|%2|%3|%4|%5|%6")
                      .arg(r.bold)
                      .arg(r.italic)
                      .arg(r.underline)
                      .arg(r.font)
                      .arg(r.sizePt)
                      .arg(r.color);
    int id = sc.seen.value(key, 0);
    if (id == 0) {
        id = ++sc.n;
        sc.seen[key] = id;
        QString inner;
        if (r.bold)
            inner += QLatin1String("fo:font-weight=\"bold\" ");
        if (r.italic)
            inner += QLatin1String("fo:font-style=\"italic\" ");
        if (r.underline)
            inner += QLatin1String("style:text-underline-style=\"solid\" ");
        if (!r.font.isEmpty())
            inner += QStringLiteral("style:font-name=\"%1\" ").arg(xmlEscape(r.font));
        if (r.sizePt > 0)
            inner += QStringLiteral("fo:font-size=\"%1pt\" ").arg(r.sizePt);
        if (!r.color.isEmpty())
            inner += QStringLiteral("fo:color=\"%1\" ").arg(xmlEscape(r.color));
        sc.stylesXml += QStringLiteral(
                            "<style:style style:name=\"T%1\" style:family=\"text\"><style:text-properties %2/></style:style>")
                            .arg(id)
                            .arg(inner);
    }
    return QStringLiteral("<text:span text:style-name=\"T%1\">%2</text:span>").arg(id).arg(xmlEscape(r.text));
}

QString spansXml(SpanCounter &sc, const QVector<TextRun> &runs)
{
    QString xml;
    for (const auto &r : runs)
        xml += emitSpan(sc, r);
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
    const QString pageLayout = QStringLiteral(
        "<style:page-layout style:name=\"pm1\"><style:page-layout-properties "
        "fo:page-width=\"%1mm\" fo:page-height=\"%2mm\" style:print-orientation=\"%3\" "
        "fo:margin-top=\"%4mm\" fo:margin-right=\"%5mm\" fo:margin-bottom=\"%6mm\" fo:margin-left=\"%7mm\"/>"
        "</style:page-layout>")
                                   .arg(wMm)
                                   .arg(hMm)
                                   .arg(doc.page.orientation)
                                   .arg(doc.page.marginsMm[0])
                                   .arg(doc.page.marginsMm[1])
                                   .arg(doc.page.marginsMm[2])
                                   .arg(doc.page.marginsMm[3]);

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

    SpanCounter sc;
    QString bodyXml;
    QVector<ZipEntry> pictures;
    int imgN = 0;
    int i = 0;
    const auto &body = doc.body;
    while (i < body.size()) {
        const Block &b = body[i];
        const QString st = odfStyleName(styleForBlock(b));
        if (b.type == QLatin1String("list-item")) {
            const QString kind = b.listKind;
            bodyXml += QLatin1String("<text:list>");
            while (i < body.size() && body[i].type == QLatin1String("list-item") && body[i].listKind == kind) {
                QString inner = QStringLiteral("<text:p text:style-name=\"%1\">%2</text:p>")
                                    .arg(odfStyleName(styleForBlock(body[i])), spansXml(sc, body[i].runs));
                const int lv = qMax(1, body[i].level);
                for (int n = 1; n < lv; ++n)
                    inner = QStringLiteral("<text:list><text:list-item>%1</text:list-item></text:list>").arg(inner);
                bodyXml += QStringLiteral("<text:list-item>%1</text:list-item>").arg(inner);
                ++i;
            }
            bodyXml += QLatin1String("</text:list>");
            continue;
        }
        if (b.type == QLatin1String("heading")) {
            bodyXml += QStringLiteral("<text:h text:style-name=\"%1\" text:outline-level=\"%2\">%3</text:h>")
                           .arg(st)
                           .arg(b.level > 0 ? b.level : 1)
                           .arg(spansXml(sc, b.runs));
        } else if (b.type == QLatin1String("break")) {
            bodyXml += QLatin1String("<text:p text:style-name=\"PBreak\"/>");
        } else if (b.type == QLatin1String("table")) {
            bodyXml += QLatin1String(
                "<table:table xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\">");
            for (const auto &row : b.table) {
                bodyXml += QLatin1String("<table:table-row>");
                for (const auto &cell : row) {
                    bodyXml += QStringLiteral(
                        "<table:table-cell><text:p text:style-name=\"Standard\">%1</text:p></table:table-cell>")
                                   .arg(spansXml(sc, cell.runs));
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
            bodyXml += QStringLiteral("<text:p text:style-name=\"%1\">%2</text:p>")
                           .arg(st, spansXml(sc, b.runs));
        }
        ++i;
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

    const QString meta = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<office:document-meta xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" office:version=\"1.3\">"
        "<office:meta><dc:title>%1</dc:title></office:meta></office:document-meta>")
                             .arg(xmlEscape(doc.title));

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
    if (!a.value(QStringLiteral("fo:color")).isEmpty())
        r.color = a.value(QStringLiteral("fo:color")).toString();
    if (!a.value(QStringLiteral("fo:font-size")).isEmpty()) {
        QString s = a.value(QStringLiteral("fo:font-size")).toString();
        s.remove(QStringLiteral("pt"));
        r.sizePt = s.toDouble();
    }
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
            if (!st.isStartElement() || st.name() != QLatin1String("page-layout-properties"))
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
                if (stn == QLatin1String("PBreak")) {
                    Block br;
                    br.type = QStringLiteral("break");
                    br.breakKind = QStringLiteral("page");
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
                }
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
