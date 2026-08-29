#include "core/Docx.h"

#include "core/Xml.h"
#include "core/Zip.h"

#include <QFile>
#include <QMap>
#include <QXmlStreamReader>
#include <cmath>

namespace aioffice {
namespace {

QString wAlign(const QString &a)
{
    if (a == QLatin1String("center"))
        return QStringLiteral("center");
    if (a == QLatin1String("right"))
        return QStringLiteral("right");
    if (a == QLatin1String("justify"))
        return QStringLiteral("both");
    return QStringLiteral("left");
}

QString runsToWml(const QVector<TextRun> &runs)
{
    QString xml;
    for (const auto &r : runs) {
        QString rPr;
        if (r.bold)
            rPr += QLatin1String("<w:b/>");
        if (r.italic)
            rPr += QLatin1String("<w:i/>");
        if (r.underline)
            rPr += QLatin1String("<w:u w:val=\"single\"/>");
        if (r.sizePt > 0)
            rPr += QStringLiteral("<w:sz w:val=\"%1\"/><w:szCs w:val=\"%1\"/>").arg(int(r.sizePt * 2));
        if (!r.font.isEmpty())
            rPr += QStringLiteral("<w:rFonts w:ascii=\"%1\" w:hAnsi=\"%1\"/>").arg(xmlEscape(r.font));
        if (!r.color.isEmpty()) {
            QString c = r.color;
            c.remove(QLatin1Char('#'));
            rPr += QStringLiteral("<w:color w:val=\"%1\"/>").arg(c);
        }
        QString t = xmlEscape(r.text);
        const QString space = r.text.startsWith(QLatin1Char(' ')) || r.text.endsWith(QLatin1Char(' '))
            ? QStringLiteral(" xml:space=\"preserve\"")
            : QString();
        xml += QStringLiteral("<w:r>");
        if (!rPr.isEmpty())
            xml += QStringLiteral("<w:rPr>%1</w:rPr>").arg(rPr);
        xml += QStringLiteral("<w:t%1>%2</w:t></w:r>").arg(space, t);
    }
    return xml;
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
    if (!b.align.isEmpty())
        xml += QStringLiteral("<w:jc w:val=\"%1\"/>").arg(wAlign(b.align));
    if (b.type == QLatin1String("list-item")) {
        const int numId = b.listKind == QLatin1String("decimal") ? 1 : 2;
        xml += QStringLiteral("<w:numPr><w:ilvl w:val=\"%1\"/><w:numId w:val=\"%2\"/></w:numPr>")
                   .arg(qMax(0, b.level - 1))
                   .arg(numId);
    }
    if (xml.isEmpty())
        return {};
    return QStringLiteral("<w:pPr>%1</w:pPr>").arg(xml);
}

int twipsMm(double mm) { return int(std::lround(mm * 1440.0 / 25.4)); }
double mmTwips(int tw) { return tw * 25.4 / 1440.0; }

} // namespace

bool saveDocx(const Document &doc, const QString &path, QString *error)
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
    int relN = 2;
    for (const auto &b : doc.body) {
        if (b.type == QLatin1String("table")) {
            body += QLatin1String("<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/></w:tblPr>");
            for (const auto &row : b.table) {
                body += QLatin1String("<w:tr>");
                for (const auto &cell : row)
                    body += QStringLiteral("<w:tc><w:p>%1</w:p></w:tc>").arg(runsToWml(cell.runs));
                body += QLatin1String("</w:tr>");
            }
            body += QLatin1String("</w:tbl>");
            continue;
        }
        if (b.type == QLatin1String("break")) {
            body += QLatin1String("<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>");
            continue;
        }
        if (b.type == QLatin1String("image")) {
            ++imgN;
            ++relN;
            const QString rid = QStringLiteral("rId%1").arg(relN);
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
        body += QStringLiteral("<w:p>%1%2</w:p>").arg(pPr(b), runsToWml(b.runs));
    }
    body += QStringLiteral("<w:sectPr><w:pgSz w:w=\"%1\" w:h=\"%2\"%3/>"
                           "<w:pgMar w:top=\"%4\" w:right=\"%5\" w:bottom=\"%6\" w:left=\"%7\"/></w:sectPr>")
                .arg(pgW)
                .arg(pgH)
                .arg(orient)
                .arg(mt)
                .arg(mr)
                .arg(mb)
                .arg(ml);

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

    QString contentTypes = QStringLiteral(
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

    QVector<ZipEntry> entries;
    entries.push_back({QStringLiteral("[Content_Types].xml"), contentTypes.toUtf8(), false});
    entries.push_back({QStringLiteral("_rels/.rels"), rels.toUtf8(), false});
    entries.push_back({QStringLiteral("word/_rels/document.xml.rels"), docRels.toUtf8(), false});
    entries.push_back({QStringLiteral("word/document.xml"), documentXml.toUtf8(), false});
    entries.push_back({QStringLiteral("word/styles.xml"), styles.toUtf8(), false});
    entries.push_back({QStringLiteral("word/numbering.xml"), numbering.toUtf8(), false});
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
        }
    }

    QXmlStreamReader xml(xmlBytes);
    QVector<TextRun> runs;
    TextRun run;
    QString pStyle;
    QString align;
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
        } else {
            b.type = QStringLiteral("paragraph");
            b.style = pStyle.isEmpty() ? QStringLiteral("Normal") : pStyle;
        }
        b.align = align;
        b.runs = runs;
        if (b.runs.isEmpty())
            b.runs.push_back(TextRun{});
        if (inCell) {
            TableCell cell;
            cell.runs = b.runs;
            tableRow.push_back(cell);
        } else {
            doc.assignId(b);
            doc.body.push_back(b);
        }
        runs.clear();
        pStyle.clear();
        align.clear();
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
                numId = 0;
            } else if (n == QLatin1String("pStyle")) {
                pStyle = xml.attributes().value(QStringLiteral("w:val")).toString();
            } else if (n == QLatin1String("numId")) {
                numId = xml.attributes().value(QStringLiteral("w:val")).toInt();
            } else if (n == QLatin1String("ilvl")) {
                ilvl = xml.attributes().value(QStringLiteral("w:val")).toInt();
            } else if (n == QLatin1String("jc")) {
                const auto v = xml.attributes().value(QStringLiteral("w:val")).toString();
                align = (v == QLatin1String("both")) ? QStringLiteral("justify") : v;
            } else if (n == QLatin1String("r")) {
                inR = true;
                run = TextRun{};
            } else if (n == QLatin1String("b") && inR) {
                run.bold = true;
            } else if (n == QLatin1String("i") && inR) {
                run.italic = true;
            } else if (n == QLatin1String("u") && inR) {
                run.underline = true;
            } else if (n == QLatin1String("t") && inR) {
                run.text += xml.readElementText();
            } else if (n == QLatin1String("blip")) {
                blipRid = xml.attributes().value(QStringLiteral("r:embed")).toString();
            } else if (n == QLatin1String("br")) {
                if (xml.attributes().value(QStringLiteral("w:type")) == QLatin1String("page")) {
                    skipEmptyP = true;
                    if (inP)
                        flushP();
                    Block br;
                    br.type = QStringLiteral("break");
                    br.breakKind = QStringLiteral("page");
                    doc.assignId(br);
                    doc.body.push_back(br);
                    inP = false;
                    runs.clear();
                }
            }
        } else if (xml.isEndElement()) {
            const auto n = xml.name();
            if (n == QLatin1String("r") && inR) {
                if (!run.text.isEmpty() || run.bold || run.italic)
                    runs.push_back(run);
                inR = false;
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
