#include "core/EpubReader.h"

#include "core/Document.h"
#include "core/Zip.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <QXmlStreamReader>

#include <utility>

namespace aioffice {
namespace {

constexpr int kMaxImageBytes = 32 * 1024 * 1024;
constexpr int kMaxElementDepth = 256;
constexpr int kMaxElements = 2000000;
constexpr int kMaxTableColumns = 256;
constexpr int kMaxTableRows = 10000;
constexpr qint64 kMaxArchiveBytes = 1ll << 30;

// Temporary marker for an unresolved internal link or note reference. U+001F
// cannot appear in XML text (XML 1.0 forbids every C0 control but tab, LF, CR),
// so no imported run can collide with it.
constexpr ushort kMarker = 0x1F;

struct XNode {
    QString name; // local name, lower case; empty for a text node
    bool textNode = false;
    QVector<QPair<QString, QString>> attrs;
    QString text; // text node content
    QVector<XNode> children;
    int line = 1;
};

QString xmlErrorText(const QString &file, const QXmlStreamReader &xml)
{
    QString message = xml.errorString();
    if (message.isEmpty())
        message = QStringLiteral("invalid XML");
    return file + QStringLiteral(": ") + message + QStringLiteral(" at line ")
        + QString::number(qMax(1, int(xml.lineNumber())));
}

bool parseElement(QXmlStreamReader &xml, const QString &file, int depth, int *elements, XNode *out, QString *error)
{
    if (++*elements > kMaxElements) {
        *error = file + QStringLiteral(": too many elements at line ") + QString::number(int(xml.lineNumber()));
        return false;
    }
    if (depth > kMaxElementDepth) {
        *error = file + QStringLiteral(": element nesting too deep at line ") + QString::number(int(xml.lineNumber()));
        return false;
    }
    out->name = xml.qualifiedName().toString().section(QLatin1Char(':'), -1).toLower();
    out->line = int(xml.lineNumber());
    for (const QXmlStreamAttribute &a : xml.attributes())
        out->attrs.push_back({a.qualifiedName().toString(), a.value().toString()});
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.hasError()) {
            *error = xmlErrorText(file, xml);
            return false;
        }
        if (xml.isStartElement()) {
            XNode child;
            if (!parseElement(xml, file, depth + 1, elements, &child, error))
                return false;
            out->children.push_back(std::move(child));
        } else if (xml.isEndElement()) {
            return true;
        } else if (xml.isCharacters()) {
            // Keep text in document order with the child elements: a paragraph
            // like "a <b>c</b> d" must not become the text "a d" plus a child.
            const QString chunk = xml.text().toString();
            if (!chunk.isEmpty()) {
                if (!out->children.isEmpty() && out->children.last().textNode) {
                    out->children.last().text += chunk;
                } else {
                    XNode textNode;
                    textNode.textNode = true;
                    textNode.text = chunk;
                    textNode.line = int(xml.lineNumber());
                    out->children.push_back(std::move(textNode));
                }
            }
        }
    }
    *error = xmlErrorText(file, xml);
    return false;
}

bool parseXml(const QByteArray &bytes, const QString &file, XNode *root, QString *error)
{
    if (bytes.isEmpty()) {
        *error = file + QStringLiteral(": empty document at line 1");
        return false;
    }
    QXmlStreamReader xml(bytes);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.hasError()) {
            *error = xmlErrorText(file, xml);
            return false;
        }
        if (xml.isStartElement())
            break;
    }
    if (!xml.isStartElement()) {
        *error = file + QStringLiteral(": empty document at line 1");
        return false;
    }
    int elements = 0;
    if (!parseElement(xml, file, 1, &elements, root, error))
        return false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.hasError()) {
            *error = xmlErrorText(file, xml);
            return false;
        }
    }
    return true;
}

QString attr(const XNode &node, const QString &name)
{
    const QString want = name.toLower();
    for (const auto &a : node.attrs) {
        if (a.first.compare(want, Qt::CaseInsensitive) == 0)
            return a.second;
        const int colon = a.first.indexOf(QLatin1Char(':'));
        if (colon > 0 && a.first.mid(colon + 1).compare(want, Qt::CaseInsensitive) == 0)
            return a.second;
    }
    return {};
}

bool hasEpubType(const XNode &node, const QString &token)
{
    const QStringList parts = attr(node, QStringLiteral("epub:type")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part.trimmed().compare(token, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool isHeadingTag(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^h[1-6]$"));
    return re.match(name).hasMatch();
}

bool isSkippedContainer(const QString &name)
{
    return name == QLatin1String("script") || name == QLatin1String("style") || name == QLatin1String("iframe")
        || name == QLatin1String("object") || name == QLatin1String("embed") || name == QLatin1String("form")
        || name == QLatin1String("noscript") || name == QLatin1String("template") || name == QLatin1String("head")
        || name == QLatin1String("nav");
}

bool isBlockTag(const QString &name)
{
    return name == QLatin1String("p") || name == QLatin1String("div") || name == QLatin1String("ul")
        || name == QLatin1String("ol") || name == QLatin1String("table") || name == QLatin1String("figure")
        || name == QLatin1String("aside") || name == QLatin1String("blockquote") || name == QLatin1String("section")
        || name == QLatin1String("article") || name == QLatin1String("main") || name == QLatin1String("header")
        || name == QLatin1String("footer") || name == QLatin1String("pre") || name == QLatin1String("dl")
        || name == QLatin1String("dt") || name == QLatin1String("dd") || name == QLatin1String("hr")
        || isHeadingTag(name);
}

bool hasBlockChild(const XNode &node)
{
    for (const XNode &child : node.children) {
        if (isBlockTag(child.name))
            return true;
    }
    return false;
}

bool isTrimSpace(QChar c)
{
    return c == QLatin1Char(' ') || c == QLatin1Char('\n') || c == QLatin1Char('\t') || c == QLatin1Char('\r');
}

QString collapseSpace(const QString &text)
{
    QString out;
    out.reserve(text.size());
    bool previousSpace = false;
    for (const QChar c : text) {
        if (c == QLatin1Char('\r'))
            continue;
        if (c == QLatin1Char(' ') || c == QLatin1Char('\n') || c == QLatin1Char('\t')) {
            if (!previousSpace)
                out += QLatin1Char(' ');
            previousSpace = true;
            continue;
        }
        out += c;
        previousSpace = false;
    }
    return out;
}

QString preformattedText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

bool hasVisibleText(const QVector<TextRun> &runs)
{
    for (const auto &r : runs) {
        for (const QChar c : r.text) {
            if (!isTrimSpace(c))
                return true;
        }
    }
    return false;
}

void trimRuns(QVector<TextRun> &runs)
{
    while (!runs.isEmpty()) {
        QString &text = runs.first().text;
        int cut = 0;
        while (cut < text.size() && isTrimSpace(text.at(cut)))
            ++cut;
        text = text.mid(cut);
        if (text.isEmpty() && runs.first().noteId.isEmpty() && runs.first().field.isEmpty())
            runs.removeFirst();
        else
            break;
    }
    while (!runs.isEmpty()) {
        QString &text = runs.last().text;
        int cut = text.size();
        while (cut > 0 && isTrimSpace(text.at(cut - 1)))
            --cut;
        text = text.left(cut);
        if (text.isEmpty() && runs.last().noteId.isEmpty() && runs.last().field.isEmpty())
            runs.removeLast();
        else
            break;
    }
}

bool sameRunFormat(const TextRun &a, const TextRun &b)
{
    return a.bold == b.bold && a.italic == b.italic && a.underline == b.underline && a.strike == b.strike
        && a.superscript == b.superscript && a.subscript == b.subscript && a.font == b.font
        && qFuzzyCompare(a.sizePt + 1.0, b.sizePt + 1.0) && a.color == b.color && a.highlight == b.highlight
        && a.smallCaps == b.smallCaps && a.caps == b.caps && a.hidden == b.hidden && a.charStyle == b.charStyle
        && a.kerning == b.kerning && a.link == b.link && a.field == b.field && a.fieldArg == b.fieldArg
        && a.xrefKind == b.xrefKind && a.noteId == b.noteId && a.ruby == b.ruby && a.redact == b.redact
        && a.showIf == b.showIf && a.lang == b.lang;
}

bool hasScheme(const QString &href)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z][A-Za-z0-9+.\\-]*:"));
    return re.match(href).hasMatch();
}

bool isKeptExternalLink(const QString &href)
{
    const QString lower = href.toLower();
    return lower.startsWith(QLatin1String("http://")) || lower.startsWith(QLatin1String("https://"))
        || lower.startsWith(QLatin1String("mailto:"));
}

QString decodePercent(QString text)
{
    return QUrl::fromPercentEncoding(text.toUtf8());
}

struct ResolvedHref {
    QString path;
    QString fragment;
    QString reason; // empty when ok

    bool ok() const { return reason.isEmpty(); }
};

// Resolves a package href against the resource it appears in. `resourcePath` is
// the full path inside the zip (the current document or the OPF). A href with a
// scheme, an absolute path, or a ".." segment is rejected with a reason; the
// caller turns that into a warning (D7).
ResolvedHref resolveHref(const QString &resourcePath, const QString &raw)
{
    ResolvedHref out;
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) {
        out.reason = QStringLiteral("empty");
        return out;
    }
    if (hasScheme(trimmed)) {
        out.reason = QStringLiteral("scheme");
        return out;
    }
    QString reference = trimmed;
    const int hash = reference.indexOf(QLatin1Char('#'));
    if (hash >= 0) {
        out.fragment = decodePercent(reference.mid(hash + 1));
        reference = reference.left(hash);
    }
    const int query = reference.indexOf(QLatin1Char('?'));
    if (query >= 0)
        reference = reference.left(query);
    reference = decodePercent(reference);
    if (reference.isEmpty()) {
        if (hash >= 0) {
            out.path = resourcePath;
            return out;
        }
        out.reason = QStringLiteral("empty");
        return out;
    }
    if (reference.startsWith(QLatin1Char('/'))) {
        out.reason = QStringLiteral("absolute");
        return out;
    }
    QStringList parts;
    const QStringList segments = reference.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString &segment : segments) {
        if (segment.isEmpty() || segment == QLatin1String("."))
            continue;
        if (segment == QLatin1String("..")) {
            out.reason = QStringLiteral("parent");
            return out;
        }
        parts.push_back(segment);
    }
    if (parts.isEmpty()) {
        out.reason = QStringLiteral("empty");
        return out;
    }
    const int slash = resourcePath.lastIndexOf(QLatin1Char('/'));
    const QString base = slash > 0 ? resourcePath.left(slash + 1) : QString();
    out.path = base + parts.join(QLatin1Char('/'));
    return out;
}

// Same sanitizer as the EPUB writer, so a class our export emits maps back to
// the style name it came from.
QString cssClassName(const QString &name)
{
    if (name.trimmed().isEmpty())
        return {};
    QString out = QStringLiteral("s");
    for (const QChar c : name) {
        if (c.isLetterOrNumber())
            out += c;
        else if (!out.endsWith(QLatin1Char('-')))
            out += QLatin1Char('-');
    }
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    if (out.size() < 2)
        return {};
    return out;
}

QHash<QString, QString> cssDeclarations(const QString &style)
{
    QHash<QString, QString> out;
    for (const QString &part : style.split(QLatin1Char(';'))) {
        const int colon = part.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString key = part.left(colon).trimmed().toLower();
        QString value = part.mid(colon + 1).trimmed().toLower();
        const int important = value.indexOf(QLatin1String("!important"));
        if (important >= 0)
            value = value.left(important).trimmed();
        if (!key.isEmpty())
            out.insert(key, value);
    }
    return out;
}

void applyInlineCss(const QString &style, TextRun &run)
{
    if (style.trimmed().isEmpty())
        return;
    const QHash<QString, QString> decls = cssDeclarations(style);
    const auto bold = decls.constFind(QStringLiteral("font-weight"));
    if (bold != decls.cend()) {
        const QString v = bold.value();
        if (v == QLatin1String("bold") || v == QLatin1String("bolder") || v == QLatin1String("700")
            || v == QLatin1String("800") || v == QLatin1String("900")) {
            run.bold = true;
        } else if (v == QLatin1String("normal") || v == QLatin1String("lighter") || v == QLatin1String("400")) {
            run.bold = false;
        }
    }
    const auto italic = decls.constFind(QStringLiteral("font-style"));
    if (italic != decls.cend()) {
        if (italic.value().startsWith(QLatin1String("italic")) || italic.value().startsWith(QLatin1String("oblique")))
            run.italic = true;
        else if (italic.value() == QLatin1String("normal"))
            run.italic = false;
    }
    const auto decoration = decls.constFind(QStringLiteral("text-decoration"));
    if (decoration != decls.cend()) {
        if (decoration.value().contains(QLatin1String("underline")))
            run.underline = true;
        if (decoration.value().contains(QLatin1String("line-through")))
            run.strike = true;
    }
}

QString plainTextOf(const XNode &node)
{
    if (node.textNode)
        return node.text;
    QString text;
    for (const XNode &child : node.children)
        text += plainTextOf(child);
    return collapseSpace(text);
}

struct ManifestItem {
    QString id;
    QString href;
    QString mediaType;
    QString properties;
    QString path;
};

struct PackageInfo {
    QString title;
    QString creator;
    QString language;
    QVector<ManifestItem> items;
    QHash<QString, QString> mimeByPath;
    QVector<ManifestItem> spine;
};

const XNode *findChild(const XNode &node, const QString &name)
{
    for (const XNode &child : node.children) {
        if (child.name == name)
            return &child;
    }
    return nullptr;
}

const XNode *findDescendant(const XNode &node, const QString &name)
{
    for (const XNode &child : node.children) {
        if (child.name == name)
            return &child;
        if (const XNode *found = findDescendant(child, name))
            return found;
    }
    return nullptr;
}

void collectDescendants(const XNode &node, const QString &name, QVector<const XNode *> *out)
{
    for (const XNode &child : node.children) {
        if (child.name == name)
            out->push_back(&child);
        collectDescendants(child, name, out);
    }
}

bool isBcp47(const QString &tag)
{
    static const QRegularExpression re(QStringLiteral(
        "^[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})*(?:-[A-Za-z0-9](?:-[A-Za-z0-9]{1,8})+)*$"));
    return re.match(tag).hasMatch();
}

bool isFontPath(const QString &path)
{
    const QString lower = path.toLower();
    return lower.endsWith(QLatin1String(".ttf")) || lower.endsWith(QLatin1String(".otf"))
        || lower.endsWith(QLatin1String(".woff")) || lower.endsWith(QLatin1String(".woff2"))
        || lower.endsWith(QLatin1String(".eot"));
}

// D7: an EPUB that encrypts a package resource is DRM-protected and is refused.
// Font obfuscation (IDPF/Adobe) is allowed: the reader never uses those fonts.
bool checkEncryption(const QVector<ZipEntry> &entries, QString *error)
{
    const QByteArray bytes = Zip::file(entries, QStringLiteral("META-INF/encryption.xml"));
    if (bytes.isEmpty())
        return true;
    XNode root;
    QString parseError;
    if (!parseXml(bytes, QStringLiteral("META-INF/encryption.xml"), &root, &parseError)) {
        *error = parseError;
        return false;
    }
    QVector<const XNode *> references;
    collectDescendants(root, QStringLiteral("cipherreference"), &references);
    QVector<const XNode *> encrypted;
    collectDescendants(root, QStringLiteral("encrypteddata"), &encrypted);
    if (encrypted.isEmpty())
        return true;
    if (references.isEmpty()) {
        *error = QStringLiteral("EPUB is DRM-protected: encrypted resources without a reference");
        return false;
    }
    for (const XNode *node : references) {
        const QString uri = attr(*node, QStringLiteral("uri")).trimmed();
        if (!isFontPath(uri)) {
            *error = QStringLiteral("EPUB is DRM-protected: ") + uri;
            return false;
        }
    }
    return true;
}

struct BlockTarget {
    QString type;
    int level = 0;
    QString listKind;
    QString style;
    QString lang;
    QString dir;
    QString sourceId;
    bool dropImages = false;
};

struct TableRowSource {
    QVector<const XNode *> cells;
};

struct Builder {
    Document &doc;
    const QVector<ZipEntry> &entries;
    QStringList *warnings = nullptr;
    QHash<QString, QString> mimeByPath;
    QHash<QString, QString> classToStyle;
    QString docPath;
    QString error;
    bool failed = false;
    QHash<QString, QString> noteIds;
    QHash<QString, QString> blockIds;

    void warn(const QString &message)
    {
        if (warnings && !warnings->contains(message))
            warnings->push_back(message);
    }

    bool fail(const QString &message)
    {
        if (error.isEmpty())
            error = message;
        failed = true;
        return false;
    }

    QString keyFor(const QString &id) const { return docPath + QLatin1Char('#') + id; }

    QString styleFor(const XNode &node) const
    {
        const QStringList tokens = attr(node, QStringLiteral("class")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &token : tokens) {
            const auto it = classToStyle.constFind(token.trimmed());
            if (it != classToStyle.cend())
                return it.value();
        }
        return {};
    }

    static QString langFor(const XNode &node, const QString &inherited)
    {
        const QString own = attr(node, QStringLiteral("lang")).trimmed();
        return own.isEmpty() ? inherited : own;
    }

    static QString dirFor(const XNode &node, const QString &inherited)
    {
        const QString own = attr(node, QStringLiteral("dir")).toLower();
        if (own == QLatin1String("rtl") || own == QLatin1String("ltr"))
            return own;
        return inherited;
    }

    BlockTarget targetFor(const XNode &node, const QString &type, const QString &lang, const QString &dir,
                          int level = 0) const
    {
        BlockTarget target;
        target.type = type;
        target.level = level;
        target.style = styleFor(node);
        target.lang = langFor(node, lang);
        target.dir = dirFor(node, dir);
        target.sourceId = attr(node, QStringLiteral("id")).trimmed();
        return target;
    }

    void appendBlock(Block block, const QString &sourceId)
    {
        doc.assignId(block);
        if (!sourceId.isEmpty()) {
            const QString key = keyFor(sourceId);
            if (!blockIds.contains(key))
                blockIds.insert(key, block.id);
        }
        doc.body.push_back(std::move(block));
    }

    void flush(QVector<TextRun> &runs, const BlockTarget &target)
    {
        trimRuns(runs);
        if (!hasVisibleText(runs)) {
            runs.clear();
            return;
        }
        Block block;
        block.type = target.type;
        block.level = target.level;
        block.listKind = target.listKind;
        block.style = target.style;
        block.lang = target.lang;
        block.dir = target.dir;
        block.runs = runs;
        runs.clear();
        appendBlock(std::move(block), target.sourceId);
    }

    void pushText(QVector<TextRun> &runs, const TextRun &format, const QString &text, bool preserveSpace);
    bool collectInline(const XNode &node, const TextRun &format, QVector<TextRun> &runs, const BlockTarget &target,
                       bool preserveSpace);
    bool emitParagraph(const XNode &node, const QString &lang, const QString &dir, bool preserveSpace);
    bool emitHeading(const XNode &node, const QString &lang, const QString &dir);
    bool walkBlock(const XNode &node, const QString &lang, const QString &dir);
    bool walkChildren(const QVector<XNode> &children, const QString &lang, const QString &dir);
    bool walkList(const XNode &list, int level, const QString &lang, const QString &dir);
    bool walkTable(const XNode &table, const QString &lang, const QString &dir);
    bool walkFigure(const XNode &figure, const QString &lang, const QString &dir);
    bool emitImage(const XNode &image, const BlockTarget &target, const QString &caption);
    bool emitNote(const XNode &aside, const QString &lang, const QString &dir);
    bool collectNoteGroups(const QVector<XNode> &children, const TextRun &format, QVector<TextRun> &out,
                           const BlockTarget &target, int *groups);
    bool buildCell(const XNode &cell, TableCell *out, const BlockTarget &target);
    bool buildNested(const XNode &cell, QVector<QVector<NestedCell>> *out, const BlockTarget &target);
    void resolveAll();
    void resolveRuns(QVector<TextRun> &runs);
};

void Builder::pushText(QVector<TextRun> &runs, const TextRun &format, const QString &text, bool preserveSpace)
{
    const QString prepared = preserveSpace ? preformattedText(text) : collapseSpace(text);
    if (prepared.isEmpty())
        return;
    if (!runs.isEmpty() && sameRunFormat(runs.last(), format)) {
        runs.last().text += prepared;
        return;
    }
    TextRun run = format;
    run.text = prepared;
    runs.push_back(std::move(run));
}

bool Builder::collectInline(const XNode &node, const TextRun &format, QVector<TextRun> &runs,
                            const BlockTarget &target, bool preserveSpace)
{
    if (node.textNode) {
        pushText(runs, format, node.text, preserveSpace);
        return true;
    }
    const QString name = node.name;
    if (isSkippedContainer(name) || name == QLatin1String("rt") || name == QLatin1String("rp"))
        return true;
    if (name == QLatin1String("table"))
        return true; // nested tables inside inline content are not flattened into text
    if (name == QLatin1String("img")) {
        if (target.dropImages) {
            warn(QStringLiteral("EPUB import: image inside a note or table cell was ignored"));
            return true;
        }
        flush(runs, target);
        return emitImage(node, target, QString());
    }

    TextRun next = format;
    if (name == QLatin1String("strong") || name == QLatin1String("b")) {
        next.bold = true;
    } else if (name == QLatin1String("em") || name == QLatin1String("i")) {
        next.italic = true;
    } else if (name == QLatin1String("u") || name == QLatin1String("ins")) {
        next.underline = true;
    } else if (name == QLatin1String("s") || name == QLatin1String("strike") || name == QLatin1String("del")) {
        next.strike = true;
    } else if (name == QLatin1String("sub")) {
        next.subscript = true;
    } else if (name == QLatin1String("sup")) {
        next.superscript = true;
    } else if (name == QLatin1String("span")) {
        applyInlineCss(attr(node, QStringLiteral("style")), next);
    } else if (name == QLatin1String("br")) {
        pushText(runs, next, QStringLiteral("\n"), preserveSpace);
    } else if (name == QLatin1String("hr")) {
        pushText(runs, next, QStringLiteral("\n"), preserveSpace);
    } else if (name == QLatin1String("a")) {
        if (hasEpubType(node, QStringLiteral("backlink")))
            return true; // the backlink label repeats the reference number
        const QString href = attr(node, QStringLiteral("href")).trimmed();
        if (hasEpubType(node, QStringLiteral("noteref"))) {
            const ResolvedHref resolved = resolveHref(docPath, href);
            if (!resolved.ok()) {
                warn(QStringLiteral("EPUB import: ignored note reference ") + href);
            } else {
                next.noteId = QString(QChar(kMarker)) + resolved.path + QLatin1Char('#') + resolved.fragment;
                next.superscript = true;
            }
        } else if (href.isEmpty()) {
            // no target: keep the text
        } else if (isKeptExternalLink(href)) {
            next.link = href;
        } else if (!hasScheme(href)) {
            const ResolvedHref resolved = resolveHref(docPath, href);
            if (!resolved.ok()) {
                warn(QStringLiteral("EPUB import: ignored href ") + href);
            } else {
                next.link = QString(QChar(kMarker)) + resolved.path + QLatin1Char('#') + resolved.fragment;
            }
        } else {
            warn(QStringLiteral("EPUB import: ignored href ") + href);
        }
    }

    for (const XNode &child : node.children) {
        if (!collectInline(child, next, runs, target, preserveSpace))
            return false;
    }
    return true;
}

bool Builder::emitParagraph(const XNode &node, const QString &lang, const QString &dir, bool preserveSpace)
{
    BlockTarget target = targetFor(node, QStringLiteral("paragraph"), lang, dir);
    QVector<TextRun> runs;
    if (!collectInline(node, TextRun{}, runs, target, preserveSpace))
        return false;
    flush(runs, target);
    return true;
}

bool Builder::emitHeading(const XNode &node, const QString &lang, const QString &dir)
{
    const int level = qBound(1, node.name.mid(1).toInt(), 6);
    BlockTarget target = targetFor(node, QStringLiteral("heading"), lang, dir, level);
    QVector<TextRun> runs;
    if (!collectInline(node, TextRun{}, runs, target, false))
        return false;
    flush(runs, target);
    return true;
}

bool Builder::emitImage(const XNode &image, const BlockTarget &target, const QString &caption)
{
    const QString src = attr(image, QStringLiteral("src")).trimmed();
    if (src.isEmpty()) {
        warn(QStringLiteral("EPUB import: image without src was ignored"));
        return true;
    }
    if (hasScheme(src) || src.startsWith(QLatin1Char('/'))) {
        warn(QStringLiteral("EPUB import: ignored image ") + src);
        return true;
    }
    const ResolvedHref resolved = resolveHref(docPath, src);
    if (!resolved.ok()) {
        warn(QStringLiteral("EPUB import: ignored image ") + src);
        return true;
    }
    const auto mimeIt = mimeByPath.constFind(resolved.path);
    if (mimeIt == mimeByPath.cend() || !mimeIt.value().startsWith(QLatin1String("image/"))) {
        warn(QStringLiteral("EPUB import: image is not a manifest image: ") + resolved.path);
        return true;
    }
    const QByteArray bytes = Zip::file(entries, resolved.path);
    if (bytes.isEmpty()) {
        warn(QStringLiteral("EPUB import: image not found: ") + resolved.path);
        return true;
    }
    if (bytes.size() > kMaxImageBytes)
        return fail(QStringLiteral("EPUB image exceeds 32MB: ") + resolved.path);

    Block block;
    block.type = QStringLiteral("image");
    block.lang = target.lang;
    block.dir = target.dir;
    block.imageBytes = bytes;
    block.imageMime = mimeIt.value();
    block.altText = attr(image, QStringLiteral("alt")).trimmed();
    if (block.altText.isEmpty())
        block.altText = caption;
    block.caption = caption;
    appendBlock(std::move(block), target.sourceId);
    return true;
}

bool Builder::walkFigure(const XNode &figure, const QString &lang, const QString &dir)
{
    const XNode *captionNode = findDescendant(figure, QStringLiteral("figcaption"));
    QString caption;
    if (captionNode)
        caption = plainTextOf(*captionNode).trimmed();
    const XNode *imageNode = findDescendant(figure, QStringLiteral("img"));
    if (imageNode) {
        BlockTarget target = targetFor(figure, QStringLiteral("image"), lang, dir);
        if (!emitImage(*imageNode, target, caption))
            return false;
    }
    for (const XNode &child : figure.children) {
        if (child.name == QLatin1String("figcaption") || child.name == QLatin1String("img"))
            continue;
        if (isSkippedContainer(child.name))
            continue;
        if (isBlockTag(child.name)) {
            if (!walkBlock(child, lang, dir))
                return false;
        }
    }
    return true;
}

bool Builder::buildCell(const XNode &node, TableCell *out, const BlockTarget &target)
{
    BlockTarget cellTarget = target;
    cellTarget.dropImages = true;
    cellTarget.sourceId.clear();
    QVector<TextRun> runs;
    if (!collectInline(node, TextRun{}, runs, cellTarget, false))
        return false;
    trimRuns(runs);
    out->runs = runs;
    return buildNested(node, &out->nested, cellTarget);
}

bool Builder::buildNested(const XNode &cell, QVector<QVector<NestedCell>> *out, const BlockTarget &target)
{
    for (const XNode &child : cell.children) {
        if (child.name != QLatin1String("table"))
            continue;
        QVector<const XNode *> rows;
        for (const XNode &row : child.children) {
            if (row.name == QLatin1String("tr")) {
                rows.push_back(&row);
            } else if (row.name == QLatin1String("thead") || row.name == QLatin1String("tbody")
                       || row.name == QLatin1String("tfoot")) {
                for (const XNode &inner : row.children) {
                    if (inner.name == QLatin1String("tr"))
                        rows.push_back(&inner);
                }
            }
        }
        for (const XNode *row : rows) {
            QVector<NestedCell> nestedRow;
            for (const XNode &cellNode : row->children) {
                if (cellNode.name != QLatin1String("td") && cellNode.name != QLatin1String("th"))
                    continue;
                NestedCell nested;
                QVector<TextRun> runs;
                if (!collectInline(cellNode, TextRun{}, runs, target, false))
                    return false;
                trimRuns(runs);
                nested.runs = runs;
                nestedRow.push_back(nested);
            }
            out->push_back(nestedRow);
        }
    }
    return true;
}

bool Builder::walkTable(const XNode &table, const QString &lang, const QString &dir)
{
    QVector<const XNode *> rowNodes;
    for (const XNode &child : table.children) {
        if (child.name == QLatin1String("tr")) {
            rowNodes.push_back(&child);
        } else if (child.name == QLatin1String("thead") || child.name == QLatin1String("tbody")
                   || child.name == QLatin1String("tfoot")) {
            for (const XNode &inner : child.children) {
                if (inner.name == QLatin1String("tr"))
                    rowNodes.push_back(&inner);
            }
        }
    }
    if (rowNodes.isEmpty())
        return true;
    if (rowNodes.size() > kMaxTableRows)
        return fail(QStringLiteral("EPUB table has more than %1 rows").arg(kMaxTableRows));

    QVector<TableRowSource> declared;
    declared.reserve(rowNodes.size());
    for (const XNode *rowNode : rowNodes) {
        TableRowSource row;
        for (const XNode &cell : rowNode->children) {
            if (cell.name == QLatin1String("td") || cell.name == QLatin1String("th"))
                row.cells.push_back(&cell);
        }
        if (!row.cells.isEmpty())
            declared.push_back(row);
    }
    if (declared.isEmpty())
        return true;
    bool header = true;
    for (const XNode *cell : declared.first().cells) {
        if (cell->name != QLatin1String("th"))
            header = false;
    }

    BlockTarget target = targetFor(table, QStringLiteral("table"), lang, dir);
    QHash<qint64, bool> covered;
    const auto slot = [](int r, int c) -> qint64 { return (qint64(r) << 32) | qint64(quint32(c)); };
    QVector<QVector<TableCell>> grid;
    for (int r = 0; r < declared.size(); ++r) {
        QVector<TableCell> row;
        int next = 0;
        for (int c = 0; c < kMaxTableColumns; ++c) {
            if (covered.contains(slot(r, c))) {
                row.push_back(TableCell{});
                continue;
            }
            if (next >= declared.at(r).cells.size())
                break;
            const XNode &cellNode = *declared.at(r).cells.at(next++);
            TableCell cell;
            bool ok = false;
            const int colspan = qBound(1, attr(cellNode, QStringLiteral("colspan")).toInt(&ok), kMaxTableColumns);
            const int rowspan = qBound(1, attr(cellNode, QStringLiteral("rowspan")).toInt(&ok), kMaxTableRows);
            cell.colSpan = colspan;
            cell.rowSpan = qMin(rowspan, declared.size() - r);
            if (!buildCell(cellNode, &cell, target))
                return false;
            for (int rr = r; rr < r + cell.rowSpan; ++rr) {
                for (int cc = c; cc < c + cell.colSpan; ++cc) {
                    if (rr == r && cc == c)
                        continue;
                    covered.insert(slot(rr, cc), true);
                }
            }
            row.push_back(cell);
        }
        if (next < declared.at(r).cells.size())
            return fail(QStringLiteral("EPUB table has more than %1 columns").arg(kMaxTableColumns));
        grid.push_back(row);
    }

    Block block;
    block.type = QStringLiteral("table");
    block.lang = target.lang;
    block.dir = target.dir;
    block.style = target.style;
    block.headerRow = header && !grid.isEmpty();
    block.table = grid;
    appendBlock(std::move(block), target.sourceId);
    return true;
}

bool Builder::walkList(const XNode &list, int level, const QString &lang, const QString &dir)
{
    const QString kind = list.name == QLatin1String("ol") ? QStringLiteral("decimal") : QStringLiteral("bullet");
    for (const XNode &item : list.children) {
        if (item.name == QLatin1String("ul") || item.name == QLatin1String("ol")) {
            if (!walkList(item, level, lang, dir))
                return false;
            continue;
        }
        if (item.name != QLatin1String("li"))
            continue;
        const QString itemLang = langFor(item, lang);
        const QString itemDir = dirFor(item, dir);
        bool hasParagraphs = false;
        for (const XNode &child : item.children) {
            if (child.name == QLatin1String("p") || isHeadingTag(child.name))
                hasParagraphs = true;
        }
        if (hasParagraphs) {
            for (const XNode &child : item.children) {
                if (child.name != QLatin1String("p") && !isHeadingTag(child.name))
                    continue;
                BlockTarget target = targetFor(item, QStringLiteral("list-item"), itemLang, itemDir, level);
                target.listKind = kind;
                target.style = styleFor(child).isEmpty() ? target.style : styleFor(child);
                target.sourceId = attr(child, QStringLiteral("id")).trimmed();
                QVector<TextRun> runs;
                if (!collectInline(child, TextRun{}, runs, target, false))
                    return false;
                flush(runs, target);
            }
        } else {
            BlockTarget target = targetFor(item, QStringLiteral("list-item"), itemLang, itemDir, level);
            target.listKind = kind;
            QVector<TextRun> runs;
            for (const XNode &child : item.children) {
                if (child.name == QLatin1String("ul") || child.name == QLatin1String("ol")
                    || child.name == QLatin1String("table"))
                    continue;
                if (!collectInline(child, TextRun{}, runs, target, false))
                    return false;
            }
            flush(runs, target);
        }
        for (const XNode &child : item.children) {
            if (child.name == QLatin1String("ul") || child.name == QLatin1String("ol")) {
                if (!walkList(child, level + 1, itemLang, itemDir))
                    return false;
            }
        }
    }
    return true;
}

bool Builder::collectNoteGroups(const QVector<XNode> &children, const TextRun &format, QVector<TextRun> &out,
                                const BlockTarget &target, int *groups)
{
    QVector<TextRun> current;
    const auto flushGroup = [&](QVector<TextRun> &runs) {
        trimRuns(runs);
        if (hasVisibleText(runs)) {
            if (*groups > 0) {
                TextRun separator;
                separator.text = QString(QChar(0x2029));
                out.push_back(separator);
            }
            for (auto &run : runs)
                out.push_back(std::move(run));
            ++*groups;
        }
        runs.clear();
    };
    for (const XNode &child : children) {
        if (child.name == QLatin1String("p") || isHeadingTag(child.name)) {
            flushGroup(current);
            QVector<TextRun> runs;
            if (!collectInline(child, format, runs, target, false))
                return false;
            flushGroup(runs);
        } else if (child.name == QLatin1String("div") || child.name == QLatin1String("section")
                   || child.name == QLatin1String("blockquote")) {
            flushGroup(current);
            if (!collectNoteGroups(child.children, format, out, target, groups))
                return false;
        } else {
            if (!collectInline(child, format, current, target, false))
                return false;
        }
    }
    flushGroup(current);
    return true;
}

bool Builder::emitNote(const XNode &aside, const QString &lang, const QString &dir)
{
    const QString type = hasEpubType(aside, QStringLiteral("endnote")) ? QStringLiteral("endnote")
                                                                       : QStringLiteral("footnote");
    BlockTarget target = targetFor(aside, type, lang, dir);
    target.dropImages = true;
    QVector<TextRun> runs;
    int groups = 0;
    if (!collectNoteGroups(aside.children, TextRun{}, runs, target, &groups))
        return false;
    Block block;
    block.type = type;
    block.style = QStringLiteral("Normal");
    block.lang = target.lang;
    block.dir = target.dir;
    block.runs = runs;
    if (block.runs.isEmpty())
        block.runs.push_back(TextRun{});
    doc.assignId(block);
    if (!target.sourceId.isEmpty()) {
        const QString key = keyFor(target.sourceId);
        if (!noteIds.contains(key))
            noteIds.insert(key, block.id);
        if (!blockIds.contains(key))
            blockIds.insert(key, block.id);
    }
    doc.body.push_back(std::move(block));
    return true;
}

bool Builder::walkBlock(const XNode &node, const QString &lang, const QString &dir)
{
    const QString name = node.name;
    if (isSkippedContainer(name))
        return true;
    if (isHeadingTag(name))
        return emitHeading(node, lang, dir);
    if (name == QLatin1String("p"))
        return emitParagraph(node, lang, dir, false);
    if (name == QLatin1String("pre"))
        return emitParagraph(node, lang, dir, true);
    if (name == QLatin1String("ul") || name == QLatin1String("ol"))
        return walkList(node, 1, lang, dir);
    if (name == QLatin1String("table"))
        return walkTable(node, lang, dir);
    if (name == QLatin1String("figure"))
        return walkFigure(node, lang, dir);
    if (name == QLatin1String("aside")) {
        if (hasEpubType(node, QStringLiteral("footnote")) || hasEpubType(node, QStringLiteral("endnote")))
            return emitNote(node, lang, dir);
        return walkChildren(node.children, langFor(node, lang), dirFor(node, dir));
    }
    if (name == QLatin1String("hr"))
        return true;
    if (name == QLatin1String("dt") || name == QLatin1String("dd"))
        return emitParagraph(node, langFor(node, lang), dirFor(node, dir), false);
    if (name == QLatin1String("dl")) {
        for (const XNode &child : node.children) {
            if (!walkBlock(child, lang, dir))
                return false;
        }
        return true;
    }
    // container (div, section, article, main, header, footer, blockquote)
    const QString childLang = langFor(node, lang);
    const QString childDir = dirFor(node, dir);
    if (hasBlockChild(node))
        return walkChildren(node.children, childLang, childDir);
    return emitParagraph(node, childLang, childDir, false);
}

bool Builder::walkChildren(const QVector<XNode> &children, const QString &lang, const QString &dir)
{
    int index = 0;
    while (index < children.size()) {
        const XNode &node = children.at(index);
        if (isSkippedContainer(node.name) || node.name == QLatin1String("hr")) {
            ++index;
            continue;
        }
        if (isBlockTag(node.name)) {
            if (!walkBlock(node, lang, dir))
                return false;
            ++index;
            continue;
        }
        BlockTarget target;
        target.type = QStringLiteral("paragraph");
        target.lang = lang;
        target.dir = dir;
        QVector<TextRun> runs;
        bool any = false;
        while (index < children.size() && !isBlockTag(children.at(index).name)
               && !isSkippedContainer(children.at(index).name)) {
            if (!collectInline(children.at(index), TextRun{}, runs, target, false))
                return false;
            any = true;
            ++index;
        }
        if (any)
            flush(runs, target);
    }
    return true;
}

void Builder::resolveRuns(QVector<TextRun> &runs)
{
    for (TextRun &run : runs) {
        if (!run.link.isEmpty() && run.link.at(0) == QChar(kMarker)) {
            const QString key = run.link.mid(1);
            const QString id = blockIds.value(key);
            if (!id.isEmpty()) {
                run.link = QStringLiteral("#") + id;
            } else {
                run.link.clear();
                warn(QStringLiteral("EPUB import: internal link target not found: ") + key);
            }
        }
        if (!run.noteId.isEmpty() && run.noteId.at(0) == QChar(kMarker)) {
            const QString key = run.noteId.mid(1);
            const QString id = noteIds.value(key);
            if (!id.isEmpty()) {
                run.noteId = id;
            } else {
                run.noteId.clear();
                warn(QStringLiteral("EPUB import: note reference has no note: ") + key);
            }
        }
    }
}

void Builder::resolveAll()
{
    for (Block &block : doc.body) {
        resolveRuns(block.runs);
        resolveRuns(block.captionRuns);
        for (auto &row : block.table) {
            for (auto &cell : row) {
                resolveRuns(cell.runs);
                for (auto &nestedRow : cell.nested) {
                    for (auto &nestedCell : nestedRow)
                        resolveRuns(nestedCell.runs);
                }
            }
        }
    }
}

bool readPackage(const XNode &root, const QString &opfPath, PackageInfo *package, Builder *builder)
{
    const XNode *metadata = findChild(root, QStringLiteral("metadata"));
    if (metadata) {
        for (const XNode &child : metadata->children) {
            if (child.name == QLatin1String("title") && package->title.isEmpty())
                package->title = plainTextOf(child).trimmed();
            else if (child.name == QLatin1String("creator") && package->creator.isEmpty())
                package->creator = plainTextOf(child).trimmed();
            else if (child.name == QLatin1String("language") && package->language.isEmpty())
                package->language = plainTextOf(child).trimmed();
        }
    }
    const XNode *manifest = findChild(root, QStringLiteral("manifest"));
    if (!manifest)
        return builder->fail(QStringLiteral("EPUB package has no manifest"));
    QHash<QString, int> byId;
    for (const XNode &child : manifest->children) {
        if (child.name != QLatin1String("item"))
            continue;
        ManifestItem item;
        item.id = attr(child, QStringLiteral("id")).trimmed();
        item.href = attr(child, QStringLiteral("href")).trimmed();
        item.mediaType = attr(child, QStringLiteral("media-type")).trimmed().toLower();
        item.properties = attr(child, QStringLiteral("properties")).trimmed();
        if (item.id.isEmpty() || item.href.isEmpty())
            continue;
        const ResolvedHref resolved = resolveHref(opfPath, item.href);
        if (!resolved.ok()) {
            builder->warn(QStringLiteral("EPUB import: ignored manifest href ") + item.href);
            continue;
        }
        item.path = resolved.path;
        package->items.push_back(item);
        byId.insert(item.id, package->items.size() - 1);
        if (!item.mediaType.isEmpty())
            package->mimeByPath.insert(item.path, item.mediaType);
    }

    const XNode *spine = findChild(root, QStringLiteral("spine"));
    if (!spine)
        return builder->fail(QStringLiteral("EPUB package has no spine"));
    for (const XNode &child : spine->children) {
        if (child.name != QLatin1String("itemref"))
            continue;
        const QString idref = attr(child, QStringLiteral("idref")).trimmed();
        const auto it = byId.constFind(idref);
        if (it == byId.cend())
            return builder->fail(QStringLiteral("EPUB spine references an unknown manifest item: ") + idref);
        const ManifestItem &item = package->items.at(it.value());
        const QStringList props = item.properties.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (props.contains(QLatin1String("nav"), Qt::CaseInsensitive))
            continue;
        if (!item.mediaType.startsWith(QLatin1String("application/xhtml+xml")))
            return builder->fail(QStringLiteral("EPUB spine item is not XHTML: ") + item.path);
        package->spine.push_back(item);
    }
    if (package->spine.isEmpty())
        return builder->fail(QStringLiteral("EPUB spine is empty"));
    return true;
}

} // namespace

bool loadEpubFile(const QString &path, Document &doc, QString *error, QStringList *warnings)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    const QFileInfo info(path);
    if (!info.isFile())
        return fail(QStringLiteral("Cannot read ") + path);
    if (info.size() > kMaxArchiveBytes)
        return fail(QStringLiteral("archive entry too large"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Cannot read ") + path);
    const QByteArray zip = file.read(kMaxArchiveBytes + 1);
    if (zip.size() > kMaxArchiveBytes)
        return fail(QStringLiteral("archive entry too large"));

    QVector<ZipEntry> entries;
    QString err;
    if (!Zip::unpack(zip, entries, &err))
        return fail(err);

    // Refuse DRM before parsing any package file: an encrypted OPF would
    // otherwise be reported as malformed XML instead of a clear DRM error.
    if (!checkEncryption(entries, &err))
        return fail(err);

    const QByteArray containerBytes = Zip::file(entries, QStringLiteral("META-INF/container.xml"));
    if (containerBytes.isEmpty())
        return fail(QStringLiteral("EPUB missing META-INF/container.xml"));
    XNode container;
    if (!parseXml(containerBytes, QStringLiteral("META-INF/container.xml"), &container, &err))
        return fail(err);
    QVector<const XNode *> rootfiles;
    collectDescendants(container, QStringLiteral("rootfile"), &rootfiles);
    QString opfHref;
    for (const XNode *node : rootfiles) {
        const QString mediaType = attr(*node, QStringLiteral("media-type"));
        if (opfHref.isEmpty() || mediaType.compare(QLatin1String("application/oebps-package+xml"), Qt::CaseInsensitive) == 0) {
            opfHref = attr(*node, QStringLiteral("full-path")).trimmed();
            if (mediaType.compare(QLatin1String("application/oebps-package+xml"), Qt::CaseInsensitive) == 0)
                break;
        }
    }
    if (opfHref.isEmpty())
        return fail(QStringLiteral("EPUB container has no rootfile"));
    const ResolvedHref opfRef = resolveHref(QString(), opfHref);
    if (!opfRef.ok())
        return fail(QStringLiteral("EPUB rootfile path is not allowed: ") + opfHref);
    const QString opfPath = opfRef.path;
    const QByteArray opfBytes = Zip::file(entries, opfPath);
    if (opfBytes.isEmpty())
        return fail(QStringLiteral("EPUB package not found: ") + opfPath);
    XNode opf;
    if (!parseXml(opfBytes, opfPath, &opf, &err))
        return fail(err);

    Document loaded = Document::blank();
    loaded.body.clear();

    Builder builder{loaded, entries, warnings};
    PackageInfo package;
    if (!readPackage(opf, opfPath, &package, &builder))
        return fail(builder.error);

    if (!package.title.trimmed().isEmpty())
        loaded.title = package.title.trimmed();
    loaded.author = package.creator.trimmed();

    QHash<QString, QString> classToStyle;
    for (const StyleDef &style : loaded.styles) {
        const QString className = cssClassName(style.name);
        if (!className.isEmpty() && !classToStyle.contains(className))
            classToStyle.insert(className, style.name);
    }

    QString documentLang = package.language.trimmed();
    if (!isBcp47(documentLang))
        documentLang.clear();
    builder.mimeByPath = package.mimeByPath;
    builder.classToStyle = classToStyle;

    for (const ManifestItem &item : package.spine) {
        const QByteArray bytes = Zip::file(entries, item.path);
        if (bytes.isEmpty())
            return fail(QStringLiteral("EPUB spine item is empty or missing: ") + item.path);
        XNode root;
        if (!parseXml(bytes, item.path, &root, &err))
            return fail(err);
        const XNode *body = findChild(root, QStringLiteral("body"));
        if (!body)
            return fail(item.path + QStringLiteral(": no body element"));
        QString lang = documentLang;
        const QString rootLang = attr(root, QStringLiteral("lang")).trimmed();
        if (!rootLang.isEmpty())
            lang = rootLang;
        const QString bodyLang = attr(*body, QStringLiteral("lang")).trimmed();
        if (!bodyLang.isEmpty())
            lang = bodyLang;
        QString dir = attr(root, QStringLiteral("dir")).toLower();
        if (dir != QLatin1String("rtl") && dir != QLatin1String("ltr"))
            dir.clear();
        const QString bodyDir = attr(*body, QStringLiteral("dir")).toLower();
        if (bodyDir == QLatin1String("rtl") || bodyDir == QLatin1String("ltr"))
            dir = bodyDir;

        builder.docPath = item.path;
        if (!builder.walkChildren(body->children, lang, dir))
            return fail(builder.error);
    }

    builder.resolveAll();
    // D1: a U+2029 in a non-note block is a hard line break, not a paragraph
    // separator. The reader keeps U+2029 inside note bodies; normalize the rest.
    loaded.normalizeExternalParaSeps();
    doc = std::move(loaded);
    return true;
}

} // namespace aioffice