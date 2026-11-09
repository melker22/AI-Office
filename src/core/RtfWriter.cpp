#include "core/RtfWriter.h"

#include "core/Document.h"
#include "core/FieldExpr.h"
#include "core/RtfLang.h"
#include "core/Zip.h"

#include <algorithm>

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>

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
    QSet<QString> consumedImages;
    QMap<QString, QStringList> cellPlaceholders;
    int defaultFont = 0;
    int defaultSizeHalfPt = 24;
    QStringList authorNames;
    QMap<QString, int> authorIndex;
    QSet<QString> revisionHosts;
    int nextAtn = 1;
    QMap<QString, int> atnIds;
    int nextShpLid = 1;

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

    int langCode(const QString &lang)
    {
        if (lang.isEmpty())
            return 0;
        const int lc = rtfLcidForTag(lang);
        if (lc <= 0) {
            warnOnce(QStringLiteral("RTF export: unknown language \"%1\" written without \\lang").arg(lang));
            return 0;
        }
        return lc;
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

    static QString escapeBookmarkName(const QString &name)
    {
        QString out;
        out.reserve(name.size() * 2);
        for (const QChar c : name) {
            if (c == QLatin1Char('\\'))
                out += QStringLiteral("\\\\");
            else if (c == QLatin1Char('{'))
                out += QStringLiteral("\\{");
            else if (c == QLatin1Char('}'))
                out += QStringLiteral("\\}");
            else
                out += c;
        }
        return out;
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
                // RTF \\uN is a signed 16-bit value. U+8000 and above, including
                // CJK, Hangul, and both halves of an emoji surrogate pair, must
                // be negative or the native reader rejects the file.
                int u = int(c.unicode());
                if (u >= 0x8000)
                    u -= 0x10000;
                out += QStringLiteral("\\u%1?").arg(u);
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
            const int lc = langCode(r.lang);
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
        if (r.field == QLatin1String("filepath") || r.field == QLatin1String("file_path")
            || r.field == QLatin1String("path")) {
            instr = QStringLiteral(" FILENAME \\p ");
            return true;
        }
        if (r.field == QLatin1String("time")) {
            instr = QStringLiteral(" TIME ");
            return true;
        }
        if (r.field == QLatin1String("word_count")) {
            instr = QStringLiteral(" NUMWORDS ");
            return true;
        }
        if (r.field == QLatin1String("merge") || r.field == QLatin1String("mail_merge")) {
            instr = QStringLiteral(" MERGEFIELD %1 ").arg(quoteFieldArg(r.fieldArg));
            return true;
        }
        if (r.field == QLatin1String("var") || r.field == QLatin1String("variable")
            || r.field == QLatin1String("custom")) {
            instr = QStringLiteral(" DOCVARIABLE %1 ").arg(quoteFieldArg(r.fieldArg));
            return true;
        }
        if (r.field == QLatin1String("input")) {
            instr = QStringLiteral(" FILLIN %1 ").arg(quoteFieldArg(r.fieldArg));
            return true;
        }
        if (r.field == QLatin1String("xref")) {
            const QString target = quoteFieldArg(bm(r.fieldArg));
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
            instr = QStringLiteral(" SEQ %1 \\* %2 ").arg(quoteFieldArg(name), fmt);
            return true;
        }
        if (!r.link.isEmpty()) {
            // A local target is the \l switch. Anything else is the URL, and a
            // later import skips \o / \t / \m / \n before that URL.
            if (r.link.startsWith(QLatin1Char('#'))) {
                // Same token marksFor writes on \bkmkstart. No bookmark is
                // emitted when that target was never declared.
                instr = QStringLiteral(" HYPERLINK \\l %1 ").arg(quoteFieldArg(bm(r.link.mid(1))));
            } else
                instr = QStringLiteral(" HYPERLINK %1 ").arg(quoteFieldArg(r.link));
            return true;
        }
        return false;
    }

    // Field arguments are quoted. \\ { } " are escaped inside the quotes.
    // Controls (below 0x20, and DEL) are stripped before RTF escaping.
    static QString quoteFieldArg(const QString &raw)
    {
        QString s;
        s.reserve(raw.size() + 8);
        s += QLatin1Char('"');
        for (const QChar c : raw) {
            const char16_t u = c.unicode();
            if (u < 0x20 || u == 0x7f)
                continue;
            if (c == QLatin1Char('\\') || c == QLatin1Char('{') || c == QLatin1Char('}')
                || c == QLatin1Char('"'))
                s += QLatin1Char('\\');
            s += c;
        }
        s += QLatin1Char('"');
        return s;
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

    QString emitRun(const TextRun &r, int defFont, int defSizeHalfPt, bool inNote = false)
    {
        QString out;
        if (!r.noteId.isEmpty() && !inNote) {
            const Block *nb = doc->findBlock(r.noteId);
            if (nb) {
                // D2: the note's U+2029-separated paragraphs become \par inside the
                // note group; each paragraph keeps its run formatting. A note that
                // carries revisions uses the same walker as body text (wave 28 D2).
                QString body;
                if (blockHasRevision(nb->id))
                    body = emitRevisedRuns(nb->id, nb->runs, defFont, defSizeHalfPt, true);
                else {
                    const QVector<QVector<TextRun>> paras = splitNoteParagraphs(nb->runs);
                    for (int i = 0; i < paras.size(); ++i) {
                        if (i > 0)
                            body += QStringLiteral("\\par ");
                        for (const auto &nr : paras.at(i))
                            body += emitRun(nr, defFont, defSizeHalfPt, true);
                    }
                }
                if (!nb->comments.isEmpty())
                    body = commentStarts(*nb) + body + commentEnds(*nb);
                out += QStringLiteral("{\\footnote");
                if (nb->type == QLatin1String("endnote"))
                    out += QStringLiteral("\\ftnalt");
                // \\chftn is the note reference. The reader synthesizes the marker
                // in the host, so the body must not also contain a visible number.
                out += QStringLiteral("{\\pard\\plain \\chftn %1\\par}}").arg(body);
            }
            return out;
        }
        QString instr;
        if (nativeFieldInstr(r, instr)) {
            // A hyperlink run has link set and field empty. nativeFieldInstr
            // still builds HYPERLINK for that shape. The result group carries
            // the same character properties as a run outside a field, or bold
            // and color on the link are dropped on the round trip.
            const QString result = !r.field.isEmpty() ? fieldResult(r) : (r.text.isEmpty() ? r.link : r.text);
            const QString props = charProps(r, defFont, defSizeHalfPt);
            const QString shown = escapeText(result);
            const QString rslt = props.isEmpty()
                ? shown
                : QLatin1Char('{') + props + QLatin1Char(' ') + shown + QLatin1Char('}');
            out += QStringLiteral("{\\field{\\*\\fldinst %1}{\\fldrslt %2}}")
                       .arg(escapeText(instr.trimmed()), rslt);
            return out;
        }
        if (!r.field.isEmpty()) {
            out += escapeText(fieldResult(r));
            return out;
        }
        const QString props = charProps(r, defFont, defSizeHalfPt);
        const QString txt = escapeText(r.text);
        if (props.isEmpty())
            out += txt;
        else {
            // A space after a control word is only a delimiter. `\plain ` ate the
            // next run's leading space ("bold and" became "boldand"). A group
            // restores character formatting and leaves the following text intact.
            out += QLatin1Char('{') + props + QLatin1Char(' ') + txt + QLatin1Char('}');
        }
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
        // \\pard drops paragraph properties inherited from the previous paragraph
        // (list, outline level, heading spacing) before \\intbl. \\plain drops
        // character properties, including \\lang: an unknown or empty language
        // must not keep the previous paragraph's LCID (wave 25 D1).
        QString p = inTable ? QStringLiteral("\\pard\\intbl\\plain") : QStringLiteral("\\pard\\plain");
        p += paraAlign(b);
        if (b.dir == QLatin1String("rtl"))
            p += QStringLiteral("\\rtlpar");
        if (!b.lang.isEmpty()) {
            const int lc = langCode(b.lang);
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

    bool textVanished(const Block &b) const
    {
        if (b.sectionHidden)
            return true;
        return blockHiddenByExpr(*doc, b, 1, 1);
    }

    QString vanishWrap(const Block &b, const QString &inner)
    {
        if (!textVanished(b))
            return inner;
        warnOnce(QStringLiteral("RTF export: hidden text in block %1 is published with \\v (not redacted)")
                     .arg(b.id));
        if (inner.isEmpty())
            return inner;
        return QStringLiteral("{\\v ") + inner + QLatin1Char('}');
    }

    struct BmMark {
        int offset = 0;
        QString name;
    };

    QVector<BmMark> marksFor(const QString &blockId)
    {
        QVector<BmMark> marks;
        if (blockId.isEmpty())
            return marks;
        for (const auto &bm : doc->bookmarks) {
            if (bm.blockId != blockId)
                continue;
            BmMark mark;
            mark.offset = qMax(0, bm.offset);
            mark.name = escapeBookmarkName(exportedBookmarkName(bm.name.isEmpty() ? bm.id : bm.name));
            marks.push_back(mark);
        }
        std::sort(marks.begin(), marks.end(), [](const BmMark &a, const BmMark &b) {
            if (a.offset != b.offset)
                return a.offset < b.offset;
            return a.name < b.name;
        });
        return marks;
    }

    static QString emitBookmark(const QString &name)
    {
        return QStringLiteral("{\\*\\bkmkstart %1}{\\*\\bkmkend %1}").arg(name);
    }

    int displayedLen(const TextRun &r)
    {
        if (!r.noteId.isEmpty())
            return r.text.size();
        if (r.field == QLatin1String("seq")) {
            const QJsonObject jo = fieldArgObject(r);
            const QString name = jo.value(QStringLiteral("name")).toString();
            const QString format = jo.value(QStringLiteral("format")).toString(QStringLiteral("arabic"));
            const int n = seqShown.value(name, 0) + 1;
            return formatPageNumber(n, format).size();
        }
        if (!r.field.isEmpty())
            return runDisplayText(*doc, r).size();
        if (!r.link.isEmpty())
            return (r.text.isEmpty() ? r.link : r.text).size();
        return r.text.size();
    }

    static bool atomicRun(const TextRun &r)
    {
        return !r.noteId.isEmpty() || !r.field.isEmpty() || !r.link.isEmpty();
    }

    bool blockHasRevision(const QString &blockId) const
    {
        for (const auto &rv : doc->revisions) {
            if (rv.blockId == blockId)
                return true;
        }
        return false;
    }

    void buildAuthors()
    {
        authorNames.clear();
        authorIndex.clear();
        QSet<QString> seen;
        auto take = [&](const QString &author) {
            if (author.isEmpty() || seen.contains(author))
                return;
            seen.insert(author);
            authorIndex.insert(author, authorNames.size() + 1);
            authorNames.append(author);
        };
        QSet<QString> inBody;
        for (const Block &b : doc->body) {
            if (!b.id.isEmpty())
                inBody.insert(b.id);
            for (const Revision &rv : doc->revisions) {
                if (rv.blockId == b.id)
                    take(rv.author);
            }
        }
        for (const Revision &rv : doc->revisions) {
            if (!inBody.contains(rv.blockId))
                take(rv.author);
        }
    }

    static QString escapeRevAuthor(const QString &name)
    {
        QString s = escapeText(name);
        s.replace(QStringLiteral(";"), QStringLiteral("\\'3b"));
        return s;
    }

    QString revTbl() const
    {
        if (doc->revisions.isEmpty())
            return {};
        QString t = QStringLiteral("{\\*\\revtbl{Unknown;}");
        for (const QString &name : authorNames)
            t += QLatin1Char('{') + escapeRevAuthor(name) + QStringLiteral(";}");
        t += QStringLiteral("}\n");
        return t;
    }

    int authorIndexOf(const QString &author) const
    {
        if (author.isEmpty())
            return 0;
        return authorIndex.value(author, 0);
    }

    // RTF DTTM is a signed 32-bit word. Minute precision, UTC. Weekday lives in
    // the top bits, so a Friday can be negative; LibreOffice reads that form.
    static QString dttmNumber(qint64 timestamp)
    {
        if (timestamp == 0)
            return {};
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(timestamp, QTimeZone::utc());
        const QDate date = dt.date();
        const QTime time = dt.time();
        const int dow = date.dayOfWeek() % 7;
        const int year = date.year() - 1900;
        const quint32 n = quint32(time.minute() & 0x3F)
            | (quint32(time.hour() & 0x1F) << 6)
            | (quint32(date.day() & 0x1F) << 11)
            | (quint32(date.month() & 0xF) << 16)
            | (quint32(year & 0x1FF) << 20)
            | (quint32(dow & 7) << 29);
        return QString::number(qint32(n));
    }

    QString openRevised(const Revision *rv) const
    {
        QString s = QStringLiteral("{\\revised\\revauth%1").arg(authorIndexOf(rv->author));
        const QString d = dttmNumber(rv->timestamp);
        if (!d.isEmpty())
            s += QStringLiteral("\\revdttm") + d;
        s += QLatin1Char(' ');
        return s;
    }

    QString crauthProps(const Revision *rv) const
    {
        QString s = QStringLiteral("\\crauth%1").arg(authorIndexOf(rv->author));
        const QString d = dttmNumber(rv->timestamp);
        if (!d.isEmpty())
            s += QStringLiteral("\\crdate") + d;
        s += QLatin1Char(' ');
        return s;
    }

    bool offsetRedacted(const QVector<TextRun> &live, int offset) const
    {
        int pos = 0;
        for (const TextRun &r : live) {
            const int n = r.text.size();
            if (offset >= pos && offset < pos + n)
                return r.redact;
            pos += n;
        }
        return false;
    }

    QStringList secretsForBlock(const QString &blockId) const
    {
        QStringList secrets;
        auto take = [&](const TextRun &r) {
            if (r.redact && !r.text.isEmpty())
                secrets.append(r.text);
        };
        if (const Block *b = doc->findBlock(blockId)) {
            for (const TextRun &r : b->runs)
                take(r);
            for (const auto &row : b->table)
                for (const TableCell &cell : row)
                    for (const TextRun &r : cell.runs)
                        take(r);
        }
        for (const Revision &rv : doc->revisions) {
            if (rv.blockId != blockId)
                continue;
            for (const TextRun &r : rv.deletedRuns)
                take(r);
            for (const TextRun &r : rv.priorRuns)
                take(r);
        }
        std::sort(secrets.begin(), secrets.end(), [](const QString &a, const QString &b) {
            return a.size() > b.size();
        });
        return secrets;
    }

    static QString scrubSecrets(QString text, const QStringList &secrets)
    {
        for (const QString &secret : secrets) {
            if (!secret.isEmpty())
                text.replace(secret, QString());
        }
        return text;
    }

    QString aiorevfmtGroup(const Revision *rv, const QVector<TextRun> &live) const
    {
        const QStringList secrets = secretsForBlock(rv->blockId);
        QJsonArray prior;
        int local = 0;
        for (const TextRun &pr : rv->priorRuns) {
            TextRun copy = pr;
            if (copy.redact) {
                copy.text.clear();
            } else {
                QString kept;
                kept.reserve(copy.text.size());
                for (int i = 0; i < copy.text.size(); ++i) {
                    if (!offsetRedacted(live, rv->start + local + i))
                        kept += copy.text.at(i);
                }
                copy.text = scrubSecrets(kept, secrets);
            }
            local += pr.text.size();
            prior.append(copy.toJson());
        }
        QJsonObject o;
        o.insert(QStringLiteral("payload"), rv->payload);
        o.insert(QStringLiteral("priorRuns"), prior);
        const QString json = QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
        return QStringLiteral("{\\*\\aiorevfmt ") + escapeText(json) + QLatin1Char('}');
    }

    QString emitDeleteGroup(const Revision *rv, int defFont, int defSizeHalfPt, bool inNote)
    {
        QString s = QStringLiteral("{\\deleted\\revauthdel%1").arg(authorIndexOf(rv->author));
        const QString d = dttmNumber(rv->timestamp);
        if (!d.isEmpty())
            s += QStringLiteral("\\revdttmdel") + d;
        s += QLatin1Char(' ');
        QVector<TextRun> gone = rv->deletedRuns;
        if (gone.isEmpty() && !rv->text.isEmpty()) {
            TextRun fallback;
            fallback.text = rv->text;
            gone.push_back(fallback);
        }
        const QStringList secrets = secretsForBlock(rv->blockId);
        for (TextRun slice : gone) {
            if (slice.redact)
                slice.text.clear();
            else
                slice.text = scrubSecrets(slice.text, secrets);
            s += emitRun(slice, defFont, defSizeHalfPt, inNote);
        }
        s += QLatin1Char('}');
        return s;
    }

    // Bookmarks are a point. Start and end sit together at Bookmark::offset,
    // measured in plain text. A mark that lands inside a field stays on the
    // field boundary so the field group is not split.
    QString emitMarkedRuns(const QString &blockId, const QVector<TextRun> &runs, int defFont, int defSizeHalfPt)
    {
        revisionHosts.insert(blockId);
        if (blockHasRevision(blockId))
            return emitRevisedRuns(blockId, runs, defFont, defSizeHalfPt, false);
        return emitBookmarkedRuns(blockId, runs, defFont, defSizeHalfPt);
    }

    QString emitBookmarkedRuns(const QString &blockId, const QVector<TextRun> &runs, int defFont, int defSizeHalfPt)
    {
        const QVector<BmMark> marks = marksFor(blockId);
        QString out;
        int cursor = 0;
        int mi = 0;
        auto emitDue = [&](int upto) {
            while (mi < marks.size() && marks.at(mi).offset <= upto) {
                out += emitBookmark(marks.at(mi).name);
                ++mi;
            }
        };
        for (const TextRun &r : runs) {
            if (atomicRun(r)) {
                const int len = displayedLen(r);
                emitDue(cursor + qMax(0, len) - 1);
                out += emitRun(r, defFont, defSizeHalfPt);
                cursor += qMax(0, len);
                continue;
            }
            const QString text = r.text;
            int local = 0;
            while (local <= text.size()) {
                emitDue(cursor);
                if (local >= text.size())
                    break;
                int next = text.size();
                if (mi < marks.size() && marks.at(mi).offset > cursor) {
                    const int rel = marks.at(mi).offset - cursor;
                    if (rel < text.size() - local)
                        next = local + rel;
                }
                if (next <= local)
                    break;
                TextRun slice = r;
                slice.text = text.mid(local, next - local);
                out += emitRun(slice, defFont, defSizeHalfPt);
                cursor += next - local;
                local = next;
            }
        }
        while (mi < marks.size()) {
            out += emitBookmark(marks.at(mi).name);
            ++mi;
        }
        return out;
    }

    struct RevEv {
        int pos = 0;
        int ord = 0;
        int seq = 0;
        const Revision *rv = nullptr;
        bool open = false;
        bool point = false;
    };

    // Same offset order as the DOCX exporter: close, then a delete point, then open.
    QVector<RevEv> revisionEvents(const QString &blockId, int total) const
    {
        QVector<RevEv> evs;
        int seq = 0;
        for (const Revision &rv : doc->revisions) {
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
        return evs;
    }

    QString emitRevisedRuns(const QString &blockId, const QVector<TextRun> &runs, int defFont, int defSizeHalfPt,
                            bool inNote)
    {
        revisionHosts.insert(blockId);
        int total = 0;
        for (const TextRun &r : runs)
            total += r.text.size();
        const QVector<BmMark> marks = marksFor(blockId);
        const QVector<RevEv> evs = revisionEvents(blockId, total);
        int mi = 0;
        int ei = 0;
        const Revision *insert = nullptr;
        bool insertOpen = false;
        QVector<const Revision *> formats;
        QSet<const Revision *> aiorevDone;
        QString out;

        auto syncInsert = [&]() {
            if (insert && !insertOpen) {
                out += openRevised(insert);
                insertOpen = true;
            } else if (!insert && insertOpen) {
                out += QLatin1Char('}');
                insertOpen = false;
            }
        };
        auto flushEvents = [&](int limit, bool inclusive) {
            while (ei < evs.size()) {
                const RevEv &ev = evs.at(ei);
                if (inclusive ? ev.pos > limit : ev.pos >= limit)
                    break;
                ++ei;
                if (ev.point) {
                    syncInsert();
                    out += emitDeleteGroup(ev.rv, defFont, defSizeHalfPt, inNote);
                } else if (ev.open) {
                    if (ev.rv->kind == QLatin1String("insert")) {
                        if (insert && insert != ev.rv) {
                            insert = nullptr;
                            syncInsert();
                        }
                        insert = ev.rv;
                        syncInsert();
                    } else {
                        formats.push_back(ev.rv);
                    }
                } else if (ev.rv->kind == QLatin1String("insert")) {
                    if (insert == ev.rv)
                        insert = nullptr;
                    syncInsert();
                } else {
                    if (!aiorevDone.contains(ev.rv)) {
                        syncInsert();
                        out += aiorevfmtGroup(ev.rv, runs);
                        aiorevDone.insert(ev.rv);
                    }
                    formats.erase(std::remove(formats.begin(), formats.end(), ev.rv), formats.end());
                }
            }
        };
        auto emitBookmarksUpTo = [&](int upto) {
            while (mi < marks.size() && marks.at(mi).offset <= upto) {
                syncInsert();
                out += emitBookmark(marks.at(mi).name);
                ++mi;
            }
        };
        auto emitSlice = [&](const TextRun &slice) {
            if (slice.text.isEmpty() && !atomicRun(slice))
                return;
            syncInsert();
            const Revision *fmt = formats.isEmpty() ? nullptr : formats.back();
            QString before;
            if (fmt) {
                for (const Revision *f : formats) {
                    if (!aiorevDone.contains(f)) {
                        before += aiorevfmtGroup(f, runs);
                        aiorevDone.insert(f);
                    }
                }
            }
            const QString inner = emitRun(slice, defFont, defSizeHalfPt, inNote);
            if (fmt)
                out += QLatin1Char('{') + crauthProps(fmt) + before + inner + QLatin1Char('}');
            else
                out += before + inner;
        };

        int cursor = 0;
        for (const TextRun &r : runs) {
            if (atomicRun(r)) {
                const int len = r.text.size();
                flushEvents(cursor, true);
                if (len > 0)
                    flushEvents(cursor + len, false);
                emitBookmarksUpTo(cursor + qMax(0, len) - 1);
                emitSlice(r);
                cursor += len;
                continue;
            }
            const QString text = r.text;
            int local = 0;
            while (local <= text.size()) {
                flushEvents(cursor, true);
                emitBookmarksUpTo(cursor);
                if (local >= text.size())
                    break;
                if (inNote && text.at(local) == QChar(0x2029)) {
                    syncInsert();
                    out += QStringLiteral("\\par ");
                    ++cursor;
                    ++local;
                    continue;
                }
                int next = text.size();
                if (mi < marks.size() && marks.at(mi).offset > cursor) {
                    const int rel = marks.at(mi).offset - cursor;
                    if (rel < text.size() - local)
                        next = local + rel;
                }
                if (ei < evs.size() && evs.at(ei).pos > cursor) {
                    const int rel = evs.at(ei).pos - cursor;
                    if (local + rel < next)
                        next = local + rel;
                }
                if (inNote) {
                    const int sep = text.indexOf(QChar(0x2029), local);
                    if (sep >= 0 && sep < next)
                        next = sep;
                }
                if (next <= local)
                    break;
                TextRun slice = r;
                slice.text = text.mid(local, next - local);
                emitSlice(slice);
                cursor += next - local;
                local = next;
            }
        }
        flushEvents(total, true);
        while (mi < marks.size()) {
            syncInsert();
            out += emitBookmark(marks.at(mi).name);
            ++mi;
        }
        if (insertOpen)
            out += QLatin1Char('}');
        return out;
    }

    static QString initialsOf(const QString &author)
    {
        const QString trimmed = author.trimmed();
        if (trimmed.isEmpty())
            return {};
        const QStringList parts = trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() <= 1)
            return QString(trimmed.at(0).toUpper());
        QString out;
        for (const QString &p : parts) {
            if (!p.isEmpty())
                out += p.at(0).toUpper();
        }
        return out;
    }

    static QString escapeCommentBody(const QString &text)
    {
        QString t = text;
        t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        t.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        const QStringList lines = t.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
        QString out;
        for (int i = 0; i < lines.size(); ++i) {
            if (i > 0)
                out += QStringLiteral("\\par ");
            out += escapeText(lines.at(i));
        }
        return out;
    }

    int atnId(const QString &blockId, int index, const Comment &c)
    {
        const QString key = c.id.isEmpty() ? blockId + QLatin1Char('#') + QString::number(index) : c.id;
        if (!atnIds.contains(key))
            atnIds.insert(key, nextAtn++);
        return atnIds.value(key);
    }

    QString commentStarts(const Block &b)
    {
        QString s;
        for (int i = 0; i < b.comments.size(); ++i)
            s += QStringLiteral("{\\*\\atrfstart %1}").arg(atnId(b.id, i, b.comments.at(i)));
        return s;
    }

    QString commentEnds(const Block &b)
    {
        const QStringList secrets = secretsForBlock(b.id);
        QString s;
        for (int i = 0; i < b.comments.size(); ++i) {
            const Comment &c = b.comments.at(i);
            const int n = atnId(b.id, i, c);
            s += QStringLiteral("{\\*\\atrfend %1}").arg(n);
            s += QStringLiteral("{\\*\\atnid %1}").arg(escapeText(initialsOf(c.author)));
            s += QStringLiteral("{\\*\\atnauthor %1}").arg(escapeText(c.author));
            s += QStringLiteral("\\chatn{\\*\\annotation{\\*\\atnref %1}").arg(n);
            s += QStringLiteral("{\\*\\aiocid %1}").arg(escapeText(c.id));
            if (!c.parentId.isEmpty())
                s += QStringLiteral("{\\*\\aiocparent %1}").arg(escapeText(c.parentId));
            if (c.resolved)
                s += QStringLiteral("{\\*\\aiocresolved 1}");
            s += QStringLiteral("\\pard\\plain ");
            s += escapeCommentBody(scrubSecrets(c.text, secrets));
            s += QLatin1Char('}');
        }
        return s;
    }

    QString carrierParagraph(const Block &b)
    {
        if (b.comments.isEmpty())
            return {};
        return QStringLiteral("\\pard\\plain {\\*\\aiocarrier}") + commentStarts(b) + commentEnds(b)
            + QStringLiteral("\\par\n");
    }

    static int colorRef(const QString &hex)
    {
        int r = 0;
        int g = 0;
        int b = 0;
        if (!parseRgb(hex, r, g, b))
            return 0;
        return r | (g << 8) | (b << 16);
    }

    static bool nativeShapeKind(const QString &kind)
    {
        return kind == QLatin1String("rect") || kind == QLatin1String("ellipse") || kind == QLatin1String("line");
    }

    QString emitShape(const Block &b)
    {
        int shapeType = 1;
        if (b.shapeKind == QLatin1String("ellipse"))
            shapeType = 3;
        else if (b.shapeKind == QLatin1String("line"))
            shapeType = 20;
        const double wmm = b.imageWidthMm > 0 ? double(b.imageWidthMm) : 80.0;
        const double hmm = b.heightMm > 0 ? b.heightMm : 40.0;
        const int w = qRound(wmm * kTwipsPerMm / 10.0);
        const int h = qRound(hmm * kTwipsPerMm / 10.0);
        const int lid = nextShpLid++;
        QString sp = QStringLiteral("{\\sp{\\sn shapeType}{\\sv %1}}").arg(shapeType);
        if (b.fill.isEmpty()) {
            sp += QStringLiteral("{\\sp{\\sn fFilled}{\\sv 0}}");
        } else {
            sp += QStringLiteral("{\\sp{\\sn fFilled}{\\sv 1}}");
            sp += QStringLiteral("{\\sp{\\sn fillColor}{\\sv %1}}").arg(colorRef(b.fill));
        }
        if (b.stroke.isEmpty()) {
            sp += QStringLiteral("{\\sp{\\sn fLine}{\\sv 0}}");
        } else {
            sp += QStringLiteral("{\\sp{\\sn fLine}{\\sv 1}}");
            sp += QStringLiteral("{\\sp{\\sn lineColor}{\\sv %1}}").arg(colorRef(b.stroke));
        }
        QString text;
        if (!b.plainText().isEmpty())
            text = QStringLiteral("{\\shptxt ") + paraProps(b) + emitRuns(b.runs, defaultFont, defaultSizeHalfPt)
                + QStringLiteral("\\par}");
        return QStringLiteral(
                   "{\\shp{\\*\\shpinst\\shpleft0\\shptop0\\shpright%1\\shpbottom%2\\shpfhdr0\\shpbxcolumn\\shpbypara"
                   "\\shpwr1\\shpfblwtxt0\\shpz%3\\shplid%4")
                   .arg(w)
                   .arg(h)
                   .arg(b.zIndex)
                   .arg(lid)
            + sp + text + QStringLiteral("}}");
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

    QString imageAlt(const Block &b) const
    {
        return escapeText(b.altText.isEmpty() ? b.plainText() : b.altText);
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
            return imageAlt(b);
        }
        const bool mimeJpeg = b.imageMime.contains(QLatin1String("jpeg"), Qt::CaseInsensitive)
            || b.imageMime.contains(QLatin1String("jpg"), Qt::CaseInsensitive);
        const bool mimePng = b.imageMime.contains(QLatin1String("png"), Qt::CaseInsensitive);
        const bool magicJpeg = bytes.size() >= 2
            && static_cast<unsigned char>(bytes.at(0)) == 0xFF
            && static_cast<unsigned char>(bytes.at(1)) == 0xD8;
        const bool jpeg = mimeJpeg || (!mimePng && magicJpeg);
        QByteArray payload = bytes;
        int pxW = 1;
        int pxH = 1;
        if (jpeg) {
            const QImage probed = QImage::fromData(bytes);
            if (!probed.isNull() && probed.width() > 0 && probed.height() > 0) {
                pxW = probed.width();
                pxH = probed.height();
            }
        } else {
            // \pngblip must be PNG bytes. insert_image labels every non-JPEG
            // suffix as image/png, so BMP, GIF, and WebP would otherwise be
            // written under the wrong blip. Keep the original JPEG bytes.
            const QImage decoded = QImage::fromData(bytes);
            if (decoded.isNull() || decoded.width() <= 0 || decoded.height() <= 0) {
                warnOnce(QStringLiteral("RTF export: image could not be encoded as PNG"));
                return imageAlt(b);
            }
            QByteArray png;
            QBuffer buffer(&png);
            if (!buffer.open(QIODevice::WriteOnly) || !decoded.save(&buffer, "PNG") || png.isEmpty()) {
                warnOnce(QStringLiteral("RTF export: image could not be encoded as PNG"));
                return imageAlt(b);
            }
            payload = png;
            pxW = decoded.width();
            pxH = decoded.height();
        }
        const QString hex = QString::fromLatin1(payload.toHex());
        const int wTwips = qMax(100, qRound(double(b.imageWidthMm) * kTwipsPerMm / 10.0));
        const int hTwips = qMax(1, qRound(double(wTwips) * double(pxH) / double(pxW)));
        return QStringLiteral("{\\pict\\%1\\picw%2\\pich%3\\picwgoal%4\\pichgoal%5 %6}")
            .arg(jpeg ? QStringLiteral("jpegblip") : QStringLiteral("pngblip"))
            .arg(pxW)
            .arg(pxH)
            .arg(wTwips)
            .arg(hTwips)
            .arg(hex);
    }

    void collectFontsColors(const Document &doc)
    {
        // Register the default font first so \\deff0 is Liberation Serif even
        // when the first style or run names another face.
        fontId(QStringLiteral("Liberation Serif"));
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
        for (const Revision &rv : doc.revisions) {
            scanRuns(rv.deletedRuns);
            scanRuns(rv.priorRuns);
        }
        defaultFont = fontId(QStringLiteral("Liberation Serif"));
    }

    QString fontTbl() const
    {
        QString t = QStringLiteral("{\\fonttbl");
        for (int i = 0; i < fontNames.size(); ++i)
            t += QStringLiteral("{\\f%1\\fnil %2;}").arg(i).arg(escapeText(fontNames.at(i)));
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

    // Same numbering as styleSheet(): synthetic Normal is \\s0, then each
    // non-character style. -1 when the name is not in the sheet.
    int styleSlot(const QString &name) const
    {
        if (name.isEmpty())
            return -1;
        int sn = 1;
        for (const auto &s : doc->styles) {
            if (s.kind == QLatin1String("character"))
                continue;
            if (s.name == name)
                return sn;
            ++sn;
        }
        return -1;
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
        // Word: \leveltext and \levelnumbers are groups inside \listlevel, and
        // \listid stays inside {\list ...}. An extra '}' here used to close {\rtf1.
        // \levelnfc 0 is decimal; 23 is a bullet.
        QString t = QStringLiteral("{\\*\\listtable");
        for (auto it = listKindToId.cbegin(); it != listKindToId.cend(); ++it) {
            const QString key = it.key();
            const bool check = key == QLatin1String("checkbox") || key == QLatin1String("task");
            const bool numbered = !check && key != QLatin1String("bullet");
            const QString id = QString::number(it.value());
            const int nfc = numbered ? 0 : 23;
            // Nine levels so \\ilvl round-trips. The reader ignores a level past the table.
            t += QStringLiteral("{\\list\\listtemplateid") + id;
            if (check) {
                const QString kind = key == QLatin1String("task") ? QStringLiteral("task")
                                                                   : QStringLiteral("checkbox");
                t += QStringLiteral("{\\*\\aiolistkind ") + kind + QLatin1Char('}');
            }
            for (int level = 0; level < 9; ++level) {
                Q_UNUSED(level);
                t += QStringLiteral("{\\listlevel\\levelnfc") + QString::number(nfc)
                    + QStringLiteral("\\leveljc0\\levelstartat1");
                if (numbered)
                    t += QStringLiteral("{\\leveltext\\'02\\'00.;}{\\levelnumbers\\'01;}");
                else if (check)
                    t += QStringLiteral("{\\leveltext\\'01\\u9744?;}{\\levelnumbers;}");
                else
                    t += QStringLiteral("{\\leveltext\\'01\\u-3913 ?;}{\\levelnumbers;}");
                t += QLatin1Char('}');
            }
            t += QStringLiteral("\\listid") + id + QLatin1Char('}');
        }
        t += QLatin1Char('}');
        t += QStringLiteral("{\\*\\listoverridetable");
        for (auto it = listKindToId.cbegin(); it != listKindToId.cend(); ++it) {
            const QString id = QString::number(it.value());
            t += QStringLiteral("{\\listoverride\\listid") + id
                + QStringLiteral("\\listoverridecount0\\ls") + id + QLatin1Char('}');
        }
        t += QStringLiteral("}\n");
        return t;
    }

    QString emitTable(const Block &b)
    {
        QString out;
        int rowIdx = 0;
        const int cols = b.table.isEmpty() ? 0 : b.table.first().size();
        // cellx is the right edge of each column. A leading 0 is a zero-width
        // column, not a cell boundary.
        QVector<int> cellBoundaries;
        int acc = 0;
        QVector<int> widths;
        const bool given = !b.colWidthsMm.isEmpty();
        if (!given) {
            const int colW = cols > 0 ? 9000 / cols : 9000;
            for (int c = 0; c < cols; ++c)
                widths.append(qMax(1, colW));
        } else {
            // A column past the end of a short width list, or with a width of
            // 0 mm or less, gets the even default, not a one-twip sliver.
            const int fallback = qMax(1, cols > 0 ? 9000 / cols : 9000);
            for (int c = 0; c < cols; ++c) {
                int tw = fallback;
                if (c < b.colWidthsMm.size() && b.colWidthsMm.at(c) > 0)
                    tw = qRound(b.colWidthsMm.at(c) * kTwipsPerMm / 10.0);
                widths.append(qMax(1, tw));
            }
        }
        for (int tw : widths) {
            acc += tw;
            cellBoundaries.append(acc);
        }
        for (const auto &row : b.table) {
            QString rowDef = QStringLiteral("\\trowd");
            if (b.headerRow && rowIdx == 0)
                rowDef += QStringLiteral("\\trhdr");
            for (int cx : cellBoundaries)
                rowDef += QStringLiteral("\\cellx%1").arg(cx);
            QString cells;
            int colIdx = 0;
            for (const auto &cell : row) {
                QString inner = emitRuns(cell.runs, defaultFont, defaultSizeHalfPt);
                const QString key = b.id + QLatin1Char('\n') + QString::number(rowIdx)
                    + QLatin1Char('\n') + QString::number(colIdx);
                for (const QString &label : cellPlaceholders.value(key)) {
                    TextRun extra;
                    extra.text = label;
                    inner += emitRun(extra, defaultFont, defaultSizeHalfPt);
                }
                cells += paraProps(b, true) + vanishWrap(b, inner) + QStringLiteral("\\cell ");
                ++colIdx;
            }
            out += rowDef + cells + QStringLiteral("\\row\n");
            ++rowIdx;
        }
        return out;
    }

    static bool parseCellAnchor(const QString &anchor, QString &tableId, int &row, int &col)
    {
        const QString a = anchor.trimmed();
        const int c1 = a.indexOf(QLatin1Char(':'));
        const int c2 = c1 >= 0 ? a.indexOf(QLatin1Char(':'), c1 + 1) : -1;
        if (c1 <= 0 || c2 <= c1 || a.indexOf(QLatin1Char(':'), c2 + 1) >= 0)
            return false;
        const QString rowText = a.mid(c1 + 1, c2 - c1 - 1);
        const QString colText = a.mid(c2 + 1);
        bool okRow = false;
        bool okCol = false;
        row = rowText.toInt(&okRow);
        col = colText.toInt(&okCol);
        if (!okRow || !okCol || row < 0 || col < 0 || rowText.isEmpty() || colText.isEmpty())
            return false;
        tableId = a.left(c1);
        if (tableId.isEmpty() || !tableId.at(0).isLetter())
            return false;
        for (const QChar ch : tableId) {
            if (!ch.isLetterOrNumber() && ch != QLatin1Char('_'))
                return false;
        }
        return true;
    }

    static QString imageLabel(const Block &b)
    {
        QString name = b.altText.trimmed();
        if (name.isEmpty())
            name = QFileInfo(b.imagePath).fileName();
        if (name.isEmpty())
            name = QStringLiteral("imagem");
        return QStringLiteral("[imagem: %1]").arg(name);
    }

    void absorbCellImages()
    {
        consumedImages.clear();
        cellPlaceholders.clear();
        QHash<QString, const Block *> tables;
        for (const Block &candidate : doc->body) {
            if (candidate.type == QLatin1String("table") && !candidate.id.isEmpty() && !tables.contains(candidate.id))
                tables.insert(candidate.id, &candidate);
        }
        for (const Block &img : doc->body) {
            if (img.type != QLatin1String("image"))
                continue;
            QString tableId;
            int row = 0;
            int col = 0;
            if (!parseCellAnchor(img.anchor, tableId, row, col))
                continue;
            const Block *table = tables.value(tableId, nullptr);
            const bool hit = table && row < table->table.size() && col < table->table.at(row).size();
            const QString label = imageLabel(img);
            if (!hit) {
                warnOnce(QStringLiteral(
                             "RTF export: image %1 anchor does not match a table cell; written as \\pict")
                             .arg(img.id));
                continue;
            }
            const QString key = tableId + QLatin1Char('\n') + QString::number(row) + QLatin1Char('\n')
                + QString::number(col);
            cellPlaceholders[key].append(label);
            consumedImages.insert(img.id);
            warnOnce(QStringLiteral("RTF export: image %1 in a table cell replaced with %2").arg(img.id, label));
        }
    }

    QString bodyText()
    {
        absorbCellImages();
        QString body;
        const auto &blocks = doc->body;
        for (int i = 0; i < blocks.size(); ++i) {
            const Block &b = blocks[i];
            if (b.type == QLatin1String("footnote") || b.type == QLatin1String("endnote"))
                continue;
            if (nativeShapeKind(b.shapeKind)) {
                if (!b.groupId.isEmpty() || !b.connectTo.isEmpty())
                    warnOnce(QStringLiteral("RTF export: shape %1 group or connector is not written").arg(b.id));
                body += paraProps(b) + commentStarts(b) + emitShape(b) + commentEnds(b) + QStringLiteral("\\par\n");
                continue;
            }
            if (!b.shapeKind.isEmpty() || !b.qrText.isEmpty() || b.formKind == QLatin1String("dropdown")) {
                if (!b.qrText.isEmpty())
                    warnOnce(QStringLiteral("RTF export: QR exported as plain text"));
                else if (b.formKind == QLatin1String("dropdown"))
                    warnOnce(QStringLiteral("RTF export: dropdown exported as plain text"));
                else
                    warnOnce(QStringLiteral("RTF export: shape %1 exported as plain text").arg(b.id));
                body += paraProps(b) + commentStarts(b) + escapeText(b.plainText()) + commentEnds(b)
                    + QStringLiteral("\\par\n");
                continue;
            }
            if (b.type == QLatin1String("break")) {
                if (b.breakKind == QLatin1String("column"))
                    body += QStringLiteral("\\column");
                else
                    body += QStringLiteral("\\page");
                body += QStringLiteral("\\par\n");
                body += carrierParagraph(b);
                continue;
            }
            if (b.type == QLatin1String("list-item")) {
                const QString kind = b.listKind;
                const int ls = listIdForKind(kind);
                const bool check = kind == QLatin1String("checkbox") || kind == QLatin1String("task");
                while (i < blocks.size() && blocks[i].type == QLatin1String("list-item")
                       && blocks[i].listKind == kind) {
                    const Block &li = blocks[i];
                    const int ilvl = qBound(0, (li.level > 0 ? li.level : 1) - 1, 8);
                    QString inner = paraProps(li) + QStringLiteral("\\ls%1\\ilvl%2 ").arg(ls).arg(ilvl);
                    QString marker;
                    if (check) {
                        const QChar box(li.checked ? 0x2611 : 0x2610);
                        inner += QStringLiteral("{\\*\\aiocheck %1}").arg(li.checked ? 1 : 0);
                        marker = QStringLiteral("{\\listtext %1\\tab}").arg(escapeText(QString(box)));
                    }
                    // A hidden item hides its marker too: the checkbox \\listtext goes
                    // inside the \\v group, and a hidden paragraph mark (\\v\\par)
                    // makes Word drop the list number or bullet.
                    const bool hidden = textVanished(li);
                    inner += commentStarts(li);
                    inner += vanishWrap(li, marker + emitMarkedRuns(li.id, li.runs, defaultFont, defaultSizeHalfPt));
                    inner += commentEnds(li)
                        + (hidden ? QStringLiteral("\\v\\par\\v0\n") : QStringLiteral("\\par\n"));
                    body += inner;
                    ++i;
                }
                --i;
                continue;
            }
            if (b.type == QLatin1String("table")) {
                // \\pard resets \\intbl. A closing \\par here becomes an empty
                // paragraph on import, and each round trip adds another one.
                body += emitTable(b);
                body += QStringLiteral("\\pard\n");
                body += carrierParagraph(b);
                continue;
            }
            if (b.type == QLatin1String("image")) {
                if (consumedImages.contains(b.id))
                    continue;
                body += paraProps(b) + pictPng(b) + QStringLiteral("\\par\n");
                body += carrierParagraph(b);
                continue;
            }
            QString inner = paraProps(b);
            if (b.type == QLatin1String("heading")) {
                const int level = qBound(1, b.level > 0 ? b.level : 1, 9);
                const QString styleName = b.style.trimmed().isEmpty()
                    ? QStringLiteral("Heading %1").arg(level)
                    : b.style.trimmed();
                const int slot = styleSlot(styleName);
                if (slot >= 0)
                    inner += QStringLiteral("\\s%1 ").arg(slot);
                // RTF outline levels are 0-based. The reader maps that back to Heading N.
                inner += QStringLiteral("\\outlinelevel%1 ").arg(level - 1);
                if (b.spaceBeforePt < 0)
                    inner += QStringLiteral("\\sb240 ");
                if (b.spaceAfterPt < 0)
                    inner += QStringLiteral("\\sa120 ");
            }
            inner += commentStarts(b);
            inner += vanishWrap(b, emitMarkedRuns(b.id, b.runs, defaultFont, defaultSizeHalfPt));
            inner += commentEnds(b) + QStringLiteral("\\par\n");
            body += inner;
        }
        for (const Revision &rv : doc->revisions) {
            if (!revisionHosts.contains(rv.blockId))
                warnOnce(QStringLiteral("RTF export: revision %1 is not written").arg(rv.id));
        }
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
    o.buildAuthors();
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
    rtf += o.revTbl();
    rtf += o.listTable();
    rtf += QStringLiteral("\\viewkind4\n");
    rtf += o.bodyText();
    rtf += QLatin1Char('}');

    return writeFileAtomic(path, rtf.toUtf8(), error);
}

} // namespace aioffice
