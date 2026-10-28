#include "core/Epub.h"

#include "core/Document.h"
#include "core/FieldExpr.h"
#include "core/Preview.h"
#include "core/Xhtml.h"
#include "core/Zip.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <functional>

namespace aioffice {
namespace {

constexpr int kMaxImageBytes = 32 * 1024 * 1024;

// Same walk as the PDF catalog language: the first block or run language, else pt-PT.
QString rawDocumentLanguage(const Document &doc)
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

bool isBcp47(const QString &tag)
{
    // Primary, then script/region/variant (2–8), then a singleton extension
    // (en-x-custom, de-u-co-phonebk) that must carry at least one subtag.
    static const QRegularExpression re(QStringLiteral(
        "^[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})*(?:-[A-Za-z0-9](?:-[A-Za-z0-9]{1,8})+)*$"));
    return re.match(tag).hasMatch();
}

QString usableLang(const QString &raw, const QString &fallback)
{
    QString t = raw.trimmed();
    t.replace(QLatin1Char('_'), QLatin1Char('-'));
    if (isBcp47(t))
        return t;
    return fallback;
}

bool langIsRtl(const QString &lang)
{
    const QString primary = lang.section(QLatin1Char('-'), 0, 0).toLower();
    return primary == QLatin1String("ar") || primary == QLatin1String("fa") || primary == QLatin1String("he")
        || primary == QLatin1String("ps") || primary == QLatin1String("ur") || primary == QLatin1String("dv")
        || primary == QLatin1String("yi");
}

bool omittedBlock(const Document &doc, const Block &b)
{
    if (b.sectionHidden)
        return true;
    return blockHiddenByExpr(doc, b, 1, 1);
}

bool isNote(const Block &b)
{
    return b.type == QLatin1String("footnote") || b.type == QLatin1String("endnote");
}

int headingLevel(const Block &b)
{
    return qBound(1, b.level > 0 ? b.level : 1, 6);
}

bool isChapterHeading(const Block &b)
{
    if (b.type != QLatin1String("heading"))
        return false;
    const int lv = b.level > 0 ? b.level : 1;
    return lv == 1;
}

int listLevel(const Block &b)
{
    return qBound(1, b.level > 0 ? b.level : 1, 9);
}

QString listTag(const Block &b)
{
    if (b.listKind == QLatin1String("decimal") || b.listKind == QLatin1String("number"))
        return QStringLiteral("ol");
    return QStringLiteral("ul");
}

QString blockPlain(const Document &doc, const Block &b)
{
    QString t;
    for (const auto &r : b.runs)
        t += runDisplayText(doc, r, 1, 1, &r);
    return t;
}

QString xmlIdFor(const QString &id, QHash<QString, QString> *assigned)
{
    if (assigned) {
        const auto it = assigned->constFind(id);
        if (it != assigned->cend())
            return it.value();
    }
    QString o;
    for (const QChar c : id) {
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_') || c == QLatin1Char('.'))
            o += c;
        else
            o += QLatin1Char('-');
    }
    if (o.isEmpty() || !(o.at(0).isLetter() || o.at(0) == QLatin1Char('_')))
        o.prepend(QStringLiteral("id-"));
    if (!assigned)
        return o;
    QSet<QString> taken;
    for (auto it = assigned->cbegin(); it != assigned->cend(); ++it)
        taken.insert(it.value());
    QString unique = o;
    int n = 2;
    while (taken.contains(unique))
        unique = o + QLatin1Char('-') + QString::number(n++);
    assigned->insert(id, unique);
    return unique;
}

QString cssFont(QString font)
{
    font.remove(QRegularExpression(QStringLiteral("[\\\\\"'<>{};]")));
    font = font.simplified();
    if (font.isEmpty())
        font = QStringLiteral("Liberation Serif");
    return font;
}

bool isHexColor(const QString &c)
{
    static const QRegularExpression re(QStringLiteral("^#[0-9A-Fa-f]{3}([0-9A-Fa-f]{3})?$"));
    return re.match(c).hasMatch();
}

bool isCssAlign(const QString &a)
{
    return a == QLatin1String("left") || a == QLatin1String("right") || a == QLatin1String("center")
        || a == QLatin1String("justify") || a == QLatin1String("start") || a == QLatin1String("end");
}

QString cssClassName(const QString &name)
{
    if (name.trimmed().isEmpty())
        return {};
    QString o = QStringLiteral("s");
    for (const QChar c : name) {
        if (c.isLetterOrNumber())
            o += c;
        else if (!o.endsWith(QLatin1Char('-')))
            o += QLatin1Char('-');
    }
    while (o.endsWith(QLatin1Char('-')))
        o.chop(1);
    if (o.size() < 2)
        return {};
    return o;
}

QString num(double v, int decimals)
{
    return QString::number(v, 'f', decimals);
}

QString blockInlineStyle(const Block &b)
{
    QStringList decls;
    if (isCssAlign(b.align))
        decls << QStringLiteral("text-align: ") + b.align;
    if (b.indentLeftMm > 0.01)
        decls << QStringLiteral("margin-left: ") + num(qBound(0.0, b.indentLeftMm, 200.0), 2) + QStringLiteral("mm");
    if (b.indentRightMm > 0.01)
        decls << QStringLiteral("margin-right: ") + num(qBound(0.0, b.indentRightMm, 200.0), 2) + QStringLiteral("mm");
    if (qAbs(b.indentFirstMm) > 0.01)
        decls << QStringLiteral("text-indent: ") + num(qBound(-50.0, b.indentFirstMm, 50.0), 2) + QStringLiteral("mm");
    if (b.spaceBeforePt >= 0)
        decls << QStringLiteral("margin-top: ") + num(qBound(0.0, b.spaceBeforePt, 200.0), 1) + QStringLiteral("pt");
    if (b.spaceAfterPt >= 0)
        decls << QStringLiteral("margin-bottom: ") + num(qBound(0.0, b.spaceAfterPt, 200.0), 1) + QStringLiteral("pt");
    if (b.lineHeight > 0.5 && b.lineHeight < 5.0 && qAbs(b.lineHeight - 1.0) > 0.001)
        decls << QStringLiteral("line-height: ") + num(b.lineHeight, 2);
    return decls.join(QStringLiteral("; "));
}

QString runInlineStyle(const TextRun &r)
{
    QStringList decls;
    if (!r.font.trimmed().isEmpty())
        decls << QStringLiteral("font-family: \"") + cssFont(r.font) + QStringLiteral("\"");
    if (r.sizePt > 0 && r.sizePt < 200)
        decls << QStringLiteral("font-size: ") + num(r.sizePt, 1) + QStringLiteral("pt");
    if (isHexColor(r.color))
        decls << QStringLiteral("color: ") + r.color;
    if (isHexColor(r.highlight))
        decls << QStringLiteral("background-color: ") + r.highlight;
    if (r.smallCaps)
        decls << QStringLiteral("font-variant: small-caps");
    if (qAbs(r.kerning) > 0.01)
        decls << QStringLiteral("letter-spacing: ") + num(qBound(-0.2, r.kerning / 100.0, 1.0), 3) + QStringLiteral("em");
    return decls.join(QStringLiteral("; "));
}

// First style name keeps the base class. A later name that sanitizes to the same
// class gets a stable suffix in doc.styles order (-2, -3, …), the same pattern as xml ids.
QString stylesheet(const Document &doc, QHash<QString, QString> *classes)
{
    QString css;
    css += QStringLiteral(
        "body { font-family: \"Liberation Serif\", serif; font-size: 12pt; color: #1A1F24; line-height: 1.45; }\n"
        "img { max-width: 100%; height: auto; }\n"
        "figure { margin: 0.8em 0; }\n"
        "figcaption { font-size: 0.9em; text-align: center; }\n"
        "table { border-collapse: collapse; width: 100%; margin: 0.6em 0; }\n"
        "th, td { border: 1px solid #C4B8A8; padding: 0.3em 0.5em; vertical-align: top; }\n"
        "th { font-weight: bold; }\n"
        "ul, ol { margin: 0.4em 0; padding-left: 1.4em; }\n"
        "li { margin: 0.15em 0; }\n"
        "ul.checklist { list-style: none; padding-left: 0.2em; }\n"
        "aside.footnote, aside.endnote { margin-top: 1.2em; padding-top: 0.4em; border-top: 1px solid #C4B8A8; font-size: 0.9em; }\n"
        "a.noteref { font-size: 0.75em; vertical-align: super; text-decoration: none; }\n");
    QSet<QString> taken;
    QSet<QString> seenNames;
    for (const auto &s : doc.styles) {
        if (s.name.isEmpty() || seenNames.contains(s.name))
            continue;
        const QString base = cssClassName(s.name);
        if (base.isEmpty())
            continue;
        seenNames.insert(s.name);
        QString cls = base;
        int n = 2;
        while (taken.contains(cls))
            cls = base + QLatin1Char('-') + QString::number(n++);
        taken.insert(cls);
        if (classes)
            classes->insert(s.name, cls);
        QStringList sels;
        sels << QStringLiteral(".") + cls;
        if (s.name.startsWith(QLatin1String("Heading "))) {
            bool ok = false;
            const int lv = s.name.mid(8).trimmed().toInt(&ok);
            if (ok && lv >= 1 && lv <= 6)
                sels.prepend(QStringLiteral("h%1").arg(lv));
        }
        QStringList decls;
        decls << QStringLiteral("font-family: \"%1\", serif").arg(cssFont(s.font));
        if (s.sizePt > 0 && s.sizePt < 200)
            decls << QStringLiteral("font-size: %1pt").arg(num(s.sizePt, 1));
        decls << (s.bold ? QStringLiteral("font-weight: bold") : QStringLiteral("font-weight: normal"));
        decls << (s.italic ? QStringLiteral("font-style: italic") : QStringLiteral("font-style: normal"));
        if (isHexColor(s.color))
            decls << QStringLiteral("color: ") + s.color;
        if (s.spaceBeforePt > 0)
            decls << QStringLiteral("margin-top: %1pt").arg(num(qBound(0.0, s.spaceBeforePt, 200.0), 1));
        if (s.spaceAfterPt >= 0)
            decls << QStringLiteral("margin-bottom: %1pt").arg(num(qBound(0.0, s.spaceAfterPt, 200.0), 1));
        if (isCssAlign(s.align))
            decls << QStringLiteral("text-align: ") + s.align;
        css += sels.join(QStringLiteral(", "));
        css += QStringLiteral(" { ");
        css += decls.join(QStringLiteral("; "));
        css += QStringLiteral("; }\n");
    }
    return css;
}

QString publicationId(const Document &doc)
{
    // The model has no separate document id. UUID version 5 of title and author
    // is stable across exports of the same publication.
    static const QUuid kNs(QStringLiteral("{8c0e2b1a-7d4f-4e2a-9b11-6f0a0ff1ce20}"));
    const QString name = doc.title + QLatin1Char('\n') + doc.author;
    const QString hex = QUuid::createUuidV5(kNs, name.toUtf8()).toString(QUuid::WithoutBraces);
    return QStringLiteral("urn:uuid:") + hex;
}

struct Chapter {
    QString href;
    QString manifestId;
    QString title;
    bool front = false;
    QVector<int> indexes;
};

struct HeadingRef {
    int level = 1;
    QString title;
    QString href;
};

struct Asset {
    QString manifestId;
    QString href;
    QString mime;
    QByteArray bytes;
};

struct NoteUse {
    QString refId;
    QString label;
    int chapter = 0;
};

struct WriteCtx {
    const Document *doc = nullptr;
    QString docLang;
    int chapterIndex = 0;
    QString chapterHref;
    const QHash<QString, QString> *blockHref = nullptr;
    const QHash<QString, int> *noteChapter = nullptr;
    const QVector<Chapter> *chapters = nullptr;
    QHash<QString, QVector<NoteUse>> *noteUses = nullptr;
    QVector<QString> *noteOrder = nullptr;
    const QVector<QString> *orphans = nullptr;
    QVector<Asset> *assets = nullptr;
    QString *error = nullptr;
    QHash<QString, QString> *xmlIds = nullptr;
    const QHash<QString, QString> *styleClass = nullptr;
    QStringList *warnings = nullptr;
};

using Attrs = QVector<QPair<QString, QString>>;

void addLangDir(Attrs &attrs, const QString &lang, const QString &dir)
{
    if (!lang.isEmpty()) {
        attrs.push_back({QStringLiteral("xml:lang"), lang});
        attrs.push_back({QStringLiteral("lang"), lang});
    }
    if (dir == QLatin1String("rtl") || dir == QLatin1String("ltr"))
        attrs.push_back({QStringLiteral("dir"), dir});
}

QString blockLang(const Block &b, const QString &docLang)
{
    return usableLang(b.lang, docLang);
}

// Explicit ltr/rtl wins. Otherwise the first strong character, the same rule as
// Preview.cpp resolvedParagraphDirection. That helper is file-local, so the rule is duplicated here.
QString resolvedDir(const Document &doc, const Block &b)
{
    if (b.dir == QLatin1String("rtl") || b.dir == QLatin1String("ltr"))
        return b.dir;
    QString text;
    for (const auto &r : b.runs)
        text += runDisplayText(doc, r, 1, 1, &r);
    return text.isRightToLeft() ? QStringLiteral("rtl") : QStringLiteral("ltr");
}

QString classFor(const WriteCtx &ctx, const QString &name)
{
    if (name.trimmed().isEmpty())
        return {};
    if (ctx.styleClass) {
        const auto it = ctx.styleClass->constFind(name);
        if (it != ctx.styleClass->cend())
            return it.value();
    }
    return cssClassName(name);
}

void warnOnce(WriteCtx &ctx, const QString &message)
{
    if (!ctx.warnings || ctx.warnings->contains(message))
        return;
    ctx.warnings->push_back(message);
}

bool runIsSuppressed(const Document &doc, const TextRun &r)
{
    if (r.hidden)
        return true;
    if (r.showIf.isEmpty())
        return false;
    if (showIfIsBareName(r.showIf)) {
        const QString v = doc.vars.value(r.showIf);
        return v.isEmpty() || v == QLatin1String("0") || v == QLatin1String("false");
    }
    const FieldExprResult ev = FieldExpr::evaluate(r.showIf, doc, fieldContextFor(doc, 1, 1));
    if (!ev.ok)
        return false;
    return !fieldExprIsTruthy(ev.value);
}

QString cssQuotedMarker(const QString &bullet)
{
    QString body;
    for (const QChar c : bullet) {
        if (c.category() == QChar::Other_Control)
            continue;
        if (c == QLatin1Char('\\') || c == QLatin1Char('"'))
            body += QLatin1Char('\\');
        body += c;
    }
    return QStringLiteral("list-style-type: \"") + body + QStringLiteral(" \"");
}

void appendStyle(Attrs &attrs, const QString &decl)
{
    if (decl.isEmpty())
        return;
    for (auto &pair : attrs) {
        if (pair.first != QLatin1String("style"))
            continue;
        if (!pair.second.isEmpty())
            pair.second += QStringLiteral("; ");
        pair.second += decl;
        return;
    }
    attrs.push_back({QStringLiteral("style"), decl});
}

QString runsVisibleText(const Document &doc, const QVector<TextRun> &runs)
{
    QString t;
    for (const auto &r : runs) {
        if (r.field == QLatin1String("page_number") || r.field == QLatin1String("page_count"))
            continue;
        if (r.field == QLatin1String("xref") && r.xrefKind == QLatin1String("page"))
            continue;
        t += runDisplayText(doc, r, 1, 1, &r);
    }
    return t;
}

bool isCaptionTarget(const Block &b)
{
    return b.type == QLatin1String("image") || b.type == QLatin1String("table") || b.type == QLatin1String("equation")
        || b.type == QLatin1String("chart") || b.type == QLatin1String("shape") || b.type == QLatin1String("qr");
}

QString blockVisibleText(const Document &doc, const Block &b)
{
    if (isCaptionTarget(b)) {
        const QString cap = captionDisplayText(doc, b, 1, 1).trimmed();
        if (!cap.isEmpty())
            return cap;
        if (!b.altText.trimmed().isEmpty())
            return b.altText.trimmed();
    }
    const QString shown = runsVisibleText(doc, b.runs);
    if (!shown.trimmed().isEmpty())
        return shown;
    const QString plain = b.plainText();
    if (!plain.trimmed().isEmpty())
        return plain;
    return {};
}

QString xrefPageHref(const WriteCtx &ctx, const QString &targetId)
{
    const QString direct = ctx.blockHref ? ctx.blockHref->value(targetId) : QString();
    if (!direct.isEmpty())
        return direct;
    if (ctx.doc->findBlock(targetId))
        return QStringLiteral("#") + xmlIdFor(targetId, ctx.xmlIds);
    for (const auto &bm : ctx.doc->bookmarks) {
        if (bm.id != targetId && bm.name != targetId)
            continue;
        const QString via = ctx.blockHref ? ctx.blockHref->value(bm.blockId) : QString();
        if (!via.isEmpty())
            return via;
        if (!bm.blockId.isEmpty())
            return QStringLiteral("#") + xmlIdFor(bm.blockId, ctx.xmlIds);
    }
    if (targetId.isEmpty())
        return {};
    return QStringLiteral("#") + xmlIdFor(targetId, ctx.xmlIds);
}

QString xrefPageLabel(const Document &doc, const QString &targetId)
{
    if (const Block *tgt = doc.findBlock(targetId)) {
        const QString shown = blockVisibleText(doc, *tgt);
        if (!shown.isEmpty())
            return shown;
        return targetId;
    }
    for (const auto &bm : doc.bookmarks) {
        if (bm.id != targetId && bm.name != targetId)
            continue;
        if (const Block *b = doc.findBlock(bm.blockId)) {
            const QString shown = blockVisibleText(doc, *b);
            if (!shown.isEmpty())
                return shown;
        }
        if (!bm.name.isEmpty())
            return bm.name;
    }
    return targetId;
}

void writeTextBreaks(XhtmlWriter &x, const QString &s)
{
    const QStringList parts = s.split(QLatin1Char('\n'));
    for (int i = 0; i < parts.size(); ++i) {
        if (i > 0)
            x.empty(QStringLiteral("br"));
        if (!parts.at(i).isEmpty())
            x.text(parts.at(i));
    }
}

QString resolveHref(const WriteCtx &ctx, const QString &link)
{
    if (link.startsWith(QLatin1Char('#'))) {
        const QString id = link.mid(1);
        const QString found = ctx.blockHref->value(id);
        return found;
    }
    const QString lower = link.trimmed().toLower();
    if (lower.startsWith(QLatin1String("https://")) || lower.startsWith(QLatin1String("http://"))
        || lower.startsWith(QLatin1String("mailto:")))
        return link.trimmed();
    return {};
}

bool noteUsable(const Document &doc, const QString &noteId, const Block **out)
{
    const Block *note = doc.findBlock(noteId);
    if (!note || !isNote(*note) || omittedBlock(doc, *note))
        return false;
    if (out)
        *out = note;
    return true;
}

void writeRuns(XhtmlWriter &x, WriteCtx &ctx, const Block &host, const QVector<TextRun> &runs)
{
    const QString hostLang = blockLang(host, ctx.docLang);
    const QString hostDir = resolvedDir(*ctx.doc, host);
    for (const auto &r : runs) {
        if (runIsSuppressed(*ctx.doc, r))
            continue;
        const QString shown = runDisplayText(*ctx.doc, r, 1, 1, &r);
        if (!r.noteId.isEmpty()) {
            const Block *note = nullptr;
            if (noteUsable(*ctx.doc, r.noteId, &note) && ctx.noteChapter->contains(r.noteId)) {
                const int owner = ctx.noteChapter->value(r.noteId);
                if (ctx.noteOrder && !ctx.noteOrder->contains(r.noteId))
                    ctx.noteOrder->push_back(r.noteId);
                QVector<NoteUse> &uses = (*ctx.noteUses)[r.noteId];
                NoteUse use;
                use.chapter = ctx.chapterIndex;
                use.refId = QStringLiteral("nr-%1-%2").arg(xmlIdFor(r.noteId, ctx.xmlIds)).arg(uses.size() + 1);
                use.label = shown.isEmpty() ? QString::number(uses.size() + 1) : shown;
                uses.push_back(use);
                const QString href = owner == ctx.chapterIndex
                    ? QStringLiteral("#") + xmlIdFor(r.noteId, ctx.xmlIds)
                    : ctx.chapters->at(owner).href + QStringLiteral("#") + xmlIdFor(r.noteId, ctx.xmlIds);
                const QString label = use.label;
                x.start(QStringLiteral("a"),
                        {{QStringLiteral("id"), use.refId},
                         {QStringLiteral("href"), href},
                         {QStringLiteral("epub:type"), QStringLiteral("noteref")},
                         {QStringLiteral("role"), QStringLiteral("doc-noteref")},
                         {QStringLiteral("class"), QStringLiteral("noteref")}});
                x.text(label);
                x.end(QStringLiteral("a"));
                continue;
            }
        }
        if (r.field == QLatin1String("page_number") || r.field == QLatin1String("page_count")) {
            warnOnce(ctx, QStringLiteral("EPUB export: %1 in %2 left empty (EPUB has no pages)").arg(r.field, host.id));
            continue;
        }
        if (r.field == QLatin1String("xref") && r.xrefKind == QLatin1String("page")) {
            warnOnce(ctx, QStringLiteral("EPUB export: xref page %1 in %2 replaced by a link to the target text")
                             .arg(r.fieldArg, host.id));
            const QString href = xrefPageHref(ctx, r.fieldArg);
            const QString label = xrefPageLabel(*ctx.doc, r.fieldArg);
            if (!href.isEmpty())
                x.start(QStringLiteral("a"), {{QStringLiteral("href"), href}});
            x.text(label.isEmpty() ? r.fieldArg : label);
            if (!href.isEmpty())
                x.end(QStringLiteral("a"));
            continue;
        }
        if (shown.isEmpty() && r.ruby.isEmpty())
            continue;
        const QString href = r.link.isEmpty() ? QString() : resolveHref(ctx, r.link);
        const QString style = runInlineStyle(r);
        const QString charCls = classFor(ctx, r.charStyle);
        const QString runLang = r.lang.trimmed().isEmpty() ? QString() : usableLang(r.lang, QString());
        const bool langDiff = !runLang.isEmpty() && runLang.compare(hostLang, Qt::CaseInsensitive) != 0;
        QString runDir;
        if (langDiff) {
            const QString implied = langIsRtl(runLang) ? QStringLiteral("rtl") : QStringLiteral("ltr");
            if (implied != hostDir)
                runDir = implied;
        }
        if (!href.isEmpty())
            x.start(QStringLiteral("a"), {{QStringLiteral("href"), href}});
        Attrs span;
        if (!charCls.isEmpty())
            span.push_back({QStringLiteral("class"), charCls});
        if (!style.isEmpty())
            span.push_back({QStringLiteral("style"), style});
        if (langDiff)
            addLangDir(span, runLang, runDir);
        else if (!runDir.isEmpty())
            span.push_back({QStringLiteral("dir"), runDir});
        if (!span.isEmpty())
            x.start(QStringLiteral("span"), span);
        if (r.bold)
            x.start(QStringLiteral("strong"));
        if (r.italic)
            x.start(QStringLiteral("em"));
        if (r.underline)
            x.start(QStringLiteral("u"));
        if (r.strike)
            x.start(QStringLiteral("s"));
        if (r.superscript)
            x.start(QStringLiteral("sup"));
        if (r.subscript)
            x.start(QStringLiteral("sub"));
        if (!r.ruby.isEmpty()) {
            x.start(QStringLiteral("ruby"));
            writeTextBreaks(x, shown);
            x.start(QStringLiteral("rt"));
            x.text(r.ruby);
            x.end(QStringLiteral("rt"));
            x.end(QStringLiteral("ruby"));
        } else {
            writeTextBreaks(x, shown);
        }
        if (r.subscript)
            x.end(QStringLiteral("sub"));
        if (r.superscript)
            x.end(QStringLiteral("sup"));
        if (r.strike)
            x.end(QStringLiteral("s"));
        if (r.underline)
            x.end(QStringLiteral("u"));
        if (r.italic)
            x.end(QStringLiteral("em"));
        if (r.bold)
            x.end(QStringLiteral("strong"));
        if (!span.isEmpty())
            x.end(QStringLiteral("span"));
        if (!href.isEmpty())
            x.end(QStringLiteral("a"));
    }
}

Attrs blockAttrs(const Block &b, const WriteCtx &ctx, bool withId)
{
    Attrs attrs;
    if (withId && !b.id.isEmpty())
        attrs.push_back({QStringLiteral("id"), xmlIdFor(b.id, ctx.xmlIds)});
    addLangDir(attrs, blockLang(b, ctx.docLang), resolvedDir(*ctx.doc, b));
    const QString cls = classFor(ctx, b.style);
    if (!cls.isEmpty())
        attrs.push_back({QStringLiteral("class"), cls});
    const QString style = blockInlineStyle(b);
    if (!style.isEmpty())
        attrs.push_back({QStringLiteral("style"), style});
    return attrs;
}

QString altFor(const Block &b)
{
    if (!b.altText.trimmed().isEmpty())
        return b.altText.trimmed();
    if (!b.caption.trimmed().isEmpty())
        return b.caption.trimmed();
    if (b.type == QLatin1String("qr")) {
        const QString payload = b.qrText.trimmed();
        if (payload.isEmpty())
            return QStringLiteral("QR code");
        return QStringLiteral("QR code: ") + payload.left(240);
    }
    if (b.type == QLatin1String("chart"))
        return QStringLiteral("Chart");
    if (b.type == QLatin1String("shape"))
        return b.shapeKind.trimmed().isEmpty() ? QStringLiteral("Shape") : b.shapeKind.trimmed();
    return QStringLiteral("Image");
}

bool sniffImage(const QByteArray &in, QByteArray *out, QString *mime, QString *ext, QString *error, const QString &id)
{
    const auto fail = [&](const QString &msg) {
        if (error)
            *error = msg + id;
        return false;
    };
    if (in.size() > kMaxImageBytes)
        return fail(QStringLiteral("EPUB image exceeds 32MB: "));
    if (in.size() >= 8 && static_cast<unsigned char>(in[0]) == 0x89 && in[1] == 'P' && in[2] == 'N' && in[3] == 'G') {
        *out = in;
        *mime = QStringLiteral("image/png");
        *ext = QStringLiteral("png");
        return true;
    }
    if (in.size() >= 3 && static_cast<unsigned char>(in[0]) == 0xFF && static_cast<unsigned char>(in[1]) == 0xD8
        && static_cast<unsigned char>(in[2]) == 0xFF) {
        *out = in;
        *mime = QStringLiteral("image/jpeg");
        *ext = QStringLiteral("jpg");
        return true;
    }
    if (in.startsWith("GIF87a") || in.startsWith("GIF89a")) {
        *out = in;
        *mime = QStringLiteral("image/gif");
        *ext = QStringLiteral("gif");
        return true;
    }
    QImage img;
    if (!img.loadFromData(in) || img.isNull())
        return fail(QStringLiteral("EPUB image is not a supported bitmap: "));
    QByteArray png;
    QBuffer buf(&png);
    if (!buf.open(QIODevice::WriteOnly) || !img.save(&buf, "PNG") || png.isEmpty())
        return fail(QStringLiteral("EPUB image could not be encoded as PNG: "));
    *out = png;
    *mime = QStringLiteral("image/png");
    *ext = QStringLiteral("png");
    return true;
}

bool loadImageBytes(const Block &b, QByteArray *out, QString *error)
{
    if (!b.imageBytes.isEmpty()) {
        if (b.imageBytes.size() > kMaxImageBytes) {
            if (error)
                *error = QStringLiteral("EPUB image exceeds 32MB: ") + b.id;
            return false;
        }
        *out = b.imageBytes;
        return true;
    }
    if (b.imagePath.isEmpty()) {
        if (error)
            *error = QStringLiteral("EPUB image has no data: ") + b.id;
        return false;
    }
    const QFileInfo fi(b.imagePath);
    if (!fi.isFile()) {
        if (error)
            *error = QStringLiteral("EPUB image is not a regular file: ") + b.imagePath;
        return false;
    }
    if (fi.size() > kMaxImageBytes) {
        if (error)
            *error = QStringLiteral("EPUB image exceeds 32MB: ") + b.id;
        return false;
    }
    QFile f(b.imagePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot read image ") + b.imagePath;
        return false;
    }
    const QByteArray bytes = f.read(kMaxImageBytes + 1);
    if (bytes.size() > kMaxImageBytes) {
        if (error)
            *error = QStringLiteral("EPUB image exceeds 32MB: ") + b.id;
        return false;
    }
    if (bytes.isEmpty()) {
        if (error)
            *error = QStringLiteral("EPUB image has no data: ") + b.id;
        return false;
    }
    *out = bytes;
    return true;
}

bool imageNeedsRaster(const Block &b)
{
    return b.cropL > 0 || b.cropT > 0 || b.cropR > 0 || b.cropB > 0 || b.rotateDeg != 0 || b.flipH || b.flipV
        || b.opacity < 0.999 || b.shadowPt > 0;
}

bool addAsset(WriteCtx &ctx, const Block &b, const QByteArray &bytes, const QString &mime, const QString &ext, QString *hrefOut)
{
    Asset a;
    const QString base = xmlIdFor(b.id.isEmpty() ? QStringLiteral("img") : b.id, ctx.xmlIds);
    QString href = QStringLiteral("images/") + base + QLatin1Char('.') + ext;
    QString mid = QStringLiteral("img-") + base;
    QSet<QString> hrefs;
    QSet<QString> ids;
    for (const auto &have : *ctx.assets) {
        hrefs.insert(have.href);
        ids.insert(have.manifestId);
    }
    int n = 2;
    while (hrefs.contains(href) || ids.contains(mid)) {
        href = QStringLiteral("images/") + base + QLatin1Char('-') + QString::number(n) + QLatin1Char('.') + ext;
        mid = QStringLiteral("img-") + base + QLatin1Char('-') + QString::number(n);
        ++n;
    }
    a.href = href;
    a.manifestId = mid;
    a.mime = mime;
    a.bytes = bytes;
    ctx.assets->push_back(a);
    *hrefOut = href;
    return true;
}

bool writeFigure(XhtmlWriter &x, WriteCtx &ctx, const Block &b, bool raster)
{
    QByteArray bytes;
    QString mime;
    QString ext;
    if (raster) {
        bytes = renderBlockPng(*ctx.doc, b);
        if (bytes.isEmpty()) {
            if (ctx.error)
                *ctx.error = QStringLiteral("EPUB raster failed for ") + b.id;
            return false;
        }
        mime = QStringLiteral("image/png");
        ext = QStringLiteral("png");
    } else {
        QByteArray raw;
        if (!loadImageBytes(b, &raw, ctx.error))
            return false;
        if (!sniffImage(raw, &bytes, &mime, &ext, ctx.error, b.id))
            return false;
    }
    QString href;
    addAsset(ctx, b, bytes, mime, ext, &href);
    x.start(QStringLiteral("figure"), {{QStringLiteral("id"), xmlIdFor(b.id, ctx.xmlIds)}});
    x.nl();
    x.empty(QStringLiteral("img"), {{QStringLiteral("src"), href}, {QStringLiteral("alt"), altFor(b)}});
    x.nl();
    if (blockHasCaption(b)) {
        x.start(QStringLiteral("figcaption"));
        x.text(captionDisplayText(*ctx.doc, b, 1, 1));
        x.end(QStringLiteral("figcaption"));
        x.nl();
    }
    x.end(QStringLiteral("figure"));
    x.nl();
    return x.error().isEmpty();
}

void writeTable(XhtmlWriter &x, WriteCtx &ctx, const Block &b);

void writeNested(XhtmlWriter &x, WriteCtx &ctx, const Block &host, const QVector<QVector<NestedCell>> &nested)
{
    x.start(QStringLiteral("table"));
    for (const auto &row : nested) {
        x.start(QStringLiteral("tr"));
        for (const auto &cell : row) {
            x.start(QStringLiteral("td"));
            writeRuns(x, ctx, host, cell.runs);
            x.end(QStringLiteral("td"));
        }
        x.end(QStringLiteral("tr"));
    }
    x.end(QStringLiteral("table"));
}

void writeTable(XhtmlWriter &x, WriteCtx &ctx, const Block &b)
{
    x.start(QStringLiteral("table"), blockAttrs(b, ctx, true));
    x.nl();
    const int rows = b.table.size();
    int cols = 0;
    for (const auto &row : b.table)
        cols = qMax(cols, row.size());
    QVector<QVector<char>> cover(rows, QVector<char>(cols, 0));
    bool bodyOpen = false;
    if (b.headerRow && rows > 0)
        x.start(QStringLiteral("thead"));
    else if (rows > 0) {
        x.start(QStringLiteral("tbody"));
        bodyOpen = true;
    }
    for (int r = 0; r < rows; ++r) {
        if (b.headerRow && r == 1) {
            x.end(QStringLiteral("thead"));
            x.nl();
            x.start(QStringLiteral("tbody"));
            bodyOpen = true;
        }
        x.start(QStringLiteral("tr"));
        for (int c = 0; c < b.table[r].size(); ++c) {
            if (c < cols && cover[r][c])
                continue;
            const TableCell &cell = b.table[r][c];
            const int cs = qMax(1, cell.colSpan);
            const int rs = qMax(1, cell.rowSpan);
            for (int rr = r; rr < qMin(rows, r + rs); ++rr) {
                for (int cc = c; cc < qMin(cols, c + cs); ++cc) {
                    if (rr != r || cc != c)
                        cover[rr][cc] = 1;
                }
            }
            const bool header = b.headerRow && r == 0;
            const QString tag = header ? QStringLiteral("th") : QStringLiteral("td");
            Attrs attrs;
            if (header)
                attrs.push_back({QStringLiteral("scope"), QStringLiteral("col")});
            if (cs > 1)
                attrs.push_back({QStringLiteral("colspan"), QString::number(cs)});
            if (rs > 1)
                attrs.push_back({QStringLiteral("rowspan"), QString::number(rs)});
            x.start(tag, attrs);
            writeRuns(x, ctx, b, cell.runs);
            if (!cell.nested.isEmpty())
                writeNested(x, ctx, b, cell.nested);
            x.end(tag);
        }
        x.end(QStringLiteral("tr"));
        x.nl();
    }
    if (b.headerRow && rows == 1)
        x.end(QStringLiteral("thead"));
    else if (bodyOpen)
        x.end(QStringLiteral("tbody"));
    x.nl();
    x.end(QStringLiteral("table"));
    x.nl();
}

void writeList(XhtmlWriter &x, WriteCtx &ctx, const QVector<const Block *> &items, int &index, int level)
{
    const Block &first = *items.at(index);
    const QString tag = listTag(first);
    Attrs listAttrs;
    if (first.listKind == QLatin1String("checkbox") || first.listKind == QLatin1String("task"))
        listAttrs.push_back({QStringLiteral("class"), QStringLiteral("checklist")});
    x.start(tag, listAttrs);
    x.nl();
    bool any = false;
    while (index < items.size()) {
        const Block &b = *items.at(index);
        const int lv = listLevel(b);
        if (lv < level)
            break;
        if (lv == level && listTag(b) != tag)
            break;
        if (lv == level && b.listRestart && any)
            break;
        if (lv > level) {
            x.start(QStringLiteral("li"));
            writeList(x, ctx, items, index, lv);
            x.end(QStringLiteral("li"));
            x.nl();
            continue;
        }
        any = true;
        Attrs liAttrs = blockAttrs(b, ctx, true);
        if (!b.listBullet.isEmpty())
            appendStyle(liAttrs, cssQuotedMarker(b.listBullet));
        x.start(QStringLiteral("li"), liAttrs);
        if (b.listKind == QLatin1String("checkbox") || b.listKind == QLatin1String("task"))
            x.text(b.checked ? QStringLiteral("☑ ") : QStringLiteral("☐ "));
        writeRuns(x, ctx, b, b.runs);
        ++index;
        while (index < items.size() && listLevel(*items.at(index)) > level)
            writeList(x, ctx, items, index, listLevel(*items.at(index)));
        x.end(QStringLiteral("li"));
        x.nl();
    }
    x.end(tag);
    x.nl();
}

void writeTocBlock(XhtmlWriter &x, WriteCtx &ctx, const Block &b)
{
    x.start(QStringLiteral("nav"),
            {{QStringLiteral("id"), xmlIdFor(b.id, ctx.xmlIds)}, {QStringLiteral("epub:type"), QStringLiteral("toc")}});
    x.nl();
    x.start(QStringLiteral("ol"));
    x.nl();
    for (const auto &r : b.runs) {
        QString label = runDisplayText(*ctx.doc, r, 1, 1, &r).trimmed();
        if (label.isEmpty())
            continue;
        const QString href = r.link.isEmpty() ? QString() : resolveHref(ctx, r.link);
        x.start(QStringLiteral("li"));
        if (!href.isEmpty())
            x.start(QStringLiteral("a"), {{QStringLiteral("href"), href}});
        x.text(label);
        if (!href.isEmpty())
            x.end(QStringLiteral("a"));
        x.end(QStringLiteral("li"));
        x.nl();
    }
    x.end(QStringLiteral("ol"));
    x.nl();
    x.end(QStringLiteral("nav"));
    x.nl();
}

bool writeBlock(XhtmlWriter &x, WriteCtx &ctx, const Block &b)
{
    if (b.type == QLatin1String("heading")) {
        const QString tag = QStringLiteral("h%1").arg(headingLevel(b));
        x.start(tag, blockAttrs(b, ctx, true));
        writeRuns(x, ctx, b, b.runs);
        x.end(tag);
        x.nl();
    } else if (b.type == QLatin1String("table")) {
        if (!b.table.isEmpty())
            writeTable(x, ctx, b);
    } else if (b.type == QLatin1String("image")) {
        if (!writeFigure(x, ctx, b, imageNeedsRaster(b)))
            return false;
    } else if (b.type == QLatin1String("shape") || b.type == QLatin1String("chart") || b.type == QLatin1String("qr")) {
        if (!writeFigure(x, ctx, b, true))
            return false;
    } else if (b.type == QLatin1String("list-item")) {
        x.start(QStringLiteral("p"), blockAttrs(b, ctx, true));
        writeRuns(x, ctx, b, b.runs);
        x.end(QStringLiteral("p"));
        x.nl();
    } else if (b.type == QLatin1String("toc")) {
        writeTocBlock(x, ctx, b);
    } else if (b.type == QLatin1String("frame")) {
        x.start(QStringLiteral("blockquote"), blockAttrs(b, ctx, true));
        writeRuns(x, ctx, b, b.runs);
        x.end(QStringLiteral("blockquote"));
        x.nl();
    } else if (b.type == QLatin1String("equation") || b.type == QLatin1String("form")) {
        x.start(QStringLiteral("p"), blockAttrs(b, ctx, true));
        if (b.type == QLatin1String("form") && b.formKind == QLatin1String("checkbox"))
            x.text(b.checked ? QStringLiteral("☑ ") : QStringLiteral("☐ "));
        writeRuns(x, ctx, b, b.runs);
        if (b.type == QLatin1String("equation") && b.numbered)
            x.text(QStringLiteral(" (%1)").arg(b.level > 0 ? b.level : 1));
        x.end(QStringLiteral("p"));
        x.nl();
    } else if (b.type != QLatin1String("break") && !isNote(b)) {
        x.start(QStringLiteral("p"), blockAttrs(b, ctx, true));
        writeRuns(x, ctx, b, b.runs);
        x.end(QStringLiteral("p"));
        x.nl();
    }
    return x.error().isEmpty();
}

void writeNotes(XhtmlWriter &x, WriteCtx &ctx)
{
    if (!ctx.noteOrder || !ctx.noteUses)
        return;
    // Orphans follow in-flow notes, in body order, and only in the last chapter.
    // The index loop still grows: a note body may reference another note.
    const bool lastChapter = ctx.chapters && ctx.chapterIndex + 1 == ctx.chapters->size();
    if (lastChapter && ctx.orphans) {
        for (const QString &id : *ctx.orphans) {
            if (!ctx.noteOrder->contains(id))
                ctx.noteOrder->push_back(id);
        }
    }
    QSet<QString> written;
    // Index loop plus a copied use list: writeRuns may insert a note that this
    // chapter's text mentions. Mutating the hash under a QHash iterator is UB,
    // and QHash order is not the order of first reference.
    for (int i = 0; i < ctx.noteOrder->size(); ++i) {
        const QString id = ctx.noteOrder->at(i);
        if (written.contains(id))
            continue;
        const int owner = ctx.noteChapter ? ctx.noteChapter->value(id, -1) : -1;
        if (owner != ctx.chapterIndex)
            continue;
        const QVector<NoteUse> uses = ctx.noteUses->value(id);
        bool owned = false;
        QString backId;
        QString backLabel;
        for (const auto &use : uses) {
            if (use.chapter == ctx.chapterIndex) {
                owned = true;
                if (backId.isEmpty()) {
                    backId = use.refId;
                    backLabel = use.label;
                }
            }
        }
        const bool orphan = ctx.orphans && ctx.orphans->contains(id);
        if (!owned && !orphan)
            continue;
        const Block *note = nullptr;
        if (!noteUsable(*ctx.doc, id, &note))
            continue;
        written.insert(id);
        if (orphan)
            warnOnce(ctx, QStringLiteral("EPUB export: note %1 has no reference").arg(id));
        const bool endnote = note->type == QLatin1String("endnote");
        Attrs attrs = {{QStringLiteral("id"), xmlIdFor(note->id, ctx.xmlIds)},
                       {QStringLiteral("epub:type"), endnote ? QStringLiteral("endnote") : QStringLiteral("footnote")},
                       // doc-endnote is not in the EPUB 3.3 role set (epubcheck RSC-005/RSC-017).
                       {QStringLiteral("role"), endnote ? QStringLiteral("note") : QStringLiteral("doc-footnote")},
                       {QStringLiteral("class"), endnote ? QStringLiteral("endnote") : QStringLiteral("footnote")}};
        addLangDir(attrs, blockLang(*note, ctx.docLang), resolvedDir(*ctx.doc, *note));
        x.start(QStringLiteral("aside"), attrs);
        x.nl();
        x.start(QStringLiteral("p"));
        if (!backId.isEmpty()) {
            x.start(QStringLiteral("a"),
                    {{QStringLiteral("href"), QStringLiteral("#") + backId},
                     {QStringLiteral("epub:type"), QStringLiteral("backlink")},
                     {QStringLiteral("role"), QStringLiteral("doc-backlink")}});
            x.text(backLabel.isEmpty() ? QStringLiteral("1") : backLabel);
            x.end(QStringLiteral("a"));
            x.text(QStringLiteral(" "));
        }
        writeRuns(x, ctx, *note, note->runs);
        x.end(QStringLiteral("p"));
        x.nl();
        x.end(QStringLiteral("aside"));
        x.nl();
    }
}

bool writeChapter(const Document &doc, const Chapter &chapter, int chapterIndex, WriteCtx ctx, QString *xhtml, QString *error)
{
    XhtmlWriter x;
    x.startDocument();
    x.doctypeHtml();
    Attrs html = {{QStringLiteral("xmlns"), QStringLiteral("http://www.w3.org/1999/xhtml")},
                  {QStringLiteral("xmlns:epub"), QStringLiteral("http://www.idpf.org/2007/ops")}};
    addLangDir(html, ctx.docLang, QString());
    x.start(QStringLiteral("html"), html);
    x.nl();
    x.start(QStringLiteral("head"));
    x.nl();
    x.empty(QStringLiteral("meta"), {{QStringLiteral("charset"), QStringLiteral("utf-8")}});
    x.nl();
    x.start(QStringLiteral("title"));
    x.text(chapter.title);
    x.end(QStringLiteral("title"));
    x.nl();
    x.empty(QStringLiteral("link"),
            {{QStringLiteral("rel"), QStringLiteral("stylesheet")},
             {QStringLiteral("type"), QStringLiteral("text/css")},
             {QStringLiteral("href"), QStringLiteral("style.css")}});
    x.nl();
    x.end(QStringLiteral("head"));
    x.nl();
    x.start(QStringLiteral("body"),
            {{QStringLiteral("epub:type"), chapter.front ? QStringLiteral("frontmatter") : QStringLiteral("bodymatter")}});
    x.nl();
    if (chapter.indexes.isEmpty()) {
        x.start(QStringLiteral("p"));
        x.text(chapter.title);
        x.end(QStringLiteral("p"));
        x.nl();
    }
    ctx.chapterIndex = chapterIndex;
    ctx.chapterHref = chapter.href;
    QVector<const Block *> listRun;
    auto flushList = [&]() {
        int i = 0;
        while (i < listRun.size())
            writeList(x, ctx, listRun, i, listLevel(*listRun.at(i)));
        listRun.clear();
    };
    for (int idx : chapter.indexes) {
        const Block &b = doc.body.at(idx);
        if (b.type == QLatin1String("list-item")) {
            listRun.push_back(&b);
            continue;
        }
        flushList();
        if (!writeBlock(x, ctx, b)) {
            if (error)
                *error = ctx.error && !ctx.error->isEmpty() ? *ctx.error : x.error();
            return false;
        }
    }
    flushList();
    writeNotes(x, ctx);
    x.end(QStringLiteral("body"));
    x.nl();
    x.end(QStringLiteral("html"));
    x.nl();
    if (!x.ok()) {
        if (error)
            *error = x.error().isEmpty() ? QStringLiteral("EPUB XHTML is not well-formed") : x.error();
        return false;
    }
    *xhtml = x.result();
    return true;
}

// Clamp each heading to at most previous+1, starting from the shallowest level,
// so the TOC never drops a later shallower heading or nests ol directly in ol.
void normalizeTocLevels(QVector<HeadingRef> &heads)
{
    if (heads.isEmpty())
        return;
    int minLevel = heads.first().level;
    for (const auto &h : heads)
        minLevel = qMin(minLevel, h.level);
    int prev = minLevel - 1;
    for (auto &h : heads) {
        if (h.level > prev + 1)
            h.level = prev + 1;
        if (h.level < 1)
            h.level = 1;
        prev = h.level;
    }
}

void writeTocOl(XhtmlWriter &x, const QVector<HeadingRef> &heads, int &i, int level)
{
    x.start(QStringLiteral("ol"));
    x.nl();
    while (i < heads.size()) {
        if (heads.at(i).level < level)
            break;
        if (heads.at(i).level > level) {
            writeTocOl(x, heads, i, heads.at(i).level);
            continue;
        }
        x.start(QStringLiteral("li"));
        x.start(QStringLiteral("a"), {{QStringLiteral("href"), heads.at(i).href}});
        x.text(heads.at(i).title);
        x.end(QStringLiteral("a"));
        const int mine = heads.at(i).level;
        ++i;
        if (i < heads.size() && heads.at(i).level > mine)
            writeTocOl(x, heads, i, heads.at(i).level);
        x.end(QStringLiteral("li"));
        x.nl();
    }
    x.end(QStringLiteral("ol"));
    x.nl();
}

QString writeNav(const Document &doc, const QString &docLang, const QVector<Chapter> &chapters, const QVector<HeadingRef> &heads,
                 QString *error)
{
    XhtmlWriter x;
    x.startDocument();
    x.doctypeHtml();
    Attrs html = {{QStringLiteral("xmlns"), QStringLiteral("http://www.w3.org/1999/xhtml")},
                  {QStringLiteral("xmlns:epub"), QStringLiteral("http://www.idpf.org/2007/ops")}};
    addLangDir(html, docLang, QString());
    x.start(QStringLiteral("html"), html);
    x.nl();
    x.start(QStringLiteral("head"));
    x.nl();
    x.empty(QStringLiteral("meta"), {{QStringLiteral("charset"), QStringLiteral("utf-8")}});
    x.nl();
    x.start(QStringLiteral("title"));
    x.text(doc.title.isEmpty() ? QStringLiteral("Sumário") : doc.title);
    x.end(QStringLiteral("title"));
    x.nl();
    x.empty(QStringLiteral("link"),
            {{QStringLiteral("rel"), QStringLiteral("stylesheet")},
             {QStringLiteral("type"), QStringLiteral("text/css")},
             {QStringLiteral("href"), QStringLiteral("style.css")}});
    x.nl();
    x.end(QStringLiteral("head"));
    x.nl();
    x.start(QStringLiteral("body"));
    x.nl();
    x.start(QStringLiteral("nav"), {{QStringLiteral("epub:type"), QStringLiteral("toc")}, {QStringLiteral("id"), QStringLiteral("toc")}});
    x.nl();
    x.start(QStringLiteral("h1"));
    x.text(QStringLiteral("Sumário"));
    x.end(QStringLiteral("h1"));
    x.nl();
    if (heads.isEmpty()) {
        x.start(QStringLiteral("ol"));
        x.nl();
        x.start(QStringLiteral("li"));
        x.start(QStringLiteral("a"), {{QStringLiteral("href"), chapters.isEmpty() ? QStringLiteral("c1.xhtml") : chapters.first().href}});
        x.text(doc.title.isEmpty() ? QStringLiteral("Sem título") : doc.title);
        x.end(QStringLiteral("a"));
        x.end(QStringLiteral("li"));
        x.nl();
        x.end(QStringLiteral("ol"));
        x.nl();
    } else {
        QVector<HeadingRef> tocHeads = heads;
        normalizeTocLevels(tocHeads);
        int i = 0;
        writeTocOl(x, tocHeads, i, tocHeads.first().level);
    }
    x.end(QStringLiteral("nav"));
    x.nl();
    QString bodyHref = chapters.isEmpty() ? QStringLiteral("c1.xhtml") : chapters.first().href;
    for (const auto &c : chapters) {
        if (!c.front) {
            bodyHref = c.href;
            break;
        }
    }
    x.start(QStringLiteral("nav"),
            {{QStringLiteral("epub:type"), QStringLiteral("landmarks")}, {QStringLiteral("hidden"), QStringLiteral("hidden")}});
    x.nl();
    x.start(QStringLiteral("ol"));
    x.nl();
    x.start(QStringLiteral("li"));
    x.start(QStringLiteral("a"), {{QStringLiteral("epub:type"), QStringLiteral("bodymatter")}, {QStringLiteral("href"), bodyHref}});
    x.text(QStringLiteral("Início"));
    x.end(QStringLiteral("a"));
    x.end(QStringLiteral("li"));
    x.nl();
    x.start(QStringLiteral("li"));
    x.start(QStringLiteral("a"),
            {{QStringLiteral("epub:type"), QStringLiteral("toc")}, {QStringLiteral("href"), QStringLiteral("nav.xhtml#toc")}});
    x.text(QStringLiteral("Sumário"));
    x.end(QStringLiteral("a"));
    x.end(QStringLiteral("li"));
    x.nl();
    x.end(QStringLiteral("ol"));
    x.nl();
    x.end(QStringLiteral("nav"));
    x.nl();
    x.end(QStringLiteral("body"));
    x.nl();
    x.end(QStringLiteral("html"));
    x.nl();
    if (!x.ok()) {
        if (error)
            *error = x.error().isEmpty() ? QStringLiteral("EPUB nav is not well-formed") : x.error();
        return {};
    }
    return x.result();
}

void collectLangs(const Document &doc, const QString &primary, QStringList *out)
{
    QSet<QString> seen;
    auto add = [&](const QString &raw) {
        const QString lang = usableLang(raw, QString());
        if (lang.isEmpty() || seen.contains(lang.toLower()))
            return;
        seen.insert(lang.toLower());
        out->push_back(lang);
    };
    add(primary);
    for (const auto &b : doc.body) {
        if (omittedBlock(doc, b))
            continue;
        add(b.lang);
        for (const auto &r : b.runs)
            add(r.lang);
        for (const auto &row : b.table)
            for (const auto &cell : row)
                for (const auto &r : cell.runs)
                    add(r.lang);
    }
}

QString writeOpf(const Document &doc, const QString &docLang, const QString &urn, const QVector<Chapter> &chapters,
                 const QVector<Asset> &assets, bool hasFigures, bool hasHeadings, QString *error)
{
    const QString modified = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'"));
    const QString title = doc.title.trimmed().isEmpty() ? QStringLiteral("Sem título") : doc.title.trimmed();
    XhtmlWriter x;
    x.startDocument();
    x.start(QStringLiteral("package"),
            {{QStringLiteral("xmlns"), QStringLiteral("http://www.idpf.org/2007/opf")},
             {QStringLiteral("version"), QStringLiteral("3.0")},
             {QStringLiteral("unique-identifier"), QStringLiteral("pub-id")},
             {QStringLiteral("xml:lang"), docLang}});
    x.nl();
    x.start(QStringLiteral("metadata"), {{QStringLiteral("xmlns:dc"), QStringLiteral("http://purl.org/dc/elements/1.1/")}});
    x.nl();
    x.start(QStringLiteral("dc:identifier"), {{QStringLiteral("id"), QStringLiteral("pub-id")}});
    x.text(urn);
    x.end(QStringLiteral("dc:identifier"));
    x.nl();
    x.start(QStringLiteral("dc:title"), {{QStringLiteral("xml:lang"), docLang}});
    x.text(title);
    x.end(QStringLiteral("dc:title"));
    x.nl();
    QStringList langs;
    collectLangs(doc, docLang, &langs);
    if (langs.isEmpty())
        langs.push_back(docLang);
    for (const auto &lang : langs) {
        x.start(QStringLiteral("dc:language"));
        x.text(lang);
        x.end(QStringLiteral("dc:language"));
        x.nl();
    }
    if (!doc.author.trimmed().isEmpty()) {
        x.start(QStringLiteral("dc:creator"), {{QStringLiteral("xml:lang"), docLang}});
        x.text(doc.author.trimmed());
        x.end(QStringLiteral("dc:creator"));
        x.nl();
    }
    x.start(QStringLiteral("meta"), {{QStringLiteral("property"), QStringLiteral("dcterms:modified")}});
    x.text(modified);
    x.end(QStringLiteral("meta"));
    x.nl();
    auto meta = [&](const QString &property, const QString &value) {
        x.start(QStringLiteral("meta"), {{QStringLiteral("property"), property}});
        x.text(value);
        x.end(QStringLiteral("meta"));
        x.nl();
    };
    meta(QStringLiteral("schema:accessMode"), QStringLiteral("textual"));
    if (hasFigures)
        meta(QStringLiteral("schema:accessMode"), QStringLiteral("visual"));
    if (hasHeadings)
        meta(QStringLiteral("schema:accessibilityFeature"), QStringLiteral("structuralNavigation"));
    meta(QStringLiteral("schema:accessibilityFeature"), QStringLiteral("tableOfContents"));
    if (hasFigures)
        meta(QStringLiteral("schema:accessibilityFeature"), QStringLiteral("alternativeText"));
    meta(QStringLiteral("schema:accessibilityFeature"), QStringLiteral("readingOrder"));
    meta(QStringLiteral("schema:accessibilityHazard"), QStringLiteral("noFlashingHazard"));
    meta(QStringLiteral("schema:accessibilityHazard"), QStringLiteral("noMotionSimulationHazard"));
    meta(QStringLiteral("schema:accessibilityHazard"), QStringLiteral("noSoundHazard"));
    meta(QStringLiteral("schema:accessibilitySummary"),
         QStringLiteral("Publicação com títulos, tabelas, listas, notas e texto alternativo nas figuras."));
    x.end(QStringLiteral("metadata"));
    x.nl();
    x.start(QStringLiteral("manifest"));
    x.nl();
    x.empty(QStringLiteral("item"),
            {{QStringLiteral("id"), QStringLiteral("nav")},
             {QStringLiteral("href"), QStringLiteral("nav.xhtml")},
             {QStringLiteral("media-type"), QStringLiteral("application/xhtml+xml")},
             {QStringLiteral("properties"), QStringLiteral("nav")}});
    x.nl();
    x.empty(QStringLiteral("item"),
            {{QStringLiteral("id"), QStringLiteral("css")},
             {QStringLiteral("href"), QStringLiteral("style.css")},
             {QStringLiteral("media-type"), QStringLiteral("text/css")}});
    x.nl();
    for (const auto &c : chapters) {
        x.empty(QStringLiteral("item"),
                {{QStringLiteral("id"), c.manifestId},
                 {QStringLiteral("href"), c.href},
                 {QStringLiteral("media-type"), QStringLiteral("application/xhtml+xml")}});
        x.nl();
    }
    for (const auto &a : assets) {
        x.empty(QStringLiteral("item"),
                {{QStringLiteral("id"), a.manifestId},
                 {QStringLiteral("href"), a.href},
                 {QStringLiteral("media-type"), a.mime}});
        x.nl();
    }
    x.end(QStringLiteral("manifest"));
    x.nl();
    x.start(QStringLiteral("spine"));
    x.nl();
    // Landmarks link at nav.xhtml. epubcheck RSC-011 requires that target in the spine.
    x.empty(QStringLiteral("itemref"),
            {{QStringLiteral("idref"), QStringLiteral("nav")}, {QStringLiteral("linear"), QStringLiteral("no")}});
    x.nl();
    for (const auto &c : chapters) {
        x.empty(QStringLiteral("itemref"), {{QStringLiteral("idref"), c.manifestId}});
        x.nl();
    }
    x.end(QStringLiteral("spine"));
    x.nl();
    x.end(QStringLiteral("package"));
    x.nl();
    if (!x.ok()) {
        if (error)
            *error = x.error().isEmpty() ? QStringLiteral("EPUB package is not well-formed") : x.error();
        return {};
    }
    return x.result();
}

QString writeContainer()
{
    XhtmlWriter x;
    x.startDocument();
    x.start(QStringLiteral("container"),
            {{QStringLiteral("version"), QStringLiteral("1.0")},
             {QStringLiteral("xmlns"), QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:container")}});
    x.nl();
    x.start(QStringLiteral("rootfiles"));
    x.nl();
    x.empty(QStringLiteral("rootfile"),
            {{QStringLiteral("full-path"), QStringLiteral("OEBPS/content.opf")},
             {QStringLiteral("media-type"), QStringLiteral("application/oebps-package+xml")}});
    x.nl();
    x.end(QStringLiteral("rootfiles"));
    x.nl();
    x.end(QStringLiteral("container"));
    x.nl();
    return x.result();
}

void forEachRun(const Block &b, const std::function<void(const TextRun &)> &fn)
{
    for (const auto &r : b.runs)
        fn(r);
    for (const auto &r : b.captionRuns)
        fn(r);
    for (const auto &row : b.table) {
        for (const auto &cell : row) {
            for (const auto &r : cell.runs)
                fn(r);
            for (const auto &nrow : cell.nested)
                for (const auto &nc : nrow)
                    for (const auto &r : nc.runs)
                        fn(r);
        }
    }
}

} // namespace

bool saveEpub(const Document &doc, const QString &path, QString *error, QJsonObject *info)
{
    const QString primaryRaw = rawDocumentLanguage(doc);
    const QString docLang = usableLang(primaryRaw, QStringLiteral("pt-PT"));
    const QString urn = publicationId(doc);

    QVector<Chapter> chapters;
    Chapter cur;
    cur.front = true;
    bool anyH1 = false;
    QVector<HeadingRef> heads;
    for (int i = 0; i < doc.body.size(); ++i) {
        const Block &b = doc.body.at(i);
        if (omittedBlock(doc, b) || b.type == QLatin1String("break") || isNote(b))
            continue;
        if (isChapterHeading(b)) {
            anyH1 = true;
            if (!cur.indexes.isEmpty())
                chapters.push_back(cur);
            cur = Chapter();
            cur.front = false;
            cur.title = blockPlain(doc, b).trimmed();
            if (cur.title.isEmpty())
                cur.title = doc.title.trimmed().isEmpty() ? QStringLiteral("Sem título") : doc.title.trimmed();
        }
        if (cur.indexes.isEmpty() && cur.title.isEmpty())
            cur.title = doc.title.trimmed().isEmpty() ? QStringLiteral("Sem título") : doc.title.trimmed();
        cur.indexes.push_back(i);
        if (b.type == QLatin1String("heading")) {
            HeadingRef h;
            h.level = headingLevel(b);
            h.title = blockPlain(doc, b).trimmed();
            if (h.title.isEmpty())
                h.title = b.id.isEmpty() ? QStringLiteral("Sem título") : b.id;
            h.href = b.id;
            heads.push_back(h);
        }
    }
    if (!cur.indexes.isEmpty())
        chapters.push_back(cur);
    if (chapters.isEmpty()) {
        Chapter only;
        only.front = false;
        only.title = doc.title.trimmed().isEmpty() ? QStringLiteral("Sem título") : doc.title.trimmed();
        chapters.push_back(only);
    }
    if (!anyH1)
        chapters.first().front = false;
    for (int i = 0; i < chapters.size(); ++i) {
        chapters[i].href = QStringLiteral("c%1.xhtml").arg(i + 1);
        chapters[i].manifestId = QStringLiteral("c%1").arg(i + 1);
    }

    QHash<QString, QString> xmlIds;
    for (const Block &b : doc.body) {
        if (!b.id.isEmpty())
            xmlIdFor(b.id, &xmlIds);
    }
    QHash<QString, QString> blockHref;
    QHash<QString, int> blockChapter;
    for (int ci = 0; ci < chapters.size(); ++ci) {
        for (int idx : chapters[ci].indexes) {
            const QString id = doc.body.at(idx).id;
            if (id.isEmpty())
                continue;
            blockHref.insert(id, chapters[ci].href + QStringLiteral("#") + xmlIdFor(id, &xmlIds));
            blockChapter.insert(id, ci);
        }
    }
    for (auto &h : heads) {
        const QString href = blockHref.value(h.href);
        if (!href.isEmpty())
            h.href = href;
    }

    QHash<QString, int> noteChapter;
    QSet<QString> inFlowNotes;
    QSet<QString> visibleNotes;
    for (int ci = 0; ci < chapters.size(); ++ci) {
        for (int idx : chapters[ci].indexes) {
            forEachRun(doc.body.at(idx), [&](const TextRun &r) {
                if (r.noteId.isEmpty() || !noteUsable(doc, r.noteId, nullptr))
                    return;
                inFlowNotes.insert(r.noteId);
                if (runIsSuppressed(doc, r) || visibleNotes.contains(r.noteId))
                    return;
                visibleNotes.insert(r.noteId);
                noteChapter.insert(r.noteId, ci);
            });
        }
    }
    QVector<QString> orphans;
    const int orphanChapter = chapters.size() - 1;
    for (const Block &b : doc.body) {
        if (!isNote(b) || b.id.isEmpty() || !noteUsable(doc, b.id, nullptr))
            continue;
        if (inFlowNotes.contains(b.id))
            continue;
        orphans.push_back(b.id);
        noteChapter.insert(b.id, orphanChapter);
    }

    QHash<QString, QString> styleClass;
    const QString css = stylesheet(doc, &styleClass);
    QStringList warnings;

    QString err;
    QVector<Asset> assets;
    QHash<QString, QVector<NoteUse>> noteUses;
    QVector<QString> noteOrder;
    QStringList chapterXml;
    chapterXml.reserve(chapters.size());
    for (int ci = 0; ci < chapters.size(); ++ci) {
        WriteCtx ctx;
        ctx.doc = &doc;
        ctx.docLang = docLang;
        ctx.blockHref = &blockHref;
        ctx.noteChapter = &noteChapter;
        ctx.chapters = &chapters;
        ctx.noteUses = &noteUses;
        ctx.noteOrder = &noteOrder;
        ctx.orphans = &orphans;
        ctx.assets = &assets;
        ctx.error = &err;
        ctx.xmlIds = &xmlIds;
        ctx.styleClass = &styleClass;
        ctx.warnings = &warnings;
        QString xhtml;
        if (!writeChapter(doc, chapters[ci], ci, ctx, &xhtml, &err)) {
            if (error)
                *error = err.isEmpty() ? QStringLiteral("EPUB chapter failed") : err;
            return false;
        }
        chapterXml.push_back(xhtml);
    }

    QString nav = writeNav(doc, docLang, chapters, heads, &err);
    if (nav.isEmpty()) {
        if (error)
            *error = err.isEmpty() ? QStringLiteral("EPUB nav failed") : err;
        return false;
    }
    const bool hasFigures = !assets.isEmpty();
    QString opf = writeOpf(doc, docLang, urn, chapters, assets, hasFigures, !heads.isEmpty(), &err);
    if (opf.isEmpty()) {
        if (error)
            *error = err.isEmpty() ? QStringLiteral("EPUB package failed") : err;
        return false;
    }
    const QString container = writeContainer();

    QVector<ZipEntry> entries;
    ZipEntry mime;
    mime.name = QStringLiteral("mimetype");
    mime.data = QByteArray("application/epub+zip");
    mime.store = true;
    entries.push_back(mime);
    auto add = [&](const QString &name, const QString &text) {
        ZipEntry e;
        e.name = name;
        e.data = text.toUtf8();
        entries.push_back(e);
    };
    add(QStringLiteral("META-INF/container.xml"), container);
    add(QStringLiteral("OEBPS/content.opf"), opf);
    add(QStringLiteral("OEBPS/nav.xhtml"), nav);
    add(QStringLiteral("OEBPS/style.css"), css);
    for (int i = 0; i < chapters.size(); ++i)
        add(QStringLiteral("OEBPS/") + chapters[i].href, chapterXml.at(i));
    for (const auto &a : assets) {
        ZipEntry e;
        e.name = QStringLiteral("OEBPS/") + a.href;
        e.data = a.bytes;
        entries.push_back(e);
    }

    const QByteArray zip = Zip::pack(entries);
    if (zip.size() < 22 || !zip.startsWith("PK")) {
        if (error)
            *error = QStringLiteral("EPUB zip failed");
        return false;
    }
    if (!writeFileAtomic(path, zip, error))
        return false;
    if (info) {
        info->insert(QStringLiteral("format"), QStringLiteral("epub"));
        info->insert(QStringLiteral("chapters"), chapters.size());
        info->insert(QStringLiteral("images"), assets.size());
        info->insert(QStringLiteral("identifier"), urn);
        if (!warnings.isEmpty()) {
            QJsonArray wa;
            for (const QString &w : warnings)
                wa.append(w);
            info->insert(QStringLiteral("warnings"), wa);
        }
    }
    return true;
}

} // namespace aioffice
