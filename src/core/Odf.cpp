#include "core/Odf.h"

#include "core/FieldExpr.h"
#include "core/Preview.h"
#include "core/Xml.h"
#include "core/Zip.h"

#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>

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
    int dropN = 0;
    QString stylesXml;
    QMap<QString, int> seen;
};

struct ParaStyleCounter {
    int n = 0;
    QString stylesXml;
    QMap<QString, QString> seen; // key -> auto style name
};

QString paraStyleForBlock(ParaStyleCounter &pc, const Block &b)
{
    const QString parent = odfStyleName(styleForBlock(b));
    const bool needAlign = !b.align.isEmpty() && isValidBlockAlign(b.align);
    const bool needDir = !b.dir.isEmpty() && b.dir != QLatin1String("auto");
    if (!needAlign && !needDir)
        return parent;
    const QString key = parent + QLatin1Char('|') + b.align + QLatin1Char('|') + b.dir;
    if (pc.seen.contains(key))
        return pc.seen.value(key);
    const QString name = QStringLiteral("AioP%1").arg(++pc.n);
    pc.seen.insert(key, name);
    QString pp;
    if (needAlign)
        pp += QStringLiteral("fo:text-align=\"%1\" ").arg(xmlEscape(b.align));
    if (needDir) {
        const QString wm = b.dir == QLatin1String("rtl") ? QStringLiteral("rl-tb") : QStringLiteral("lr-tb");
        pp += QStringLiteral("style:writing-mode=\"%1\" ").arg(wm);
    }
    pc.stylesXml += QStringLiteral(
                         "<style:style style:name=\"%1\" style:family=\"paragraph\" style:parent-style-name=\"%2\">"
                         "<style:paragraph-properties %3/></style:style>")
                         .arg(xmlEscape(name), xmlEscape(parent), pp);
    return name;
}

struct ParaStyleProps {
    QString align;
    QString dir;
};

void collectParaStyles(const QByteArray &bytes, QMap<QString, ParaStyleProps> &out)
{
    if (bytes.isEmpty())
        return;
    QXmlStreamReader xr(bytes);
    QString curName;
    while (!xr.atEnd()) {
        xr.readNext();
        if (xr.isStartElement()) {
            if (xr.name() == QLatin1String("style"))
                curName = xr.attributes().value(QStringLiteral("style:name")).toString();
            else if (xr.name() == QLatin1String("paragraph-properties") && !curName.isEmpty()) {
                ParaStyleProps p = out.value(curName);
                const auto a = xr.attributes();
                const QString ta = a.value(QStringLiteral("fo:text-align")).toString();
                if (!ta.isEmpty())
                    p.align = ta;
                const QString wm = a.value(QStringLiteral("style:writing-mode")).toString();
                if (wm == QLatin1String("rl-tb"))
                    p.dir = QStringLiteral("rtl");
                else if (wm == QLatin1String("lr-tb"))
                    p.dir = QStringLiteral("ltr");
                out.insert(curName, p);
            }
        } else if (xr.isEndElement() && xr.name() == QLatin1String("style")) {
            curName.clear();
        }
    }
}

void applyParaStyleProps(const QMap<QString, ParaStyleProps> &paraStyles, const QString &odfStyleName,
                         QString &align, QString &dir)
{
    const ParaStyleProps p = paraStyles.value(odfStyleName);
    if (!p.align.isEmpty())
        align = p.align;
    if (!p.dir.isEmpty())
        dir = p.dir;
}

// LibreOffice formulas require the ooow prefix to be bound
// (xmlns:ooow="http://openoffice.org/2004/writer"). Equality in that formula
// language is "="; AND/OR/NOT are words. Our evaluator uses ==, &&, ||, !.
QString exprToOoow(const QString &expr)
{
    QString out;
    for (int i = 0; i < expr.size();) {
        const QChar c = expr.at(i);
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const QChar q = c;
            ++i;
            QString lit;
            while (i < expr.size() && expr.at(i) != q) {
                if (expr.at(i) == QLatin1Char('\\') && i + 1 < expr.size()) {
                    lit += expr.at(i + 1);
                    i += 2;
                    continue;
                }
                lit += expr.at(i);
                ++i;
            }
            if (i < expr.size())
                ++i;
            lit.replace(QLatin1Char('"'), QLatin1String("\\\""));
            out += QLatin1Char('"');
            out += lit;
            out += QLatin1Char('"');
            continue;
        }
        auto spacedWord = [&](const QString &word) {
            if (!out.isEmpty() && !out.back().isSpace())
                out += QLatin1Char(' ');
            out += word;
            if (i + 2 < expr.size() && !expr.at(i + 2).isSpace())
                out += QLatin1Char(' ');
        };
        if (expr.mid(i, 2) == QLatin1String("&&")) {
            spacedWord(QStringLiteral("AND"));
            i += 2;
            continue;
        }
        if (expr.mid(i, 2) == QLatin1String("||")) {
            spacedWord(QStringLiteral("OR"));
            i += 2;
            continue;
        }
        if (expr.mid(i, 2) == QLatin1String("!=")) {
            out += QLatin1String("<>");
            i += 2;
            continue;
        }
        if (expr.mid(i, 2) == QLatin1String("==")) {
            out += QLatin1Char('=');
            i += 2;
            continue;
        }
        if (c == QLatin1Char('!')) {
            out += QLatin1String("NOT");
            if (i + 1 < expr.size() && !expr.at(i + 1).isSpace())
                out += QLatin1Char(' ');
            ++i;
            continue;
        }
        out += c;
        ++i;
    }
    return out.trimmed();
}

QString ooowToExpr(QString cond)
{
    if (cond.startsWith(QLatin1String("ooow:"), Qt::CaseInsensitive))
        cond = cond.mid(5);
    QString out;
    for (int i = 0; i < cond.size();) {
        const QChar c = cond.at(i);
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const QChar q = c;
            ++i;
            QString lit;
            while (i < cond.size() && cond.at(i) != q) {
                if (cond.at(i) == QLatin1Char('\\') && i + 1 < cond.size()) {
                    lit += cond.at(i + 1);
                    i += 2;
                    continue;
                }
                lit += cond.at(i);
                ++i;
            }
            if (i < cond.size())
                ++i;
            lit.replace(QLatin1Char('"'), QLatin1String("\\\""));
            out += QLatin1Char('"');
            out += lit;
            out += QLatin1Char('"');
            continue;
        }
        if (c.isLetter() || c == QLatin1Char('_')) {
            int j = i + 1;
            while (j < cond.size()
                   && (cond.at(j).isLetterOrNumber() || cond.at(j) == QLatin1Char('_')
                       || cond.at(j) == QLatin1Char('.')))
                ++j;
            const QString word = cond.mid(i, j - i);
            const QString u = word.toUpper();
            if (u == QLatin1String("AND"))
                out += QLatin1String("&&");
            else if (u == QLatin1String("OR"))
                out += QLatin1String("||");
            else if (u == QLatin1String("NOT"))
                out += QLatin1Char('!');
            else
                out += word;
            i = j;
            continue;
        }
        if (cond.mid(i, 2) == QLatin1String("<>")) {
            out += QLatin1String("!=");
            i += 2;
            continue;
        }
        if (cond.mid(i, 2) == QLatin1String("<=") || cond.mid(i, 2) == QLatin1String(">=")
            || cond.mid(i, 2) == QLatin1String("==")) {
            out += cond.mid(i, 2);
            i += 2;
            continue;
        }
        if (c == QLatin1Char('=')) {
            out += QLatin1String("==");
            ++i;
            continue;
        }
        out += c;
        ++i;
    }
    return out.trimmed();
}

QString ooowCondition(const QString &expr)
{
    return QStringLiteral("ooow:") + exprToOoow(expr);
}

QString compactJson(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString seqNumFormat(const QString &format)
{
    if (format == QLatin1String("roman"))
        return QStringLiteral("i");
    if (format == QLatin1String("alpha"))
        return QStringLiteral("a");
    return QStringLiteral("1");
}

QString seqFormatFromOdf(const QString &numFormat)
{
    const QString f = numFormat.toLower();
    if (f == QLatin1String("i") || f == QLatin1String("roman"))
        return QStringLiteral("roman");
    if (f == QLatin1String("a") || f == QLatin1String("alpha"))
        return QStringLiteral("alpha");
    return QStringLiteral("arabic");
}

QString hiddenParagraphXml(const Document &doc, const Block &b)
{
    if (b.hideIf.isEmpty())
        return {};
    const bool hidden = blockHiddenByExpr(doc, b, 1, 1);
    QString xml = QStringLiteral("<text:hidden-paragraph text:condition=\"%1\"")
                      .arg(xmlEscape(ooowCondition(b.hideIf)));
    if (hidden)
        xml += QLatin1String(" text:is-hidden=\"true\"");
    xml += QLatin1String("/>");
    return xml;
}

void considerSeqName(const TextRun &r, QStringList &names)
{
    if (r.field != QLatin1String("seq"))
        return;
    const QString name =
        QJsonDocument::fromJson(r.fieldArg.toUtf8()).object().value(QStringLiteral("name")).toString();
    if (!name.isEmpty() && !names.contains(name))
        names.push_back(name);
}

QString sequenceDeclsXml(const Document &doc)
{
    QStringList names;
    for (const auto &b : doc.body) {
        for (const auto &r : b.runs)
            considerSeqName(r, names);
        for (const auto &r : b.captionRuns)
            considerSeqName(r, names);
        for (const auto &row : b.table) {
            for (const auto &cell : row) {
                for (const auto &r : cell.runs)
                    considerSeqName(r, names);
            }
        }
    }
    if (names.isEmpty())
        return {};
    QString xml = QLatin1String("<text:sequence-decls>");
    for (const auto &name : names) {
        xml += QStringLiteral(
                   "<text:sequence-decl text:display-outline-level=\"0\" text:name=\"%1\"/>")
                   .arg(xmlEscape(name));
    }
    xml += QLatin1String("</text:sequence-decls>");
    return xml;
}

QString userFieldDeclsXml(const Document &doc)
{
    if (doc.vars.isEmpty())
        return {};
    QString xml = QLatin1String("<text:user-field-decls>");
    for (auto it = doc.vars.cbegin(); it != doc.vars.cend(); ++it) {
        if (it.key().isEmpty())
            continue;
        xml += QStringLiteral(
                   "<text:user-field-decl office:value-type=\"string\" office:string-value=\"%1\" text:name=\"%2\"/>")
                   .arg(xmlEscape(it.value()), xmlEscape(it.key()));
    }
    xml += QLatin1String("</text:user-field-decls>");
    return xml;
}

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
        else if (r.field == QLatin1String("filepath") || r.field == QLatin1String("file_path")
                 || r.field == QLatin1String("path"))
            inner = QStringLiteral("<text:file-name text:display=\"full\">%1</text:file-name>").arg(xmlEscape(txt));
        else if (r.field == QLatin1String("word_count"))
            inner = QStringLiteral("<text:word-count>%1</text:word-count>").arg(xmlEscape(txt));
        else if (r.field == QLatin1String("if")) {
            const QJsonObject jo = QJsonDocument::fromJson(r.fieldArg.toUtf8()).object();
            inner = QStringLiteral(
                        "<text:conditional-text text:condition=\"%1\" text:string-value-if-true=\"%2\" "
                        "text:string-value-if-false=\"%3\">%4</text:conditional-text>")
                        .arg(xmlEscape(ooowCondition(jo.value(QStringLiteral("cond")).toString())),
                             xmlEscape(jo.value(QStringLiteral("then")).toString()),
                             xmlEscape(jo.value(QStringLiteral("else")).toString()),
                             xmlEscape(txt));
        } else if (r.field == QLatin1String("seq")) {
            const QJsonObject jo = QJsonDocument::fromJson(r.fieldArg.toUtf8()).object();
            const QString name = jo.value(QStringLiteral("name")).toString();
            const QString format = jo.value(QStringLiteral("format")).toString(QStringLiteral("arabic"));
            inner = QStringLiteral(
                        "<text:sequence text:name=\"%1\" text:formula=\"ooow:%2+1\" style:num-format=\"%3\">%4</text:sequence>")
                        .arg(xmlEscape(name), xmlEscape(name), seqNumFormat(format), xmlEscape(txt));
        } else if (r.field == QLatin1String("stat")) {
            const QString arg = r.fieldArg;
            QString el;
            if (arg == QLatin1String("pages"))
                el = QStringLiteral("page-count");
            else if (arg == QLatin1String("words"))
                el = QStringLiteral("word-count");
            else if (arg == QLatin1String("chars"))
                el = QStringLiteral("character-count");
            else if (arg == QLatin1String("paragraphs"))
                el = QStringLiteral("paragraph-count");
            else if (arg == QLatin1String("tables"))
                el = QStringLiteral("table-count");
            else if (arg == QLatin1String("images"))
                el = QStringLiteral("image-count");
            if (!el.isEmpty())
                inner = QStringLiteral("<text:%1>%2</text:%1>").arg(el, xmlEscape(txt));
        } else if (r.field == QLatin1String("doc_info")) {
            const QString arg = r.fieldArg;
            QString el;
            if (arg == QLatin1String("subject"))
                el = QStringLiteral("subject");
            else if (arg == QLatin1String("keywords"))
                el = QStringLiteral("keywords");
            else if (arg == QLatin1String("created"))
                el = QStringLiteral("creation-date");
            else if (arg == QLatin1String("modified"))
                el = QStringLiteral("modification-date");
            else if (arg == QLatin1String("revision"))
                el = QStringLiteral("editing-cycles");
            if (!el.isEmpty())
                inner = QStringLiteral("<text:%1>%2</text:%1>").arg(el, xmlEscape(txt));
        } else if (r.field == QLatin1String("sender")) {
            const QString arg = r.fieldArg;
            QString el;
            if (arg == QLatin1String("name"))
                el = QStringLiteral("sender-firstname");
            else if (arg == QLatin1String("email"))
                el = QStringLiteral("sender-email");
            else if (arg == QLatin1String("company"))
                el = QStringLiteral("sender-company");
            else if (arg == QLatin1String("phone"))
                el = QStringLiteral("sender-phone-work");
            if (!el.isEmpty())
                inner = QStringLiteral("<text:%1>%2</text:%1>").arg(el, xmlEscape(txt));
        } else if (r.field == QLatin1String("input_list")) {
            const QJsonObject jo = QJsonDocument::fromJson(r.fieldArg.toUtf8()).object();
            const QString selected = jo.value(QStringLiteral("selected")).toString();
            const QJsonArray opts = jo.value(QStringLiteral("options")).toArray();
            ++sc.dropN;
            QString labels;
            bool marked = false;
            for (const auto &v : opts) {
                const QString opt = v.toString();
                const bool cur = !marked && ((!selected.isEmpty() && opt == selected) || selected.isEmpty());
                if (cur)
                    marked = true;
                labels += QStringLiteral("<text:label text:value=\"%1\"%2/>")
                              .arg(xmlEscape(opt),
                                   cur ? QStringLiteral(" text:current-selected=\"true\"") : QString());
            }
            const QString shown = !selected.isEmpty() ? selected : txt;
            inner = QStringLiteral("<text:drop-down text:name=\"List%1\">%2%3</text:drop-down>")
                        .arg(sc.dropN)
                        .arg(labels, xmlEscape(shown));
        } else {
            QString fieldName = r.field;
            if (!r.fieldArg.isEmpty())
                fieldName += QLatin1Char(':') + r.fieldArg;
            inner = QStringLiteral("<text:user-field-get text:name=\"%1\">%2</text:user-field-get>")
                        .arg(xmlEscape(fieldName), xmlEscape(txt.isEmpty() ? r.fieldArg : txt));
        }
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

// ODF tracked changes for one block. Insert and format use text:change-start/end on [start, end).
// Delete is a zero-width text:change at `start`: the removed text is not in `runs` (the edit model
// drops it immediately) and is written from deletedRuns into the changed-region text:deletion.
// text:format-change is only a mark — ODF carries no previous character attributes.
struct RevMark {
    int pos = 0;
    int ord = 0; // 0 end, 1 delete point, 2 start, 3 bookmark — ends precede a point or a new range at the same offset
    int seq = 0;
    const Revision *rv = nullptr;
    bool point = false;
    bool opening = false;
    bool bookmark = false;
    QString bookmarkName;
};

QString officeChangeInfo(const Revision *rv)
{
    const QString dateStr = QDateTime::fromMSecsSinceEpoch(rv->timestamp).toString(Qt::ISODate);
    return QStringLiteral("<office:change-info><dc:creator>%1</dc:creator><dc:date>%2</dc:date></office:change-info>")
        .arg(xmlEscape(rv->author), dateStr);
}

QString revisedSpansXml(SpanCounter &sc, const QString &blockId, const QVector<TextRun> &runs,
                         const Document &doc, QVector<const Revision *> &usedRevisions)
{
    int total = 0;
    for (const auto &r : runs)
        total += r.text.size();
    QVector<RevMark> marks;
    int seq = 0;
    for (const auto &rv : doc.revisions) {
        if (rv.blockId != blockId)
            continue;
        if (rv.kind == QLatin1String("insert") || rv.kind == QLatin1String("format")) {
            const int s = qBound(0, rv.start, total);
            const int e = qBound(s, rv.end, total);
            marks.push_back(RevMark{s, 2, seq++, &rv, false, true});
            marks.push_back(RevMark{e, 0, seq++, &rv, false, false});
        } else if (rv.kind == QLatin1String("delete")) {
            const int s = qBound(0, rv.start, total);
            marks.push_back(RevMark{s, 1, seq++, &rv, true, false});
        }
    }
    for (const auto &bm : doc.bookmarks) {
        if (bm.blockId != blockId)
            continue;
        const QString name = bm.name.isEmpty() ? bm.id : bm.name;
        if (name.isEmpty())
            continue;
        marks.push_back(RevMark{qBound(0, bm.offset, total), 3, seq++, nullptr, false, false, true, name});
    }
    std::sort(marks.begin(), marks.end(), [](const RevMark &a, const RevMark &b) {
        if (a.pos != b.pos)
            return a.pos < b.pos;
        if (a.ord != b.ord)
            return a.ord < b.ord;
        return a.seq < b.seq;
    });
    QString out;
    int cursor = 0;
    for (const RevMark &m : marks) {
        if (m.pos > cursor) {
            out += spansXml(sc, splitRuns(runs, cursor, m.pos), doc);
            cursor = m.pos;
        }
        if (m.bookmark) {
            out += QStringLiteral("<text:bookmark text:name=\"%1\"/>").arg(xmlEscape(m.bookmarkName));
            continue;
        }
        const QString id = xmlEscape(m.rv->id);
        if (m.point) {
            usedRevisions.push_back(m.rv);
            out += QStringLiteral("<text:change text:change-id=\"%1\"/>").arg(id);
        } else if (m.opening) {
            usedRevisions.push_back(m.rv);
            out += QStringLiteral("<text:change-start text:change-id=\"%1\"/>").arg(id);
        } else {
            out += QStringLiteral("<text:change-end text:change-id=\"%1\"/>").arg(id);
        }
    }
    if (cursor < total)
        out += spansXml(sc, splitRuns(runs, cursor, total), doc);
    return out;
}

QString changedRegionXml(SpanCounter &sc, const Revision *rv, const Document &doc)
{
    const QString info = officeChangeInfo(rv);
    const QString id = xmlEscape(rv->id);
    if (rv->kind == QLatin1String("delete")) {
        QVector<TextRun> gone = rv->deletedRuns;
        if (gone.isEmpty() && !rv->text.isEmpty()) {
            TextRun fallback;
            fallback.text = rv->text;
            gone.push_back(fallback);
        }
        const QString body = spansXml(sc, gone, doc);
        return QStringLiteral(
                   "<text:changed-region text:id=\"%1\"><text:deletion>%2"
                   "<text:p text:style-name=\"Standard\">%3</text:p></text:deletion></text:changed-region>")
            .arg(id, info, body);
    }
    if (rv->kind == QLatin1String("format")) {
        return QStringLiteral(
                   "<text:changed-region text:id=\"%1\"><text:format-change>%2</text:format-change></text:changed-region>")
            .arg(id, info);
    }
    return QStringLiteral(
               "<text:changed-region text:id=\"%1\"><text:insertion>%2</text:insertion></text:changed-region>")
        .arg(id, info);
}

QString imageExt(const Block &b)
{
    if (b.imageMime.contains(QLatin1String("jpeg")) || b.imagePath.endsWith(QLatin1String(".jpg"), Qt::CaseInsensitive)
        || b.imagePath.endsWith(QLatin1String(".jpeg"), Qt::CaseInsensitive))
        return QStringLiteral("jpg");
    return QStringLiteral("png");
}

bool parseOdfLengthMm(QString raw, double *out, bool requirePositive)
{
    raw = raw.trimmed();
    if (raw.isEmpty() || !out)
        return false;
    double factor = 1.0;
    if (raw.endsWith(QLatin1String("cm"), Qt::CaseInsensitive)) {
        factor = 10.0;
        raw.chop(2);
    } else if (raw.endsWith(QLatin1String("mm"), Qt::CaseInsensitive)) {
        factor = 1.0;
        raw.chop(2);
    } else if (raw.endsWith(QLatin1String("in"), Qt::CaseInsensitive)) {
        factor = 25.4;
        raw.chop(2);
    } else if (raw.endsWith(QLatin1String("pt"), Qt::CaseInsensitive)) {
        factor = 25.4 / 72.0;
        raw.chop(2);
    } else if (raw.endsWith(QLatin1String("pc"), Qt::CaseInsensitive)) {
        factor = 25.4 / 6.0;
        raw.chop(2);
    }
    bool ok = false;
    const double v = raw.trimmed().toDouble(&ok);
    if (!ok)
        return false;
    if (requirePositive && !(v > 0.0))
        return false;
    if (!requirePositive && v < 0.0)
        return false;
    *out = std::round((v * factor) * 100.0) / 100.0;
    return true;
}

void applyNamedPage(Document &doc, double ww, double hh)
{
    struct Spec {
        const char *name;
        double w;
        double h;
    };
    const Spec specs[] = {
        {"A5", 148.0, 210.0},
        {"A4", 210.0, 297.0},
        {"A3", 297.0, 420.0},
        {"Letter", 215.9, 279.4},
        {"Legal", 215.9, 355.6},
    };
    const double pw = qMin(ww, hh);
    const double ph = qMax(ww, hh);
    QString name = QStringLiteral("custom");
    double cw = ww;
    double ch = hh;
    for (const auto &s : specs) {
        if (qAbs(pw - s.w) <= 1.0 && qAbs(ph - s.h) <= 1.0) {
            name = QString::fromLatin1(s.name);
            cw = s.w;
            ch = s.h;
            if (ww > hh)
                std::swap(cw, ch);
            break;
        }
    }
    doc.page.size = name;
    doc.page.widthMm = cw;
    doc.page.heightMm = ch;
    doc.page.orientation = (ww > hh) ? QStringLiteral("landscape") : QStringLiteral("portrait");
}

void readOdfPageLayout(Document &doc, const QByteArray &bytes, bool onlyIfMissing)
{
    if (bytes.isEmpty())
        return;
    if (onlyIfMissing && doc.page.widthMm > 0)
        return;
    QXmlStreamReader st(bytes);
    while (!st.atEnd()) {
        st.readNext();
        if (!st.isStartElement())
            continue;
        if (!onlyIfMissing && st.name() == QLatin1String("columns")) {
            const int n = st.attributes().value(QStringLiteral("fo:column-count")).toInt();
            if (n > 0)
                doc.page.columns = qBound(1, n, 3);
            continue;
        }
        if (st.name() != QLatin1String("page-layout-properties"))
            continue;
        if (onlyIfMissing && doc.page.widthMm > 0)
            continue;
        const auto a = st.attributes();
        double ww = 0;
        double hh = 0;
        const bool gotW = parseOdfLengthMm(a.value(QStringLiteral("fo:page-width")).toString(), &ww, true);
        const bool gotH = parseOdfLengthMm(a.value(QStringLiteral("fo:page-height")).toString(), &hh, true);
        if (gotW && gotH)
            applyNamedPage(doc, ww, hh);
        auto margin = [&](const QString &key, int index) {
            double m = 0;
            if (parseOdfLengthMm(a.value(key).toString(), &m, false))
                doc.page.marginsMm[index] = m;
        };
        margin(QStringLiteral("fo:margin-top"), 0);
        margin(QStringLiteral("fo:margin-right"), 1);
        margin(QStringLiteral("fo:margin-bottom"), 2);
        margin(QStringLiteral("fo:margin-left"), 3);
    }
}

QString listKindFromNumFormat(const QString &fmt)
{
    if (fmt == QLatin1String("a"))
        return QStringLiteral("lowerLetter");
    if (fmt == QLatin1String("A"))
        return QStringLiteral("upperLetter");
    if (fmt == QLatin1String("i"))
        return QStringLiteral("lowerRoman");
    if (fmt == QLatin1String("I"))
        return QStringLiteral("upperRoman");
    return QStringLiteral("decimal");
}

void collectListStyles(const QByteArray &bytes, QMap<QString, QMap<int, QString>> &out)
{
    if (bytes.isEmpty())
        return;
    QXmlStreamReader xr(bytes);
    QString cur;
    while (!xr.atEnd()) {
        xr.readNext();
        if (xr.isEndElement() && xr.name() == QLatin1String("list-style")) {
            cur.clear();
            continue;
        }
        if (!xr.isStartElement())
            continue;
        const auto n = xr.name();
        if (n == QLatin1String("list-style")) {
            cur = xr.attributes().value(QStringLiteral("style:name")).toString();
        } else if (!cur.isEmpty()
                   && (n == QLatin1String("list-level-style-bullet")
                       || n == QLatin1String("list-level-style-number"))) {
            int level = xr.attributes().value(QStringLiteral("text:level")).toInt();
            if (level <= 0)
                level = 1;
            const QString kind = n == QLatin1String("list-level-style-bullet")
                ? QStringLiteral("bullet")
                : listKindFromNumFormat(xr.attributes().value(QStringLiteral("style:num-format")).toString());
            out[cur].insert(level, kind);
        }
    }
}

QString listKindFor(const QMap<QString, QMap<int, QString>> &styles, const QString &name, int level)
{
    if (name.isEmpty() || !styles.contains(name))
        return QStringLiteral("decimal");
    const QMap<int, QString> levels = styles.value(name);
    if (levels.contains(level))
        return levels.value(level);
    int best = -1;
    QString kind = QStringLiteral("decimal");
    for (auto it = levels.cbegin(); it != levels.cend(); ++it) {
        if (it.key() <= level && it.key() > best) {
            best = it.key();
            kind = it.value();
        }
    }
    if (best < 0 && !levels.isEmpty())
        return levels.first();
    return kind;
}

QString listStyleXml(const QString &name, const QString &kind)
{
    const bool bullet = kind == QLatin1String("bullet");
    QString fmt = QStringLiteral("1");
    if (kind == QLatin1String("lowerLetter"))
        fmt = QStringLiteral("a");
    else if (kind == QLatin1String("upperLetter"))
        fmt = QStringLiteral("A");
    else if (kind == QLatin1String("lowerRoman"))
        fmt = QStringLiteral("i");
    else if (kind == QLatin1String("upperRoman"))
        fmt = QStringLiteral("I");
    QString levels;
    for (int lv = 1; lv <= 9; ++lv) {
        if (bullet) {
            levels += QStringLiteral(
                          "<text:list-level-style-bullet text:level=\"%1\" text:bullet-char=\"%2\"/>")
                          .arg(lv)
                          .arg(QChar(0x2022));
        } else {
            levels += QStringLiteral(
                          "<text:list-level-style-number text:level=\"%1\" style:num-format=\"%2\"/>")
                          .arg(lv)
                          .arg(xmlEscape(fmt));
        }
    }
    return QStringLiteral("<text:list-style style:name=\"%1\">%2</text:list-style>")
        .arg(xmlEscape(name), levels);
}

bool cellHasText(const QVector<TextRun> &runs)
{
    for (const auto &r : runs) {
        if (!r.text.isEmpty() || !r.field.isEmpty() || !r.noteId.isEmpty())
            return true;
    }
    return false;
}

constexpr int kMaxOdtTableColumns = 256;

QString odtTableTooWide()
{
    return QStringLiteral("ODT table row is wider than 256 columns");
}

bool appendNestedTable(QString &bodyXml, SpanCounter &sc, const QVector<QVector<NestedCell>> &nested,
                       const Document &doc, QString *error)
{
    if (nested.size() > kMaxOdtTableColumns) {
        if (error)
            *error = odtTableTooWide();
        return false;
    }
    int cols = 0;
    for (const auto &row : nested) {
        if (row.size() > kMaxOdtTableColumns) {
            if (error)
                *error = odtTableTooWide();
            return false;
        }
        cols = qMax(cols, row.size());
    }
    bodyXml += QLatin1String("<table:table>");
    for (int c = 0; c < cols; ++c)
        bodyXml += QLatin1String("<table:table-column/>");
    for (const auto &row : nested) {
        bodyXml += QLatin1String("<table:table-row>");
        for (const auto &cell : row) {
            bodyXml += QLatin1String("<table:table-cell><text:p text:style-name=\"Standard\">");
            bodyXml += spansXml(sc, cell.runs, doc);
            bodyXml += QLatin1String("</text:p></table:table-cell>");
        }
        for (int c = row.size(); c < cols; ++c)
            bodyXml += QLatin1String("<table:covered-table-cell/>");
        bodyXml += QLatin1String("</table:table-row>");
    }
    bodyXml += QLatin1String("</table:table>");
    return true;
}

bool appendOuterTable(QString &bodyXml, SpanCounter &sc, const QVector<QVector<TableCell>> &table,
                      const Document &doc, QString *error)
{
    const int nRows = table.size();
    if (nRows > kMaxOdtTableColumns) {
        if (error)
            *error = odtTableTooWide();
        return false;
    }
    // `Block::table` reaches this exporter in two different shapes depending on where
    // it came from (see tableWml in Docx.cpp for the full story): `loadDocx` never
    // stores a cell for a position covered by an earlier colSpan or an earlier row's
    // rowSpan (sparse: stored-cell count can be *less* than the grid width), while
    // `insert_table` / `merge_cells` / `loadOdt` always keep one TableCell per grid
    // column, covered or not (dense: stored-cell count always *equals* the grid
    // width). A row with more than one stored cell where one of them has colSpan>1 is
    // ambiguous between the two shapes, so it never votes on the column count; a
    // "reliable" row — either no colSpan>1 anywhere, or exactly one stored cell —
    // always votes with the sum of its own colSpans. With no reliable row at all,
    // fall back to the widest row's raw stored size.
    int colsRef = 0;
    int colsAnyRow = 0;
    for (const auto &row : table) {
        if (row.size() > kMaxOdtTableColumns) {
            if (error)
                *error = odtTableTooWide();
            return false;
        }
        bool anySpan = false;
        qint64 rowCols = 0;
        for (const auto &cell : row) {
            const int span = qMax(1, cell.colSpan);
            const int rspan = qMax(1, cell.rowSpan);
            if (span > kMaxOdtTableColumns || rspan > kMaxOdtTableColumns
                || rowCols > qint64(kMaxOdtTableColumns) - span) {
                if (error)
                    *error = odtTableTooWide();
                return false;
            }
            if (cell.colSpan > 1)
                anySpan = true;
            rowCols += span;
        }
        const bool reliable = !anySpan || row.size() == 1;
        if (reliable)
            colsRef = qMax(colsRef, int(rowCols));
        colsAnyRow = qMax(colsAnyRow, row.size());
    }
    const int cols = colsRef > 0 ? colsRef : colsAnyRow;
    if (cols > kMaxOdtTableColumns) {
        if (error)
            *error = odtTableTooWide();
        return false;
    }
    bodyXml += QLatin1String(
        "<table:table xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\">");
    for (int c = 0; c < cols; ++c)
        bodyXml += QLatin1String("<table:table-column/>");
    // `pending[gc]` carries a rowSpan anchored in an earlier row forward until it
    // stops covering this column, so a continuation row emits one covered cell per
    // covered column (with the anchor's own colSpan width) instead of confusing a
    // covered position for one of this row's own stored cells.
    struct Pending {
        int rowsLeft = 0;
        int span = 1;
    };
    QVector<Pending> pending(cols);
    for (int r = 0; r < nRows; ++r) {
        bodyXml += QLatin1String("<table:table-row>");
        // A row whose stored-cell count already equals the grid width is this row's
        // "dense" shape: every covered position (horizontal or vertical) has its own
        // (unused, often stale) stored entry that must be skipped over by consuming a
        // stored slot, not just advancing the grid column. A shorter row is "sparse":
        // only real cells are stored, so a covered position consumes no stored slot.
        const bool dense = table[r].size() >= cols;
        int gc = 0;
        int si = 0;
        while (gc < cols) {
            if (pending[gc].rowsLeft > 0) {
                const int span = qMax(1, qMin(pending[gc].span, cols - gc));
                for (int k = 0; k < span; ++k)
                    bodyXml += QLatin1String("<table:covered-table-cell/>");
                for (int cc = gc; cc < gc + span; ++cc)
                    pending[cc].rowsLeft -= 1;
                gc += span;
                if (dense)
                    si += span;
                continue;
            }
            if (si >= table[r].size()) {
                // Defensive: a well-formed row never runs out of stored cells before
                // the grid width, but never walk off the end of the stored array.
                bodyXml += QLatin1String("<table:covered-table-cell/>");
                ++gc;
                continue;
            }
            const TableCell &cell = table[r][si];
            const int cs = qBound(1, cell.colSpan, qMax(1, cols - gc));
            const int rs = qBound(1, cell.rowSpan, qMax(1, nRows - r));
            const bool merged = cs > 1 || rs > 1;
            QString attrs;
            // D4/A-odf-6: a merged cell's footprint is written as a pair (both
            // dimensions), the way LibreOffice itself writes it, even when one
            // dimension is 1 — so the attribute that spells out "not spanned on
            // this axis" round-trips instead of being silently dropped.
            if (merged) {
                attrs += QStringLiteral(" table:number-columns-spanned=\"%1\"").arg(cs);
                attrs += QStringLiteral(" table:number-rows-spanned=\"%1\"").arg(rs);
            }
            bodyXml += QStringLiteral("<table:table-cell%1>").arg(attrs);
            if (cellHasText(cell.runs) || cell.nested.isEmpty()) {
                bodyXml += QLatin1String("<text:p text:style-name=\"Standard\">");
                bodyXml += spansXml(sc, cell.runs, doc);
                bodyXml += QLatin1String("</text:p>");
            }
            if (!cell.nested.isEmpty() && !appendNestedTable(bodyXml, sc, cell.nested, doc, error))
                return false;
            bodyXml += QLatin1String("</table:table-cell>");
            for (int k = 1; k < cs; ++k)
                bodyXml += QLatin1String("<table:covered-table-cell/>");
            if (rs > 1) {
                for (int cc = gc; cc < gc + cs; ++cc) {
                    pending[cc].rowsLeft = rs - 1;
                    pending[cc].span = cs;
                }
            }
            gc += cs;
            // Dense: the anchor's own stored slot plus one stored placeholder per
            // extra covered column it spans (merge_cells never removes those). Sparse:
            // the anchor is the one and only stored slot for its whole span.
            si += dense ? cs : 1;
        }
        bodyXml += QLatin1String("</table:table-row>");
    }
    bodyXml += QLatin1String("</table:table>");
    return true;
}

bool isPaintedFigure(const Block &b)
{
    return b.type == QLatin1String("shape") || b.type == QLatin1String("chart") || b.type == QLatin1String("qr")
        || b.type == QLatin1String("form");
}

} // namespace

bool saveOdt(const Document &doc, const QString &path, QString *error)
{
    double wMm = 0, hMm = 0;
    doc.page.pageSizeMm(wMm, hMm);
    QString orient = doc.page.orientation;
    if (orient != QLatin1String("portrait") && orient != QLatin1String("landscape"))
        orient = QStringLiteral("portrait");
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
                                   .arg(xmlEscape(orient))
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
        if (!s.align.isEmpty() && isValidBlockAlign(s.align))
            pp += QStringLiteral("fo:text-align=\"%1\" ").arg(xmlEscape(s.align));
        pp += QStringLiteral("fo:margin-top=\"%1pt\" fo:margin-bottom=\"%2pt\" ")
                  .arg(s.spaceBeforePt)
                  .arg(s.spaceAfterPt);
        paraStyles += QStringLiteral(
                          "<style:style style:name=\"%1\" style:family=\"paragraph\" style:parent-style-name=\"Standard\">"
                          "<style:paragraph-properties %2/><style:text-properties style:font-name=\"%3\" %4/>"
                          "</style:style>")
                          .arg(xmlEscape(odfStyleName(s.name)), pp, xmlEscape(s.font), tp);
    }
    paraStyles += QLatin1String(
        "<style:style style:name=\"PBreak\" style:family=\"paragraph\">"
        "<style:paragraph-properties fo:break-before=\"page\"/></style:style>");
    paraStyles += QLatin1String(
        "<style:style style:name=\"PColBreak\" style:family=\"paragraph\">"
        "<style:paragraph-properties fo:break-before=\"column\"/></style:style>");

    SpanCounter sc;
    ParaStyleCounter psc;
    QString bodyXml;
    QString listStylesXml;
    QJsonArray figureBlocks;
    QVector<ZipEntry> pictures;
    QVector<const Revision *> usedRevisions;
    int imgN = 0;
    int listStyleN = 0;
    int i = 0;
    const auto &body = doc.body;
    while (i < body.size()) {
        const Block &b = body[i];
        if (b.type == QLatin1String("footnote") || b.type == QLatin1String("endnote")) {
            ++i;
            continue;
        }
        if (b.type == QLatin1String("list-item")) {
            const QString kind = b.listKind;
            const QString styleName = QStringLiteral("AioL%1").arg(++listStyleN);
            listStylesXml += listStyleXml(styleName, kind);
            bodyXml += QStringLiteral("<text:list text:style-name=\"%1\">").arg(xmlEscape(styleName));
            while (i < body.size() && body[i].type == QLatin1String("list-item") && body[i].listKind == kind) {
                QString inner = QStringLiteral("<text:p text:style-name=\"%1\">%2%3</text:p>")
                                    .arg(xmlEscape(paraStyleForBlock(psc, body[i])),
                                         hiddenParagraphXml(doc, body[i]),
                                         revisedSpansXml(sc, body[i].id, body[i].runs, doc, usedRevisions));
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
            bodyXml += QStringLiteral("<text:h text:style-name=\"%1\" text:outline-level=\"%2\">%3%4</text:h>")
                           .arg(xmlEscape(paraStyleForBlock(psc, b)))
                           .arg(b.level > 0 ? b.level : 1)
                           .arg(hiddenParagraphXml(doc, b),
                                revisedSpansXml(sc, b.id, b.runs, doc, usedRevisions));
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
            if (!appendOuterTable(bodyXml, sc, b.table, doc, error))
                return false;
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
            if (bytes.isEmpty()) {
                // A DOCX import placeholder for a drawing with no resolvable picture (an
                // unsupported shape/textbox) carries no real image data. Referencing a
                // Pictures/* entry that is never written is a dangling xlink:href, so fall
                // back to a minimal valid 1x1 transparent PNG instead of an absent file.
                bytes = QByteArray::fromBase64(
                    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR4nGMAAQAABQABDQottAAAAABJRU5ErkJggg==");
            }
            pictures.push_back({href, bytes, false});
            bodyXml += QStringLiteral(
                           "<text:p text:style-name=\"Standard\"><draw:frame xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
                           "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" svg:width=\"%1mm\" svg:height=\"40mm\">"
                           "<draw:image xmlns:xlink=\"http://www.w3.org/1999/xlink\" xlink:href=\"%2\"/></draw:frame></text:p>")
                           .arg(b.imageWidthMm)
                           .arg(xmlEscape(href));
        } else if (isPaintedFigure(b)) {
            const QByteArray png = renderBlockPng(doc, b);
            if (png.isEmpty()) {
                if (error)
                    *error = QStringLiteral("ODT figure raster failed: ") + b.id;
                return false;
            }
            ++imgN;
            const QString href = QStringLiteral("Pictures/image%1.png").arg(imgN);
            pictures.push_back({href, png, false});
            const QString name = b.id.isEmpty() ? QStringLiteral("aio-fig-%1").arg(imgN) : b.id;
            double wmm = b.imageWidthMm > 0 ? b.imageWidthMm : (b.type == QLatin1String("qr") ? 40.0 : 80.0);
            double hmm = b.heightMm > 0 ? b.heightMm : (b.type == QLatin1String("qr") ? wmm : 40.0);
            if (b.type == QLatin1String("qr"))
                hmm = wmm;
            QString titleXml;
            if (!b.altText.isEmpty()) {
                const QString alt = xmlEscape(b.altText);
                titleXml = QStringLiteral("<svg:title>%1</svg:title><svg:desc>%1</svg:desc>").arg(alt);
            }
            bodyXml += QStringLiteral(
                           "<text:p text:style-name=\"Standard\"><draw:frame draw:name=\"%1\" "
                           "svg:width=\"%2mm\" svg:height=\"%3mm\">%4"
                           "<draw:image xlink:href=\"%5\"/></draw:frame></text:p>")
                           .arg(xmlEscape(name))
                           .arg(wmm)
                           .arg(hmm)
                           .arg(titleXml, xmlEscape(href));
            QJsonObject fig = b.toJson();
            if (!b.altText.isEmpty())
                fig[QStringLiteral("alt")] = b.altText;
            if (fig.value(QStringLiteral("id")).toString().isEmpty())
                fig[QStringLiteral("id")] = name;
            figureBlocks.append(fig);
        } else {
            bodyXml += QStringLiteral("<text:p text:style-name=\"%1\">%2%3</text:p>")
                           .arg(xmlEscape(paraStyleForBlock(psc, b)), hiddenParagraphXml(doc, b),
                                revisedSpansXml(sc, b.id, b.runs, doc, usedRevisions));
        }
        ++i;
    }
    QString trackedChangesXml;
    if (!usedRevisions.isEmpty()) {
        QSet<QString> seen;
        trackedChangesXml = QLatin1String("<text:tracked-changes>");
        for (const Revision *rv : usedRevisions) {
            if (seen.contains(rv->id))
                continue;
            seen.insert(rv->id);
            trackedChangesXml += changedRegionXml(sc, rv, doc);
        }
        trackedChangesXml += QLatin1String("</text:tracked-changes>");
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
        "xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
        "xmlns:ooow=\"http://openoffice.org/2004/writer\" office:version=\"1.3\">"
        "<office:automatic-styles>%1</office:automatic-styles>"
        "<office:body><office:text>%2%3%4</office:text></office:body></office:document-content>")
                                   .arg(psc.stylesXml + sc.stylesXml + listStylesXml,
                                        sequenceDeclsXml(doc) + userFieldDeclsXml(doc),
                                        trackedChangesXml, bodyXml);

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
    if (!figureBlocks.isEmpty())
        extra[QStringLiteral("figures")] = figureBlocks;
    const QString extraJson = QString::fromUtf8(QJsonDocument(extra).toJson(QJsonDocument::Compact));
    QString metaFields;
    if (!doc.subject.isEmpty())
        metaFields += QStringLiteral("<dc:subject>%1</dc:subject>").arg(xmlEscape(doc.subject));
    if (!doc.keywords.isEmpty())
        metaFields += QStringLiteral("<meta:keyword>%1</meta:keyword>").arg(xmlEscape(doc.keywords));
    const QString created = doc.vars.value(QStringLiteral("doc.created"));
    const QString modified = doc.vars.value(QStringLiteral("doc.modified"));
    if (!created.isEmpty())
        metaFields += QStringLiteral("<meta:creation-date>%1</meta:creation-date>").arg(xmlEscape(created));
    if (!modified.isEmpty())
        metaFields += QStringLiteral("<dc:date>%1</dc:date>").arg(xmlEscape(modified));
    metaFields += QStringLiteral("<meta:editing-cycles>%1</meta:editing-cycles>").arg(qMax(1, doc.revisions.size()));
    const QString meta = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<office:document-meta xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
        "xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" office:version=\"1.3\">"
        "<office:meta><dc:title>%1</dc:title>%3"
        "<meta:user-defined meta:name=\"aioffice:extra\">%2</meta:user-defined>"
        "</office:meta></office:document-meta>")
                             .arg(xmlEscape(doc.title), xmlEscape(extraJson), metaFields);

    QVector<ZipEntry> entries;
    entries.push_back({QStringLiteral("mimetype"),
                       QByteArray("application/vnd.oasis.opendocument.text"), true});
    entries.push_back({QStringLiteral("content.xml"), contentXml.toUtf8(), false});
    entries.push_back({QStringLiteral("styles.xml"), stylesXml.toUtf8(), false});
    entries.push_back({QStringLiteral("meta.xml"), meta.toUtf8(), false});
    entries.push_back({QStringLiteral("META-INF/manifest.xml"), manifest.toUtf8(), false});
    for (const auto &pic : pictures)
        entries.push_back(pic);

    return writeFileAtomic(path, Zip::pack(entries), error);
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
    QString font = a.value(QStringLiteral("style:font-name")).toString().trimmed();
    if (font.isEmpty())
        font = a.value(QStringLiteral("fo:font-family")).toString().trimmed();
    auto stripQuotes = [](QString s) {
        s = s.trimmed();
        if (s.size() >= 2) {
            const QChar q = s.front();
            if ((q == QLatin1Char('\'') || q == QLatin1Char('"')) && s.back() == q)
                s = s.mid(1, s.size() - 2).trimmed();
        }
        return s;
    };
    font = stripQuotes(font);
    const int comma = font.indexOf(QLatin1Char(','));
    if (comma >= 0)
        font = stripQuotes(font.left(comma));
    if (!font.isEmpty())
        r.font = font;
}

struct TrackedChange {
    QString kind;
    QString author;
    qint64 timestamp = 0;
    QString text;
    QVector<TextRun> deletedRuns;
};

void copyCharFormat(TextRun &dst, const TextRun &src)
{
    dst.bold = src.bold;
    dst.italic = src.italic;
    dst.underline = src.underline;
    dst.strike = src.strike;
    dst.superscript = src.superscript;
    dst.subscript = src.subscript;
    dst.color = src.color;
    dst.sizePt = src.sizePt;
    dst.highlight = src.highlight;
    dst.font = src.font;
}

// Regions live in text:tracked-changes (LibreOffice and our export put that before the body).
// Deletion content is collected here and never fed to the paragraph walker.
QMap<QString, TrackedChange> collectTrackedChanges(const QByteArray &content, const QMap<QString, TextRun> &autoText)
{
    QMap<QString, TrackedChange> out;
    QXmlStreamReader xml(content);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QLatin1String("tracked-changes"))
            continue;
        QString curId;
        TrackedChange cur;
        bool inDeletion = false;
        bool inInfo = false;
        bool inPara = false;
        TextRun span;
        auto flushSpan = [&]() {
            if (span.text.isEmpty())
                return;
            cur.deletedRuns.push_back(span);
            span = TextRun{};
        };
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isEndElement() && xml.name() == QLatin1String("tracked-changes"))
                break;
            if (xml.isStartElement()) {
                const auto nn = xml.name();
                if (nn == QLatin1String("changed-region")) {
                    curId = xml.attributes().value(QStringLiteral("text:id")).toString();
                    if (curId.isEmpty())
                        curId = xml.attributes().value(QStringLiteral("xml:id")).toString();
                    cur = TrackedChange{};
                } else if (nn == QLatin1String("insertion")) {
                    cur.kind = QStringLiteral("insert");
                } else if (nn == QLatin1String("deletion")) {
                    cur.kind = QStringLiteral("delete");
                    inDeletion = true;
                } else if (nn == QLatin1String("format-change")) {
                    cur.kind = QStringLiteral("format");
                } else if (nn == QLatin1String("change-info")) {
                    inInfo = true;
                } else if (nn == QLatin1String("creator")) {
                    cur.author = xml.readElementText();
                } else if (nn == QLatin1String("date")) {
                    const QString ds = xml.readElementText();
                    QDateTime dt = QDateTime::fromString(ds, Qt::ISODateWithMs);
                    if (!dt.isValid())
                        dt = QDateTime::fromString(ds, Qt::ISODate);
                    if (dt.isValid())
                        cur.timestamp = dt.toMSecsSinceEpoch();
                } else if (inDeletion && !inInfo
                           && (nn == QLatin1String("p") || nn == QLatin1String("h"))) {
                    flushSpan();
                    if (!cur.deletedRuns.isEmpty()) {
                        TextRun nl;
                        nl.text = QStringLiteral("\n");
                        cur.deletedRuns.push_back(nl);
                    }
                    inPara = true;
                } else if (inDeletion && inPara && !inInfo && nn == QLatin1String("span")) {
                    flushSpan();
                    const QString sn = xml.attributes().value(QStringLiteral("text:style-name")).toString();
                    if (autoText.contains(sn))
                        copyCharFormat(span, autoText.value(sn));
                }
            } else if (xml.isCharacters() && inDeletion && inPara && !inInfo) {
                span.text += xml.text();
            } else if (xml.isEndElement()) {
                const auto nn = xml.name();
                if (nn == QLatin1String("span") && inDeletion) {
                    flushSpan();
                    span = TextRun{};
                } else if ((nn == QLatin1String("p") || nn == QLatin1String("h")) && inDeletion) {
                    flushSpan();
                    inPara = false;
                } else if (nn == QLatin1String("change-info"))
                    inInfo = false;
                else if (nn == QLatin1String("deletion")) {
                    flushSpan();
                    QString plain;
                    for (const auto &r : cur.deletedRuns)
                        plain += r.text;
                    cur.text = plain;
                    inDeletion = false;
                    inPara = false;
                } else if (nn == QLatin1String("changed-region") && !curId.isEmpty()) {
                    if (cur.kind.isEmpty())
                        cur.kind = QStringLiteral("insert");
                    out.insert(curId, cur);
                    curId.clear();
                }
            }
        }
        break;
    }
    return out;
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

    QMap<QString, ParaStyleProps> paraStyles;
    const QByteArray stylesBytes = Zip::file(entries, QStringLiteral("styles.xml"));
    collectParaStyles(stylesBytes, paraStyles);
    collectParaStyles(content, paraStyles);
    QMap<QString, QMap<int, QString>> listStyles;
    collectListStyles(stylesBytes, listStyles);
    collectListStyles(content, listStyles);
    readOdfPageLayout(doc, stylesBytes, false);
    readOdfPageLayout(doc, content, true);
    QHash<QString, Block> figureById;

    const QByteArray metaBytes = Zip::file(entries, QStringLiteral("meta.xml"));
    if (!metaBytes.isEmpty()) {
        QXmlStreamReader meta(metaBytes);
        while (!meta.atEnd()) {
            meta.readNext();
            if (meta.isStartElement() && meta.name().toString() == QLatin1String("title"))
                doc.title = meta.readElementText();
            else if (meta.isStartElement() && meta.name() == QLatin1String("subject"))
                doc.subject = meta.readElementText();
            else if (meta.isStartElement() && meta.name() == QLatin1String("keyword")) {
                const QString kw = meta.readElementText();
                if (!kw.isEmpty()) {
                    if (!doc.keywords.isEmpty())
                        doc.keywords += QLatin1String(", ");
                    doc.keywords += kw;
                }
            } else if (meta.isStartElement() && meta.name() == QLatin1String("creation-date"))
                doc.vars.insert(QStringLiteral("doc.created"), meta.readElementText());
            else if (meta.isStartElement() && meta.name() == QLatin1String("date"))
                doc.vars.insert(QStringLiteral("doc.modified"), meta.readElementText());
            else if (meta.isStartElement() && meta.name() == QLatin1String("editing-cycles"))
                doc.vars.insert(QStringLiteral("doc.editing-cycles"), meta.readElementText());
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
                for (const auto &v : extra.value(QStringLiteral("figures")).toArray()) {
                    Block fb = Block::fromJson(v.toObject());
                    if (!fb.id.isEmpty())
                        figureById.insert(fb.id, fb);
                }
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

    const QMap<QString, TrackedChange> tracked = collectTrackedChanges(content, autoText);

    QXmlStreamReader xml(content);
    QVector<TextRun> runs;
    TextRun spanRun;
    QString currentType = QStringLiteral("paragraph");
    int currentLevel = 0;
    QString currentStyle;
    QString pendingAlign;
    QString pendingDir;
    QString listKind;
    int listLevel = 0;
    QVector<QString> listStyleStack;
    bool inText = false;
    bool inTable = false;
    struct OdtTableFrame {
        bool nested = false;
        QVector<QVector<TableCell>> rows;
        QVector<TableCell> row;
        QVector<QVector<NestedCell>> nestRows;
        QVector<NestedCell> nestRow;
        bool inCell = false;
        bool covered = false;
        int colSpan = 1;
        int rowSpan = 1;
        int repeat = 1;
        QVector<TextRun> savedRuns;
        QVector<QVector<NestedCell>> cellNested;
    };
    QVector<OdtTableFrame> tableStack;
    struct DrawAcc {
        int depth = 0;
        bool emitted = false;
        QString name;
        QString title;
        QString desc;
        double widthMm = 0;
    };
    DrawAcc draw;
    auto isDrawingName = [](QStringView n) {
        return n == QLatin1String("frame") || n == QLatin1String("rect") || n == QLatin1String("ellipse")
            || n == QLatin1String("circle") || n == QLatin1String("line") || n == QLatin1String("custom-shape")
            || n == QLatin1String("path") || n == QLatin1String("polygon") || n == QLatin1String("polyline")
            || n == QLatin1String("chart");
    };
    auto hasRevisionId = [&](const QString &id) {
        for (const auto &rv : doc.revisions) {
            if (rv.id == id)
                return true;
        }
        return false;
    };
    QVector<int> pendingBookmarkIdx;  // doc.bookmarks[] entries opened in the block being built
    QVector<int> pendingRevisionIdx;  // doc.revisions[] entries opened in the block being built
    // aioffice:extra revisions stay authoritative when present. Native marks fill ids the JSON did not load.
    QMap<QString, int> changeStartOff;   // change-id -> offset at change-start, within current block
    QString curLinkHref; // set while inside <text:a>, applied to runs started within it
    QString pendingHideIf;

    auto startRun = [&]() { spanRun = TextRun{}; spanRun.link = curLinkHref; };
    auto curLen = [&]() {
        int n = 0;
        for (const auto &rr : runs)
            n += rr.text.size();
        n += spanRun.text.size();
        return n;
    };
    auto flushPara = [&]() {
        if (!inText && currentType != QLatin1String("table"))
            return;
        if (!spanRun.text.isEmpty())
            runs.push_back(spanRun);
        Block b;
        b.type = currentType;
        b.level = currentLevel;
        b.style = currentStyle;
        b.align = pendingAlign;
        b.dir = pendingDir;
        b.listKind = listKind;
        b.hideIf = pendingHideIf;
        pendingHideIf.clear();
        b.runs = runs;
        if (b.runs.isEmpty())
            b.runs.push_back(TextRun{});
        if (b.type == QLatin1String("list-item") && b.level <= 0)
            b.level = qMax(1, listLevel);
        doc.assignId(b);
        for (int idx : pendingBookmarkIdx)
            doc.bookmarks[idx].blockId = b.id;
        for (int idx : pendingRevisionIdx)
            doc.revisions[idx].blockId = b.id;
        pendingBookmarkIdx.clear();
        pendingRevisionIdx.clear();
        doc.body.push_back(b);
        runs.clear();
        startRun();
        inText = false;
        currentType = QStringLiteral("paragraph");
        currentLevel = 0;
        currentStyle.clear();
        pendingAlign.clear();
        pendingDir.clear();
    };
    auto placeBookmark = [&](const QString &name) {
        if (name.isEmpty())
            return;
        int idx = -1;
        for (int i = 0; i < doc.bookmarks.size(); ++i) {
            if (doc.bookmarks[i].name == name || doc.bookmarks[i].id == name) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            Bookmark bm;
            bm.name = name;
            bm.id = QStringLiteral("bm%1").arg(doc.bookmarks.size() + 1);
            bm.blockId = QStringLiteral("pending");
            doc.bookmarks.push_back(bm);
            idx = doc.bookmarks.size() - 1;
        }
        doc.bookmarks[idx].offset = curLen();
        if (!pendingBookmarkIdx.contains(idx))
            pendingBookmarkIdx.push_back(idx);
    };
    auto emitDetached = [&](Block block, bool keepId) {
        bool hadText = !spanRun.text.trimmed().isEmpty() || !spanRun.field.isEmpty();
        for (const auto &r : runs) {
            if (!r.text.trimmed().isEmpty() || !r.field.isEmpty())
                hadText = true;
        }
        if (hadText)
            flushPara();
        else {
            runs.clear();
            startRun();
            inText = false;
        }
        if (!keepId || block.id.isEmpty() || doc.indexOf(block.id) >= 0)
            doc.assignId(block);
        doc.body.push_back(block);
    };
    auto parseTableBound = [&](const QString &raw, int *out) -> bool {
        if (raw.isEmpty()) {
            *out = 1;
            return true;
        }
        bool ok = false;
        const qlonglong v = raw.toLongLong(&ok);
        if (!ok || v < 1 || v > kMaxOdtTableColumns) {
            if (error)
                *error = odtTableTooWide();
            return false;
        }
        *out = int(v);
        return true;
    };

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto n = xml.name();
            if (n == QLatin1String("list")) {
                QString sn = xml.attributes().value(QStringLiteral("text:style-name")).toString();
                if (sn.isEmpty() && !listStyleStack.isEmpty())
                    sn = listStyleStack.last();
                listStyleStack.push_back(sn);
                ++listLevel;
                listKind = listKindFor(listStyles, sn, listLevel);
            } else if (n == QLatin1String("list-item")) {
                currentType = QStringLiteral("list-item");
            } else if (n == QLatin1String("table")) {
                if (!tableStack.isEmpty()) {
                    if (!spanRun.text.isEmpty() || !spanRun.field.isEmpty())
                        runs.push_back(spanRun);
                    tableStack.last().savedRuns = runs;
                    runs.clear();
                    startRun();
                } else if (inText) {
                    flushPara();
                }
                OdtTableFrame frame;
                frame.nested = !tableStack.isEmpty();
                tableStack.push_back(frame);
                inTable = true;
            } else if (n == QLatin1String("table-row") && !tableStack.isEmpty()) {
                if (tableStack.last().nested)
                    tableStack.last().nestRow.clear();
                else
                    tableStack.last().row.clear();
            } else if ((n == QLatin1String("table-cell") || n == QLatin1String("covered-table-cell"))
                       && !tableStack.isEmpty()) {
                auto &tb = tableStack.last();
                tb.inCell = true;
                tb.covered = n == QLatin1String("covered-table-cell");
                tb.cellNested.clear();
                const auto attrs = xml.attributes();
                if (!parseTableBound(attrs.value(QStringLiteral("table:number-columns-spanned")).toString(), &tb.colSpan)
                    || !parseTableBound(attrs.value(QStringLiteral("table:number-rows-spanned")).toString(), &tb.rowSpan)
                    || !parseTableBound(attrs.value(QStringLiteral("table:number-columns-repeated")).toString(),
                                        &tb.repeat))
                    return false;
                runs.clear();
                startRun();
                inText = !tb.covered;
                currentType = QStringLiteral("paragraph");
            } else if (isDrawingName(n)) {
                if (draw.depth == 0) {
                    draw = {};
                    draw.name = xml.attributes().value(QStringLiteral("draw:name")).toString();
                    double w = 0;
                    if (parseOdfLengthMm(xml.attributes().value(QStringLiteral("svg:width")).toString(), &w, true)
                        || parseOdfLengthMm(xml.attributes().value(QStringLiteral("width")).toString(), &w, true))
                        draw.widthMm = w;
                }
                ++draw.depth;
            } else if ((n == QLatin1String("title") || n == QLatin1String("desc")) && draw.depth > 0) {
                const QString t = xml.readElementText();
                if (n == QLatin1String("title"))
                    draw.title = t;
                else
                    draw.desc = t;
            } else if (n == QLatin1String("h")) {
                runs.clear();
                startRun();
                pendingHideIf.clear();
                currentType = QStringLiteral("heading");
                currentLevel = xml.attributes().value(QStringLiteral("text:outline-level")).toInt();
                if (currentLevel <= 0)
                    currentLevel = 1;
                const QString stn = xml.attributes().value(QStringLiteral("text:style-name")).toString();
                currentStyle = fromOdfStyleName(stn);
                pendingAlign.clear();
                pendingDir.clear();
                applyParaStyleProps(paraStyles, stn, pendingAlign, pendingDir);
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
                    pendingHideIf.clear();
                    if (currentType != QLatin1String("list-item"))
                        currentType = QStringLiteral("paragraph");
                    currentStyle = fromOdfStyleName(stn);
                    pendingAlign.clear();
                    pendingDir.clear();
                    applyParaStyleProps(paraStyles, stn, pendingAlign, pendingDir);
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
                QStringList parts;
                QString cur;
                int bodyDepth = 0;
                bool inNotePara = false;
                while (!xml.atEnd()) {
                    xml.readNext();
                    if (xml.isStartElement() && xml.name() == QLatin1String("note-body"))
                        ++bodyDepth;
                    else if (xml.isStartElement() && bodyDepth > 0
                             && (xml.name() == QLatin1String("p") || xml.name() == QLatin1String("h"))) {
                        inNotePara = true;
                        cur.clear();
                    } else if (xml.isCharacters() && inNotePara)
                        cur += xml.text();
                    else if (xml.isEndElement() && inNotePara
                             && (xml.name() == QLatin1String("p") || xml.name() == QLatin1String("h"))) {
                        const QString t = cur.trimmed();
                        if (!t.isEmpty())
                            parts.push_back(t);
                        inNotePara = false;
                    } else if (xml.isEndElement() && xml.name() == QLatin1String("note-body"))
                        bodyDepth = qMax(0, bodyDepth - 1);
                    else if (xml.isEndElement() && xml.name() == QLatin1String("note"))
                        break;
                }
                TextRun nr;
                nr.text = parts.join(QStringLiteral("\n"));
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
            } else if (n == QLatin1String("user-field-decl")) {
                const QString name = xml.attributes().value(QStringLiteral("text:name")).toString();
                QString val = xml.attributes().value(QStringLiteral("office:string-value")).toString();
                if (val.isEmpty())
                    val = xml.attributes().value(QStringLiteral("office:value")).toString();
                if (!name.isEmpty() && !doc.vars.contains(name))
                    doc.vars.insert(name, val);
            } else if (n == QLatin1String("hidden-paragraph")) {
                if (inText && !inTable)
                    pendingHideIf = ooowToExpr(xml.attributes().value(QStringLiteral("text:condition")).toString());
            } else if (n == QLatin1String("s") && inText) {
                const QString cAttr = xml.attributes().value(QStringLiteral("text:c")).toString();
                qlonglong count = 1;
                if (!cAttr.isEmpty()) {
                    bool okCount = false;
                    const qlonglong raw = cAttr.toLongLong(&okCount);
                    if (!okCount || raw > 10000) {
                        if (error)
                            *error = QStringLiteral("text:s text:c exceeds 10000");
                        return false;
                    }
                    if (raw > 0)
                        count = raw;
                }
                spanRun.text += QString(int(count), QLatin1Char(' '));
            } else if (n == QLatin1String("drop-down")) {
                if (!spanRun.text.isEmpty() || !spanRun.field.isEmpty())
                    runs.push_back(spanRun);
                startRun();
                QStringList options;
                QString selected;
                QString loose;
                if (!xml.isEndElement()) {
                    while (!xml.atEnd()) {
                        xml.readNext();
                        if (xml.isStartElement() && xml.name() == QLatin1String("label")) {
                            const QString val = xml.attributes().value(QStringLiteral("text:value")).toString();
                            if (!val.isEmpty())
                                options.push_back(val);
                            if (xml.attributes().value(QStringLiteral("text:current-selected"))
                                == QLatin1String("true"))
                                selected = val;
                        } else if (xml.isCharacters() && !xml.isWhitespace()) {
                            loose += xml.text();
                        } else if (xml.isEndElement() && xml.name() == QLatin1String("drop-down"))
                            break;
                    }
                }
                if (selected.isEmpty())
                    selected = loose.trimmed();
                QJsonArray opts;
                for (const auto &opt : options)
                    opts.append(opt);
                QJsonObject jo;
                jo.insert(QStringLiteral("options"), opts);
                jo.insert(QStringLiteral("selected"), selected);
                spanRun.field = QStringLiteral("input_list");
                spanRun.fieldArg = compactJson(jo);
                spanRun.text = selected;
                runs.push_back(spanRun);
                startRun();
            } else if (n == QLatin1String("page-number") || n == QLatin1String("page-count")
                       || n == QLatin1String("date") || n == QLatin1String("title")
                       || n == QLatin1String("author-name") || n == QLatin1String("file-name")
                       || n == QLatin1String("word-count") || n == QLatin1String("user-field-get")
                       || n == QLatin1String("conditional-text") || n == QLatin1String("sequence")
                       || n == QLatin1String("character-count") || n == QLatin1String("paragraph-count")
                       || n == QLatin1String("table-count") || n == QLatin1String("image-count")
                       || n == QLatin1String("subject") || n == QLatin1String("keywords")
                       || n == QLatin1String("creation-date") || n == QLatin1String("modification-date")
                       || n == QLatin1String("editing-cycles") || n == QLatin1String("sender-firstname")
                       || n == QLatin1String("sender-email") || n == QLatin1String("sender-company")
                       || n == QLatin1String("sender-phone-work")) {
                if (!spanRun.text.isEmpty() || !spanRun.field.isEmpty())
                    runs.push_back(spanRun);
                startRun();
                const QXmlStreamAttributes attrs = xml.attributes();
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
                else if (n == QLatin1String("file-name")) {
                    const QString display = attrs.value(QStringLiteral("text:display")).toString();
                    spanRun.field = display.compare(QLatin1String("full"), Qt::CaseInsensitive) == 0
                        ? QStringLiteral("filepath")
                        : QStringLiteral("filename");
                }
                else if (n == QLatin1String("word-count"))
                    spanRun.field = QStringLiteral("word_count");
                else if (n == QLatin1String("conditional-text")) {
                    spanRun.field = QStringLiteral("if");
                    QJsonObject jo;
                    jo.insert(QStringLiteral("cond"),
                              ooowToExpr(attrs.value(QStringLiteral("text:condition")).toString()));
                    jo.insert(QStringLiteral("then"),
                              attrs.value(QStringLiteral("text:string-value-if-true")).toString());
                    jo.insert(QStringLiteral("else"),
                              attrs.value(QStringLiteral("text:string-value-if-false")).toString());
                    spanRun.fieldArg = compactJson(jo);
                } else if (n == QLatin1String("sequence")) {
                    spanRun.field = QStringLiteral("seq");
                    QJsonObject jo;
                    jo.insert(QStringLiteral("name"), attrs.value(QStringLiteral("text:name")).toString());
                    jo.insert(QStringLiteral("format"),
                              seqFormatFromOdf(attrs.value(QStringLiteral("style:num-format")).toString()));
                    spanRun.fieldArg = compactJson(jo);
                } else if (n == QLatin1String("character-count") || n == QLatin1String("paragraph-count")
                           || n == QLatin1String("table-count") || n == QLatin1String("image-count")) {
                    spanRun.field = QStringLiteral("stat");
                    if (n == QLatin1String("character-count"))
                        spanRun.fieldArg = QStringLiteral("chars");
                    else if (n == QLatin1String("paragraph-count"))
                        spanRun.fieldArg = QStringLiteral("paragraphs");
                    else if (n == QLatin1String("table-count"))
                        spanRun.fieldArg = QStringLiteral("tables");
                    else
                        spanRun.fieldArg = QStringLiteral("images");
                } else if (n == QLatin1String("subject") || n == QLatin1String("keywords")
                           || n == QLatin1String("creation-date") || n == QLatin1String("modification-date")
                           || n == QLatin1String("editing-cycles")) {
                    spanRun.field = QStringLiteral("doc_info");
                    if (n == QLatin1String("subject"))
                        spanRun.fieldArg = QStringLiteral("subject");
                    else if (n == QLatin1String("keywords"))
                        spanRun.fieldArg = QStringLiteral("keywords");
                    else if (n == QLatin1String("creation-date"))
                        spanRun.fieldArg = QStringLiteral("created");
                    else if (n == QLatin1String("modification-date"))
                        spanRun.fieldArg = QStringLiteral("modified");
                    else
                        spanRun.fieldArg = QStringLiteral("revision");
                } else if (n == QLatin1String("sender-firstname") || n == QLatin1String("sender-email")
                           || n == QLatin1String("sender-company") || n == QLatin1String("sender-phone-work")) {
                    spanRun.field = QStringLiteral("sender");
                    if (n == QLatin1String("sender-firstname"))
                        spanRun.fieldArg = QStringLiteral("name");
                    else if (n == QLatin1String("sender-email"))
                        spanRun.fieldArg = QStringLiteral("email");
                    else if (n == QLatin1String("sender-company"))
                        spanRun.fieldArg = QStringLiteral("company");
                    else
                        spanRun.fieldArg = QStringLiteral("phone");
                } else {
                    const QString raw = attrs.value(QStringLiteral("text:name")).toString();
                    const int colon = raw.indexOf(QLatin1Char(':'));
                    if (colon >= 0) {
                        spanRun.field = raw.left(colon);
                        spanRun.fieldArg = raw.mid(colon + 1);
                    } else {
                        spanRun.field = raw.isEmpty() ? QStringLiteral("custom") : raw;
                    }
                }
                spanRun.text = xml.readElementText();
                if (spanRun.field == QLatin1String("doc_info")) {
                    if (spanRun.fieldArg == QLatin1String("subject") && doc.subject.isEmpty())
                        doc.subject = spanRun.text;
                    else if (spanRun.fieldArg == QLatin1String("keywords") && doc.keywords.isEmpty())
                        doc.keywords = spanRun.text;
                    else if (spanRun.fieldArg == QLatin1String("created")
                             && !doc.vars.contains(QStringLiteral("doc.created")))
                        doc.vars.insert(QStringLiteral("doc.created"), spanRun.text);
                    else if (spanRun.fieldArg == QLatin1String("modified")
                             && !doc.vars.contains(QStringLiteral("doc.modified")))
                        doc.vars.insert(QStringLiteral("doc.modified"), spanRun.text);
                } else if (spanRun.field == QLatin1String("sender") && !spanRun.fieldArg.isEmpty()) {
                    const QString key = QStringLiteral("sender.") + spanRun.fieldArg;
                    if (!doc.vars.contains(key))
                        doc.vars.insert(key, spanRun.text);
                }
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
            } else if (n == QLatin1String("bookmark") || n == QLatin1String("bookmark-start")) {
                placeBookmark(xml.attributes().value(QStringLiteral("text:name")).toString());
            } else if (n == QLatin1String("change-start") && inText
                       && !hasRevisionId(xml.attributes().value(QStringLiteral("text:change-id")).toString())) {
                changeStartOff[xml.attributes().value(QStringLiteral("text:change-id")).toString()] = curLen();
            } else if (n == QLatin1String("change-end") && inText) {
                const QString cid = xml.attributes().value(QStringLiteral("text:change-id")).toString();
                if (changeStartOff.contains(cid) && !hasRevisionId(cid)) {
                    const TrackedChange meta = tracked.value(cid);
                    Revision rv;
                    rv.id = cid;
                    rv.kind = meta.kind.isEmpty() ? QStringLiteral("insert") : meta.kind;
                    rv.author = meta.author;
                    rv.timestamp = meta.timestamp;
                    rv.blockId = QStringLiteral("pending");
                    rv.start = changeStartOff.take(cid);
                    rv.end = curLen();
                    if (rv.kind == QLatin1String("insert")) {
                        QString plain;
                        for (const auto &rr : runs)
                            plain += rr.text;
                        plain += spanRun.text;
                        rv.text = plain.mid(rv.start, qMax(0, rv.end - rv.start));
                    } else if (rv.kind == QLatin1String("format")) {
                        // D10/A-odf-1: ODF's text:format-change carries no previous
                        // attribute values (unlike DOCX's w:rPrChange), so the payload
                        // we can rebuild from the native mark alone is "what the run
                        // covering [start,end) looks like now", truthy flags only.
                        QJsonObject payload;
                        int pos = 0;
                        auto consider = [&](const TextRun &r) {
                            const int len = r.text.size();
                            const bool overlaps = pos < rv.end && (pos + len) > rv.start;
                            if (overlaps) {
                                if (r.bold)
                                    payload[QStringLiteral("bold")] = true;
                                if (r.italic)
                                    payload[QStringLiteral("italic")] = true;
                                if (r.underline)
                                    payload[QStringLiteral("underline")] = true;
                                if (r.strike)
                                    payload[QStringLiteral("strike")] = true;
                            }
                            pos += len;
                        };
                        for (const auto &rr : runs)
                            consider(rr);
                        consider(spanRun);
                        rv.payload = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
                    }
                    doc.revisions.push_back(rv);
                    pendingRevisionIdx.push_back(doc.revisions.size() - 1);
                }
            } else if (n == QLatin1String("change") && inText
                       && !hasRevisionId(xml.attributes().value(QStringLiteral("text:change-id")).toString())) {
                const QString cid = xml.attributes().value(QStringLiteral("text:change-id")).toString();
                const TrackedChange meta = tracked.value(cid);
                Revision rv;
                rv.id = cid;
                rv.kind = QStringLiteral("delete");
                rv.author = meta.author;
                rv.timestamp = meta.timestamp;
                rv.blockId = QStringLiteral("pending");
                const int at = curLen();
                rv.start = at;
                rv.end = at;
                rv.text = meta.text;
                rv.deletedRuns = meta.deletedRuns;
                doc.revisions.push_back(rv);
                pendingRevisionIdx.push_back(doc.revisions.size() - 1);
            } else if (n == QLatin1String("tracked-changes")) {
                while (!xml.atEnd()) {
                    xml.readNext();
                    if (xml.isEndElement() && xml.name() == QLatin1String("tracked-changes"))
                        break;
                }
            } else if (n == QLatin1String("a")) {
                if (!spanRun.text.isEmpty())
                    runs.push_back(spanRun);
                curLinkHref = xml.attributes().value(QStringLiteral("xlink:href")).toString();
                startRun();
            } else if (n == QLatin1String("image")) {
                QString href = xml.attributes().value(QStringLiteral("href")).toString();
                if (href.isEmpty())
                    href = xml.attributes().value(QStringLiteral("xlink:href")).toString();
                const QString alt = !draw.title.isEmpty() ? draw.title : draw.desc;
                if (!href.isEmpty() || (draw.depth > 0 && figureById.contains(draw.name))) {
                    Block img;
                    bool keepId = false;
                    if (draw.depth > 0 && figureById.contains(draw.name)) {
                        img = figureById.take(draw.name);
                        keepId = true;
                        if (img.altText.isEmpty())
                            img.altText = alt;
                    } else {
                        img.type = QStringLiteral("image");
                        img.altText = alt;
                        img.imagePath = href;
                        img.imageBytes = Zip::file(entries, href);
                        if (img.imageBytes.isEmpty() && !href.isEmpty())
                            img.imageBytes = Zip::file(entries, href.startsWith(QLatin1Char('/')) ? href.mid(1) : href);
                        img.imageWidthMm = draw.widthMm > 0 ? int(qRound(draw.widthMm)) : 80;
                    }
                    emitDetached(img, keepId);
                    if (draw.depth > 0)
                        draw.emitted = true;
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
            } else if (n == QLatin1String("a")) {
                if (!spanRun.text.isEmpty())
                    runs.push_back(spanRun);
                curLinkHref.clear();
                startRun();
            } else if ((n == QLatin1String("table-cell") || n == QLatin1String("covered-table-cell"))
                       && !tableStack.isEmpty()) {
                auto &tb = tableStack.last();
                if (!spanRun.text.isEmpty())
                    runs.push_back(spanRun);
                if (tb.row.size() + tb.nestRow.size() + tb.repeat > kMaxOdtTableColumns) {
                    if (error)
                        *error = odtTableTooWide();
                    return false;
                }
                if (tb.nested) {
                    NestedCell nc;
                    if (!tb.covered) {
                        nc.runs = runs;
                        for (const auto &nrow : tb.cellNested) {
                            for (const auto &inner : nrow) {
                                for (const auto &r : inner.runs) {
                                    if (!r.text.isEmpty())
                                        nc.runs.push_back(r);
                                }
                            }
                        }
                    }
                    for (int i = 0; i < tb.repeat; ++i)
                        tb.nestRow.push_back(nc);
                } else {
                    TableCell cell;
                    if (!tb.covered) {
                        cell.runs = runs;
                        cell.colSpan = tb.colSpan;
                        cell.rowSpan = tb.rowSpan;
                        cell.nested = tb.cellNested;
                    }
                    for (int i = 0; i < tb.repeat; ++i)
                        tb.row.push_back(cell);
                }
                tb.cellNested.clear();
                tb.inCell = false;
                runs.clear();
                startRun();
                inText = false;
            } else if (n == QLatin1String("table-row") && !tableStack.isEmpty()) {
                auto &tb = tableStack.last();
                if (tb.nested) {
                    tb.nestRows.push_back(tb.nestRow);
                    tb.nestRow.clear();
                } else {
                    tb.rows.push_back(tb.row);
                    tb.row.clear();
                }
            } else if (n == QLatin1String("table") && !tableStack.isEmpty()) {
                OdtTableFrame done = tableStack.takeLast();
                if (tableStack.isEmpty()) {
                    Block b;
                    b.type = QStringLiteral("table");
                    b.table = done.rows;
                    doc.assignId(b);
                    doc.body.push_back(b);
                    inTable = false;
                    inText = false;
                } else {
                    auto &outer = tableStack.last();
                    if (outer.cellNested.isEmpty())
                        outer.cellNested = done.nestRows;
                    else {
                        for (const auto &nrow : done.nestRows)
                            outer.cellNested.push_back(nrow);
                    }
                    runs = outer.savedRuns;
                    startRun();
                    inText = outer.inCell && !outer.covered;
                    inTable = true;
                }
            } else if (isDrawingName(n) && draw.depth > 0) {
                --draw.depth;
                if (draw.depth == 0 && !draw.emitted) {
                    Block img;
                    img.type = QStringLiteral("image");
                    img.altText = !draw.title.isEmpty() ? draw.title : draw.desc;
                    img.imageWidthMm = draw.widthMm > 0 ? int(qRound(draw.widthMm)) : 80;
                    emitDetached(img, false);
                    draw.emitted = true;
                }
            } else if (n == QLatin1String("h")) {
                flushPara();
            } else if (n == QLatin1String("p") && inText && !inTable) {
                flushPara();
            } else if (n == QLatin1String("list")) {
                if (!listStyleStack.isEmpty())
                    listStyleStack.removeLast();
                listLevel = qMax(0, listLevel - 1);
                if (listLevel == 0)
                    listKind.clear();
                else if (!listStyleStack.isEmpty())
                    listKind = listKindFor(listStyles, listStyleStack.last(), listLevel);
            }
        }
    }
    if (xml.hasError()) {
        if (error)
            *error = QStringLiteral("content.xml: %1 at line %2")
                         .arg(xml.errorString(), QString::number(xml.lineNumber()));
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
