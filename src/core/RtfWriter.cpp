#include "core/RtfWriter.h"

#include "core/Document.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

namespace aioffice {

namespace {

constexpr int kTwipsPerMm = 567; // 1440 twips/inch / 25.4 mm/inch ≈ 56.692913, rounded for RTF

struct RtfOut {
    const Document *doc = nullptr;
    QStringList *warnings = nullptr;

    QStringList fontNames;
    QMap<QString, int> fontIndex;
    QStringList colorKeys;
    QMap<QString, int> colorIndex;
    QMap<QString, int> listKindToId;
    int nextListId = 1;
    QMap<QString, QString> bookmarkExportNames;
    QMap<QString, int> seqShown;
    int defaultFont = 0;
    int defaultSizeHalfPt = 24;

    void warnOnce(const QString &msg)
    {
        if (!warnings || warnings->contains(msg))
            return;
        warnings->append(msg);
    }

    int fontId(const QString &name)
    {
        const QString key = name.isEmpty() ? QStringLiteral("Liberation Serif") : name;
        if (fontIndex.contains(key))
            return fontIndex.value(key);
        const int id = fontNames.size();
        fontNames.append(key);
        fontIndex.insert(key, id);
        return id;
    }

    int colorId(const QString &hex)
    {
        if (hex.isEmpty())
            return 0;
        QString key = hex.trimmed().toLower();
        if (key.startsWith(QLatin1Char('#')))
            key = key.mid(1);
        if (key.size() != 6)
            return 0;
        if (colorIndex.contains(key))
            return colorIndex.value(key);
        const int id = colorKeys.size() + 1;
        colorKeys.append(key);
        colorIndex.insert(key, id);
        return id;
    }

    static bool parseRgb(const QString &hex, int &r, int &g, int &b)
    {
        QString key = hex.trimmed().toLower();
        if (key.startsWith(QLatin1Char('#')))
            key = key.mid(1);
        if (key.size() != 6)
            return false;
        bool ok = false;
        r = key.mid(0, 2).toInt(&ok, 16);
        if (!ok)
            return false;
        g = key.mid(2, 2).toInt(&ok, 16);
        if (!ok)
            return false;
        b = key.mid(4, 2).toInt(&ok, 16);
        return ok;
    }

    int langLcid(const QString &lang)
    {
        if (lang.isEmpty())
            return 0;
        const QString l = lang.toLower();
        if (l.startsWith(QStringLiteral("pt")))
            return 2070;
        if (l.startsWith(QStringLiteral("en")))
            return 1033;
        if (l.startsWith(QStringLiteral("de")))
            return 1031;
        if (l.startsWith(QStringLiteral("fr")))
            return 1036;
        if (l.startsWith(QStringLiteral("es")))
            return 1034;
        if (l.startsWith(QStringLiteral("ar")))
            return 1025;
        return 1033;
    }

    QString exportedBookmarkName(const QString &id)
    {
        if (bookmarkExportNames.contains(id))
            return bookmarkExportNames.value(id);
        QString name = id;
        name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
        if (name.isEmpty() || name.at(0).isDigit())
            name = QStringLiteral("bm_") + name;
        QString base = name;
        int n = 0;
        QSet<QString> used = QSet<QString>(bookmarkExportNames.cbegin(), bookmarkExportNames.cend());
        while (used.contains(name)) {
            name = base + QStringLiteral("_") + QString::number(++n);
        }
        bookmarkExportNames.insert(id, name);
        return name;
    }

    QString bm(const QString &blockOrBmId)
    {
        for (const auto &bm : doc->bookmarks) {
            if (bm.id == blockOrBmId || bm.blockId == blockOrBmId)
                return exportedBookmarkName(bm.name.isEmpty() ? bm.id : bm.name);
        }
        return exportedBookmarkName(blockOrBmId);
    }

    static QString escapeText(const QString &text)
    {
        QString out;
        out.reserve(text.size() * 2);
        for (const QChar c : text) {
            if (c == QLatin1Char('\\'))
                out += QStringLiteral("\\\\");
            else if (c == QLatin1Char('{'))
                out += QStringLiteral("\\{");
            else if (c == QLatin1Char('}'))
                out += QStringLiteral("\\}");
            else if (c == QLatin1Char('\n'))
                out += QStringLiteral("\\line ");
            else if (c == QLatin1Char('\t'))
                out += QStringLiteral("\\tab ");
            else if (c.unicode() < 128)
                out += c;
            else {
                out += QStringLiteral("\\u%1?").arg(static_cast<int>(c.unicode()));
            }
        }
        return out;
    }

    QString charProps(const TextRun &r, int defFont, int defSizeHalfPt)
    {
        QString p;
        const int fid = fontId(r.font.isEmpty() ? QString() : r.font);
        if (fid != defFont)
            p += QStringLiteral("\\f%1").arg(fid);
        const int fs = r.sizePt > 0 ? qRound(r.sizePt * 2) : defSizeHalfPt;
        if (fs != defSizeHalfPt)
            p += QStringLiteral("\\fs%1").arg(fs);
        if (r.bold)
            p += QStringLiteral("\\b");
        if (r.italic)
            p += QStringLiteral("\\i");
        if (r.underline)
            p += QStringLiteral("\\ul");
        if (r.strike)
            p += QStringLiteral("\\strike");
        if (r.superscript)
            p += QStringLiteral("\\super");
        if (r.subscript)
            p += QStringLiteral("\\sub");
        const int cf = colorId(r.color);
        if (cf > 0)
            p += QStringLiteral("\\cf%1").arg(cf);
        const int hi = colorId(r.highlight);
        if (hi > 0)
            p += QStringLiteral("\\highlight%1").arg(hi);
        if (!r.lang.isEmpty()) {
            const int lc = langLcid(r.lang);
            if (lc > 0)
                p += QStringLiteral("\\lang%1").arg(lc);
        }
        return p;
    }

    QJsonObject fieldArgObject(const TextRun &r)
    {
        const QJsonDocument jd = QJsonDocument::fromJson(r.fieldArg.toUtf8());
        return jd.isObject() ? jd.object() : QJsonObject{};
    }

    bool nativeFieldInstr(const TextRun &r, QString &instr)
    {
        if (r.field == QLatin1String("page_number")) {
            instr = QStringLiteral(" PAGE ");
            return true;
        }
        if (r.field == QLatin1String("page_count")
            || (r.field == QLatin1String("stat") && r.fieldArg == QLatin1String("pages"))) {
            instr = QStringLiteral(" NUMPAGES ");
            return true;
        }
        if (r.field == QLatin1String("date")) {
            instr = QStringLiteral(" DATE ");
            return true;
        }
        if (r.field == QLatin1String("title")) {
            instr = QStringLiteral(" TITLE ");
            return true;
        }
        if (r.field == QLatin1String("author")) {
            instr = QStringLiteral(" AUTHOR ");
            return true;
        }
        if (r.field == QLatin1String("filename") || r.field == QLatin1String("file_name")) {
            instr = QStringLiteral(" FILENAME ");
            return true;
        }
        if (r.field == QLatin1String("time")) {
            instr = QStringLiteral(" TIME ");
            return true;
        }
        if (r.field == QLatin1String("merge") || r.field == QLatin1String("mail_merge")) {
            instr = QStringLiteral(" MERGEFIELD %1 ").arg(r.fieldArg);
            return true;
        }
        if (r.field == QLatin1String("var") || r.field == QLatin1String("variable")
            || r.field == QLatin1String("custom")) {
            instr = QStringLiteral(" DOCVARIABLE %1 ").arg(r.fieldArg);
            return true;
        }
        if (r.field == QLatin1String("input")) {
            instr = QStringLiteral(" FILLIN \"%1\" ").arg(r.fieldArg);
            return true;
        }
        if (r.field == QLatin1String("xref")) {
            const QString target = bm(r.fieldArg);
            if (r.xrefKind == QLatin1String("page"))
                instr = QStringLiteral(" PAGEREF %1 ").arg(target);
            else
                instr = QStringLiteral(" REF %1 ").arg(target);
            return true;
        }
        if (r.field == QLatin1String("seq")) {
            const QJsonObject jo = fieldArgObject(r);
            const QString name = jo.value(QStringLiteral("name")).toString();
            QString fmt = QStringLiteral("ARABIC");
            const QString format = jo.value(QStringLiteral("format")).toString();
            if (format == QLatin1String("roman"))
                fmt = QStringLiteral("ROMAN");
            else if (format == QLatin1String("alpha"))
                fmt = QStringLiteral("ALPHABETIC");
            instr = QStringLiteral(" SEQ %1 \\* %2 ").arg(name, fmt);
            return true;
        }
        if (!r.link.isEmpty()) {
            instr = QStringLiteral(" HYPERLINK \"%1\" ").arg(r.link);
            return true;
        }
        return false;
    }

    QString fieldResult(const TextRun &r)
    {
        if (r.field == QLatin1String("seq")) {
            const QJsonObject jo = fieldArgObject(r);
            const QString name = jo.value(QStringLiteral("name")).toString();
            const QString format = jo.value(QStringLiteral("format")).toString(QStringLiteral("arabic"));
            const int n = ++seqShown[name];
            return formatPageNumber(n, format);
        }
        return runDisplayText(*doc, r);
    }

    QString emitRun(const TextRun &r, int defFont, int defSizeHalfPt)
    {
        QString out;
        if (!r.noteId.isEmpty()) {
            const Block *nb = doc->findBlock(r.noteId);
            if (nb) {
                QString body;
                for (const auto &nr : nb->runs)
                    body += escapeText(nr.text);
                out += QStringLiteral("{\\footnote{\\pard\\plain %1\\par}}").arg(body);
            }
            return out;
        }
        if (!r.field.isEmpty()) {
            QString instr;
            const QString result = fieldResult(r);
            if (nativeFieldInstr(r, instr)) {
                out += QStringLiteral("{\\field{\\*\\fldinst %1}{\\fldrslt %2}}")
                           .arg(escapeText(instr.trimmed()), escapeText(result));
            } else {
                out += escapeText(result);
            }
            return out;
        }
        const QString props = charProps(r, defFont, defSizeHalfPt);
        const QString txt = escapeText(r.text);
        if (props.isEmpty())
            out += txt;
        else
            out += props + QLatin1Char(' ') + txt + QStringLiteral("\\plain");
        return out;
    }

    QString emitRuns(const QVector<TextRun> &runs, int defFont, int defSizeHalfPt)
    {
        QString out;
        for (const auto &r : runs)
            out += emitRun(r, defFont, defSizeHalfPt);
        return out;
    }

    QString paraAlign(const Block &b)
    {
        const QString a = b.align.toLower();
        if (a == QLatin1String("center"))
            return QStringLiteral("\\qc");
        if (a == QLatin1String("right") || a == QLatin1String("end"))
            return QStringLiteral("\\qr");
        if (a == QLatin1String("justify"))
            return QStringLiteral("\\qj");
        return QStringLiteral("\\ql");
    }

    QString paraProps(const Block &b, bool inTable = false)
    {
        QString p = inTable ? QStringLiteral("\\intbl") : QStringLiteral("\\pard");
        p += paraAlign(b);
        if (b.dir == QLatin1String("rtl"))
            p += QStringLiteral("\\rtlpar");
        if (!b.lang.isEmpty()) {
            const int lc = langLcid(b.lang);
            if (lc > 0)
                p += QStringLiteral("\\lang%1").arg(lc);
        }
        if (b.indentLeftMm > 0)
            p += QStringLiteral("\\li%1").arg(qRound(b.indentLeftMm * kTwipsPerMm / 10.0));
        if (b.indentRightMm > 0)
            p += QStringLiteral("\\ri%1").arg(qRound(b.indentRightMm * kTwipsPerMm / 10.0));
        if (b.indentFirstMm != 0)
            p += QStringLiteral("\\fi%1").arg(qRound(b.indentFirstMm * kTwipsPerMm / 10.0));
        if (b.spaceBeforePt >= 0)
            p += QStringLiteral("\\sb%1").arg(qRound(b.spaceBeforePt * 20));
        if (b.spaceAfterPt >= 0)
            p += QStringLiteral("\\sa%1").arg(qRound(b.spaceAfterPt * 20));
        return p + QLatin1Char(' ');
    }

    QString bookmarksWrap(const QString &blockId, const QString &inner)
    {
        QString out = inner;
        for (const auto &bm : doc->bookmarks) {
            if (bm.blockId != blockId)
                continue;
            const QString name = exportedBookmarkName(bm.name.isEmpty() ? bm.id : bm.name);
            out = QStringLiteral("\\bkmkstart %1 %2\\bkmkend %3").arg(name, out, name);
        }
        return out;
    }

    QString commentSuffix(const Block &b)
    {
        if (b.comments.isEmpty())
            return {};
        warnOnce(QStringLiteral("RTF export: block comments exported as plain text"));
        QString s;
        for (const auto &c : b.comments) {
            s += QStringLiteral(" [") + c.author + QStringLiteral(": ") + c.text + QLatin1Char(']');
        }
        return escapeText(s);
    }

    int listIdForKind(const QString &kind)
    {
        const QString k = kind.isEmpty() ? QStringLiteral("bullet") : kind;
        if (listKindToId.contains(k))
            return listKindToId.value(k);
        const int id = nextListId++;
        listKindToId.insert(k, id);
        return id;
    }

    QString pictPng(const Block &b)
    {
        QByteArray bytes = b.imageBytes;
        if (bytes.isEmpty() && !b.imagePath.isEmpty()) {
            QFile f(b.imagePath);
            if (f.open(QIODevice::ReadOnly))
                bytes = f.readAll();
        }
        if (bytes.isEmpty()) {
            warnOnce(QStringLiteral("RTF export: image block has no bytes"));
            return escapeText(b.altText.isEmpty() ? b.plainText() : b.altText);
        }
        QString hex;
        hex.reserve(bytes.size() * 2);
        for (unsigned char ch : bytes) {
            hex += QStringLiteral("%1").arg(static_cast<int>(ch), 2, 16, QLatin1Char('0'));
        }
        const int wTwips = qMax(100, qRound(b.imageWidthMm * kTwipsPerMm / 10.0));
        return QStringLiteral("{\\pict\\pngblip\\picw%1\\pich%2 %3}")
            .arg(wTwips)
            .arg(wTwips)
            .arg(hex);
    }

    void collectFontsColors(const Document &doc)
    {
        for (const auto &s : doc.styles) {
            fontId(s.font);
            colorId(s.color);
        }
        const auto scanRuns = [&](const QVector<TextRun> &runs) {
            for (const auto &r : runs) {
                if (!r.font.isEmpty())
                    fontId(r.font);
                colorId(r.color);
                colorId(r.highlight);
            }
        };
        for (const auto &b : doc.body) {
            scanRuns(b.runs);
            scanRuns(b.captionRuns);
            for (const auto &row : b.table)
                for (const auto &cell : row)
                    scanRuns(cell.runs);
        }
        defaultFont = fontId(QStringLiteral("Liberation Serif"));
    }

    QString fontTbl() const
    {
        QString t = QStringLiteral("{\\fonttbl");
        for (int i = 0; i < fontNames.size(); ++i)
            t += QStringLiteral("{\\f%1\\fnil %2;}").arg(i).arg(fontNames.at(i));
        t += QLatin1Char('}');
        return t;
    }

    QString colorTbl() const
    {
        QString t = QStringLiteral("{\\colortbl ;");
        for (const QString &key : colorKeys) {
            int r = 0, g = 0, b = 0;
            parseRgb(key, r, g, b);
            t += QStringLiteral("\\red%1\\green%2\\blue%3;").arg(r).arg(g).arg(b);
        }
        t += QLatin1Char('}');
        return t;
    }

    QString styleSheet() const
    {
        QString t = QStringLiteral("{\\stylesheet");
        t += QStringLiteral("{\\s0\\ql\\fs24 Normal;}");
        int sn = 1;
        for (const auto &s : doc->styles) {
            if (s.kind == QLatin1String("character"))
                continue;
            QString row = QStringLiteral("{\\s%1").arg(sn++);
            if (s.bold)
                row += QStringLiteral("\\b");
            if (s.italic)
                row += QStringLiteral("\\i");
            row += QStringLiteral("\\fs%1 ").arg(qRound(s.sizePt > 0 ? s.sizePt * 2 : 24));
            row += escapeText(s.name) + QLatin1Char(';');
            row += QLatin1Char('}');
            t += row;
        }
        t += QLatin1Char('}');
        return t;
    }

    QString listTable() const
    {
        if (listKindToId.isEmpty())
            return {};
        QString t = QStringLiteral("{\\*\\listtable");
        for (auto it = listKindToId.cbegin(); it != listKindToId.cend(); ++it) {
            const bool numbered = it.key() != QLatin1String("bullet");
            t += QStringLiteral("{\\list\\listtemplateid%1{\\listlevel\\levelnfc%2\\leveljc0\\levelstartat1")
                     .arg(it.value())
                     .arg(numbered ? 23 : 23);
            if (numbered)
                t += QStringLiteral("\\leveltext\\'02\\'00.;}{\\levelnumbers\\'01;}");
            else
                t += QStringLiteral("\\leveltext\\'01\\u-3913 ?;}{\\levelnumbers;}");
            t += QStringLiteral("}\\listid%1}").arg(it.value());
        }
        t += QLatin1Char('}');
        t += QStringLiteral("{\\*\\listoverridetable");
        for (auto it = listKindToId.cbegin(); it != listKindToId.cend(); ++it) {
            t += QStringLiteral("{\\listoverride\\listid%1\\listoverridecount0\\ls%1}")
                     .arg(it.value());
        }
        t += QLatin1Char('}');
        return t;
    }

    QString emitTable(const Block &b)
    {
        QString out;
        int rowIdx = 0;
        const int cols = b.table.isEmpty() ? 0 : b.table.first().size();
        QVector<int> cellBoundaries;
        cellBoundaries.append(0);
        int acc = 0;
        const int colW = cols > 0 ? 9000 / cols : 9000;
        for (int c = 0; c < cols; ++c) {
            acc += colW;
            cellBoundaries.append(acc);
        }
        for (const auto &row : b.table) {
            QString rowDef = QStringLiteral("\\trowd");
            if (b.headerRow && rowIdx == 0)
                rowDef += QStringLiteral("\\trhdr");
            for (int cx : cellBoundaries)
                rowDef += QStringLiteral("\\cellx%1").arg(cx);
            QString cells;
            for (const auto &cell : row) {
                cells += paraProps(b, true) + emitRuns(cell.runs, defaultFont, defaultSizeHalfPt) + QStringLiteral("\\cell ");
            }
            out += rowDef + cells + QStringLiteral("\\row\n");
            ++rowIdx;
        }
        return out;
    }

    QString bodyText()
    {
        QString body;
        const auto &blocks = doc->body;
        for (int i = 0; i < blocks.size(); ++i) {
            const Block &b = blocks[i];
            if (b.type == QLatin1String("footnote") || b.type == QLatin1String("endnote"))
                continue;
            if (!b.shapeKind.isEmpty() || !b.qrText.isEmpty() || b.formKind == QLatin1String("dropdown")) {
                warnOnce(QStringLiteral("RTF export: shapes/forms exported as plain text"));
                body += paraProps(b) + escapeText(b.plainText()) + commentSuffix(b) + QStringLiteral("\\par\n");
                continue;
            }
            if (b.type == QLatin1String("break")) {
                if (b.breakKind == QLatin1String("column"))
                    body += QStringLiteral("\\column\\par\n");
                else
                    body += QStringLiteral("\\page\\par\n");
                continue;
            }
            if (b.type == QLatin1String("list-item")) {
                const QString kind = b.listKind;
                const int ls = listIdForKind(kind);
                while (i < blocks.size() && blocks[i].type == QLatin1String("list-item")
                       && blocks[i].listKind == kind) {
                    const Block &li = blocks[i];
                    QString inner = paraProps(li) + QStringLiteral("\\ls%1 ").arg(ls)
                        + emitRuns(li.runs, defaultFont, defaultSizeHalfPt) + commentSuffix(li)
                        + QStringLiteral("\\par\n");
                    body += bookmarksWrap(li.id, inner);
                    ++i;
                }
                --i;
                continue;
            }
            if (b.type == QLatin1String("table")) {
                body += emitTable(b) + QStringLiteral("\\pard\\par\n");
                continue;
            }
            if (b.type == QLatin1String("image")) {
                body += paraProps(b) + pictPng(b) + commentSuffix(b) + QStringLiteral("\\par\n");
                continue;
            }
            QString inner = paraProps(b);
            if (b.type == QLatin1String("heading")) {
                inner += QStringLiteral("\\sb240\\sa120 ");
            }
            inner += emitRuns(b.runs, defaultFont, defaultSizeHalfPt) + commentSuffix(b) + QStringLiteral("\\par\n");
            body += bookmarksWrap(b.id, inner);
        }
        if (!doc->revisions.isEmpty())
            warnOnce(QStringLiteral("RTF export: tracked revisions exported as plain text"));
        return body;
    }
};

} // namespace

bool saveRtf(const Document &doc, const QString &path, QString *error, QStringList *warnings)
{
    if (path.isEmpty()) {
        if (error)
            *error = QStringLiteral("path is required");
        return false;
    }
    RtfOut o;
    o.doc = &doc;
    o.warnings = warnings;
    o.collectFontsColors(doc);
    for (const auto &b : doc.body) {
        if (b.type == QLatin1String("list-item"))
            o.listIdForKind(b.listKind);
    }
    for (const auto &bm : doc.bookmarks)
        o.exportedBookmarkName(bm.name.isEmpty() ? bm.id : bm.name);

    QString rtf = QStringLiteral("{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0\\deflang1033\n");
    rtf += o.fontTbl() + QLatin1Char('\n');
    rtf += o.colorTbl() + QLatin1Char('\n');
    rtf += o.styleSheet() + QLatin1Char('\n');
    rtf += o.listTable();
    rtf += QStringLiteral("\\viewkind4\n");
    rtf += o.bodyText();
    rtf += QLatin1Char('}');

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Cannot write ") + path;
        return false;
    }
    f.write(rtf.toUtf8());
    return true;
}

} // namespace aioffice
