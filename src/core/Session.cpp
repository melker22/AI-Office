#include "core/Session.h"

#include "core/Docx.h"
#include "core/Odf.h"
#include "core/Preview.h"
#include "core/Spell.h"
#include "lok/LokEngine.h"

#include <QDateTime>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>
#include <QUuid>
#include <algorithm>

namespace aioffice {

QJsonObject toolResultOk(const QJsonObject &extra)
{
    QJsonObject o = extra;
    o[QStringLiteral("ok")] = true;
    return o;
}

QJsonObject toolResultErr(const QString &error)
{
    QJsonObject o;
    o[QStringLiteral("ok")] = false;
    o[QStringLiteral("error")] = error;
    return o;
}

static StyleDef *styleByName(Document &doc, const QString &name)
{
    for (auto &s : doc.styles) {
        if (s.name == name)
            return &s;
    }
    return nullptr;
}

static const StyleDef *styleByName(const Document &doc, const QString &name)
{
    for (const auto &s : doc.styles) {
        if (s.name == name)
            return &s;
    }
    return nullptr;
}

static TextRun runAtOffset(const Block &b, int offset)
{
    if (b.runs.isEmpty())
        return {};
    const QString plain = b.plainText();
    const int at = qBound(0, offset, qMax(0, plain.size() - 1));
    int pos = 0;
    for (const auto &r : b.runs) {
        if (at < pos + r.text.size() || pos + r.text.size() >= plain.size())
            return r;
        pos += r.text.size();
    }
    return b.runs.last();
}

Session::Session()
{
    doc_ = Document::blank();
    if (!doc_.body.isEmpty()) {
        sel_.anchorId = sel_.focusId = doc_.body.first().id;
        sel_.anchorOff = sel_.focusOff = 0;
    }
    captureBaseline();
}

QJsonObject Session::ok() const
{
    QJsonObject o;
    o[QStringLiteral("ok")] = true;
    o[QStringLiteral("dirty")] = dirty_;
    o[QStringLiteral("path")] = path_;
    o[QStringLiteral("title")] = doc_.title;
    return o;
}

QJsonObject Session::fail(const QString &err) const
{
    return toolResultErr(err);
}

void Session::pushUndo()
{
    if (undoSuppress_ > 0)
        return;
    undo_.push_back(doc_);
    if (undo_.size() > 80)
        undo_.removeFirst();
    redo_.clear();
}

void Session::markChanged()
{
    dirty_ = true;
    if (onChanged_)
        onChanged_();
}

void Session::captureBaseline()
{
    baseline_ = doc_;
}

void Session::stripLeadingEmptyParagraph()
{
    while (!doc_.body.isEmpty()) {
        const Block &b = doc_.body.first();
        if (b.type != QLatin1String("paragraph") || !b.plainText().trimmed().isEmpty()
            || !b.comments.isEmpty())
            break;
        doc_.body.removeFirst();
    }
}

void Session::selectInserted(const QString &id)
{
    const Block *b = doc_.findBlock(id);
    if (!b)
        return;
    sel_.anchorId = sel_.focusId = id;
    sel_.anchorOff = sel_.focusOff = 0;
    if (b->type == QLatin1String("table")) {
        sel_.tableRow = 0;
        sel_.tableCol = 0;
    } else {
        sel_.tableRow = sel_.tableCol = -1;
    }
}

int Session::insertIndex(const QString &afterId, QString *err) const
{
    if (afterId.isEmpty() || afterId == QLatin1String("end"))
        return doc_.body.size();
    if (afterId == QLatin1String("start"))
        return 0;
    const int i = doc_.indexOf(afterId);
    if (i < 0) {
        if (err)
            *err = QStringLiteral("unknown after_id: ") + afterId;
        return -1;
    }
    return i + 1;
}

QJsonArray Session::idList(const QJsonObject &args) const
{
    QJsonArray ids = args.value(QStringLiteral("ids")).toArray();
    if (ids.isEmpty()) {
        const QString id = args.value(QStringLiteral("id")).toString();
        if (!id.isEmpty())
            ids.append(id);
    }
    return ids;
}

QVector<int> Session::indexesFor(const QJsonArray &ids) const
{
    QVector<int> idx;
    for (const auto &v : ids) {
        const int i = doc_.indexOf(v.toString());
        if (i >= 0)
            idx.push_back(i);
    }
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
    return idx;
}

bool Session::saveTo(const QString &path, QString *error)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("aio") || ext == QLatin1String("json") || path.endsWith(QLatin1String(".aio.json"))) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            if (error)
                *error = QStringLiteral("Cannot write ") + path;
            return false;
        }
        f.write(doc_.toCanonicalJson());
        doc_.fileName = QFileInfo(path).fileName();
        return true;
    }
    if (ext == QLatin1String("odt")) {
        if (!saveOdt(doc_, path, error))
            return false;
        doc_.fileName = QFileInfo(path).fileName();
        return true;
    }
    if (ext == QLatin1String("docx")) {
        if (!saveDocx(doc_, path, error))
            return false;
        doc_.fileName = QFileInfo(path).fileName();
        return true;
    }
    if (ext == QLatin1String("doc")) {
        QTemporaryDir tmp;
        const QString odt = tmp.filePath(QStringLiteral("tmp.odt"));
        if (!saveOdt(doc_, odt, error))
            return false;
        return LokEngine::convertTo(odt, path, QStringLiteral("doc:MS Word 97"), error);
    }
    if (ext == QLatin1String("pdf"))
        return exportPdf(QJsonObject{{QStringLiteral("path"), path}})[QStringLiteral("ok")].toBool();
    if (ext == QLatin1String("md") || ext == QLatin1String("markdown") || ext == QLatin1String("txt")
        || ext == QLatin1String("html") || ext == QLatin1String("htm")) {
        QString out;
        for (const auto &b : doc_.body) {
            if (b.type == QLatin1String("footnote") || b.type == QLatin1String("endnote")
                || b.type == QLatin1String("break"))
                continue;
            const QString plain = b.plainText();
            if (ext == QLatin1String("html") || ext == QLatin1String("htm")) {
                if (b.type == QLatin1String("heading"))
                    out += QStringLiteral("<h%1>%2</h%1>\n").arg(qBound(1, b.level, 6)).arg(plain.toHtmlEscaped());
                else if (b.type == QLatin1String("list-item"))
                    out += QStringLiteral("<li>%1</li>\n").arg(plain.toHtmlEscaped());
                else
                    out += QStringLiteral("<p>%1</p>\n").arg(plain.toHtmlEscaped());
            } else if (ext == QLatin1String("md") || ext == QLatin1String("markdown")) {
                if (b.type == QLatin1String("heading"))
                    out += QStringLiteral("%1 %2\n\n").arg(QString(qBound(1, b.level, 6), QLatin1Char('#')), plain);
                else if (b.type == QLatin1String("list-item"))
                    out += QStringLiteral("- %1\n").arg(plain);
                else
                    out += plain + QLatin1String("\n\n");
            } else {
                out += plain + QLatin1Char('\n');
            }
        }
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            if (error)
                *error = QStringLiteral("Cannot write ") + path;
            return false;
        }
        f.write(out.toUtf8());
        doc_.fileName = QFileInfo(path).fileName();
        return true;
    }
    if (error)
        *error = QStringLiteral("Unknown extension: ") + ext;
    return false;
}

bool Session::loadFrom(const QString &path, QString *error)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("aio") || ext == QLatin1String("json") || path.endsWith(QLatin1String(".aio.json"))) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("Cannot read ") + path;
            return false;
        }
        const auto docj = QJsonDocument::fromJson(f.readAll());
        if (!docj.isObject()) {
            if (error)
                *error = QStringLiteral("Invalid JSON document");
            return false;
        }
        doc_ = Document::fromJson(docj.object());
        doc_.fileName = QFileInfo(path).fileName();
        return true;
    }
    if (ext == QLatin1String("odt")) {
        if (!loadOdtFile(path, doc_, error))
            return false;
        doc_.fileName = QFileInfo(path).fileName();
        return true;
    }
    if (ext == QLatin1String("docx")) {
        if (!loadDocxFile(path, doc_, error))
            return false;
        doc_.fileName = QFileInfo(path).fileName();
        return true;
    }
    if (ext == QLatin1String("doc") || ext == QLatin1String("rtf") || ext == QLatin1String("ott")) {
        QTemporaryDir tmp;
        const QString odt = tmp.filePath(QFileInfo(path).completeBaseName() + QStringLiteral(".odt"));
        if (!LokEngine::convertTo(path, odt, QStringLiteral("odt:writer8"), error))
            return false;
        return loadOdtFile(odt, doc_, error);
    }
    if (error)
        *error = QStringLiteral("Unknown extension: ") + ext + QStringLiteral(". Use odt, docx, doc, aio.json.");
    return false;
}

QJsonObject Session::newDocument(const QJsonObject &args)
{
    if (dirty_ && !args.value(QStringLiteral("force")).toBool())
        return fail(QStringLiteral("document is unsaved; pass force=true to discard"));
    pushUndo();
    doc_ = Document::blank(args.value(QStringLiteral("title")).toString());
    path_.clear();
    sel_ = {};
    if (!doc_.body.isEmpty()) {
        sel_.anchorId = sel_.focusId = doc_.body.first().id;
        sel_.anchorOff = sel_.focusOff = 0;
    }
    dirty_ = false;
    markChanged();
    dirty_ = false;
    captureBaseline();
    return ok();
}

QJsonObject Session::openDocument(const QJsonObject &args)
{
    const QString path = args.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
        return fail(QStringLiteral("path is required"));
    QString err;
    Document previous = doc_;
    if (!loadFrom(path, &err))
        return fail(err);
    pushUndo();
    undo_.back() = previous;
    path_ = path;
    dirty_ = false;
    markChanged();
    dirty_ = false;
    captureBaseline();
    auto r = ok();
    r[QStringLiteral("document")] = doc_.toJson();
    return r;
}

QJsonObject Session::saveDocument(const QJsonObject &args)
{
    QString path = args.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
        path = path_;
    if (path.isEmpty())
        return fail(QStringLiteral("path is required (no current file)"));
    if (args.contains(QStringLiteral("title")))
        doc_.title = args.value(QStringLiteral("title")).toString();
    QString err;
    if (!saveTo(path, &err))
        return fail(err);
    path_ = path;
    dirty_ = false;
    markChanged();
    dirty_ = false;
    captureBaseline();
    return ok();
}

QJsonObject Session::exportPdf(const QJsonObject &args)
{
    const QString pdf = args.value(QStringLiteral("path")).toString();
    if (pdf.isEmpty())
        return fail(QStringLiteral("path is required"));
    QString nativeErr;
    const bool pdfa = args.value(QStringLiteral("pdfa")).toBool()
        || args.value(QStringLiteral("pdf_a")).toBool();
    if (exportNativePdf(doc_, pdf, &nativeErr, pdfa)) {
        auto r = ok();
        if (pdfa)
            r[QStringLiteral("pdfa")] = true;
        return r;
    }
    QTemporaryDir tmp;
    const QString odt = tmp.filePath(QStringLiteral("export.odt"));
    QString err;
    if (!saveOdt(doc_, odt, &err))
        return fail(nativeErr.isEmpty() ? err : nativeErr);
    if (!LokEngine::exportPdf(odt, pdf, &err))
        return fail(QStringLiteral("native PDF failed (%1); LibreOffice: %2").arg(nativeErr, err));
    return ok();
}

QJsonObject Session::getDocument(const QJsonObject &)
{
    auto r = ok();
    r[QStringLiteral("document")] = doc_.toJson();
    r[QStringLiteral("words")] = doc_.wordCount();
    r[QStringLiteral("libreoffice")] = LokEngine::available();
    return r;
}

QJsonObject Session::getOutline(const QJsonObject &)
{
    QJsonArray outline;
    for (const auto &b : doc_.body) {
        const bool heading = b.type == QLatin1String("heading");
        const bool structural = heading || b.type == QLatin1String("table") || b.type == QLatin1String("image")
            || b.type == QLatin1String("footnote") || b.type == QLatin1String("endnote")
            || b.type == QLatin1String("toc");
        if (!structural)
            continue;
        QJsonObject n;
        n[QStringLiteral("id")] = b.id;
        n[QStringLiteral("kind")] = b.type;
        n[QStringLiteral("type")] = b.type;
        if (heading)
            n[QStringLiteral("level")] = b.level;
        n[QStringLiteral("text")] = b.plainText();
        outline.append(n);
    }
    auto r = ok();
    r[QStringLiteral("outline")] = outline;
    return r;
}

static Block prepareBlock(Document &doc, Block b)
{
    if (b.type.isEmpty())
        b.type = QStringLiteral("paragraph");
    if (b.id.isEmpty())
        doc.assignId(b);
    else if (doc.findBlock(b.id))
        doc.assignId(b);
    return b;
}

QJsonObject Session::insertBlocks(const QJsonObject &args)
{
    QString err;
    int at = insertIndex(args.value(QStringLiteral("after_id")).toString(QStringLiteral("end")), &err);
    if (at < 0)
        return fail(err);
    pushUndo();
    QJsonArray assigned;
    for (const auto &v : args.value(QStringLiteral("blocks")).toArray()) {
        Block b = prepareBlock(doc_, Block::fromJson(v.toObject()));
        doc_.body.insert(at++, b);
        assigned.append(b.id);
    }
    if (!assigned.isEmpty())
        selectInserted(assigned.last().toString());
    markChanged();
    auto r = ok();
    r[QStringLiteral("ids")] = assigned;
    return r;
}

QJsonObject Session::replaceRange(const QJsonObject &args)
{
    const auto idx = indexesFor(idList(args));
    if (idx.isEmpty())
        return fail(QStringLiteral("no matching ids"));
    pushUndo();
    const int first = idx.first();
    for (int i = idx.size() - 1; i >= 0; --i)
        doc_.body.removeAt(idx[i]);
    int at = first;
    QJsonArray assigned;
    for (const auto &v : args.value(QStringLiteral("blocks")).toArray()) {
        Block b = prepareBlock(doc_, Block::fromJson(v.toObject()));
        doc_.body.insert(at++, b);
        assigned.append(b.id);
    }
    markChanged();
    auto r = ok();
    r[QStringLiteral("ids")] = assigned;
    return r;
}

QJsonObject Session::deleteRange(const QJsonObject &args)
{
    auto idx = indexesFor(idList(args));
    if (idx.isEmpty())
        return fail(QStringLiteral("no matching ids"));
    pushUndo();
    for (int i = idx.size() - 1; i >= 0; --i)
        doc_.body.removeAt(idx[i]);
    if (doc_.body.isEmpty()) {
        Block p;
        p.type = QStringLiteral("paragraph");
        doc_.assignId(p);
        doc_.body.push_back(p);
    }
    markChanged();
    return ok();
}

static void applyRunFormat(TextRun &r, const QJsonObject &args)
{
    applyFormatToRun(r, args);
}

QJsonObject Session::applyCharacterFormat(const QJsonObject &args)
{
    const auto idx = indexesFor(idList(args));
    if (idx.isEmpty())
        return fail(QStringLiteral("no matching ids"));
    pushUndo();
    const int start = args.value(QStringLiteral("start")).toInt(-1);
    const int end = args.value(QStringLiteral("end")).toInt(-1);
    for (int i : idx) {
        auto &b = doc_.body[i];
        if (b.runs.isEmpty())
            b.runs.push_back(TextRun{});
        if (start >= 0 && end >= start && b.type != QLatin1String("table"))
            applyFormatToRange(b.runs, start, end, args);
        else {
            for (auto &r : b.runs)
                applyRunFormat(r, args);
        }
        if (b.type == QLatin1String("table")) {
            for (auto &row : b.table)
                for (auto &cell : row)
                    for (auto &r : cell.runs)
                        applyRunFormat(r, args);
        }
    }
    if (doc_.trackChanges) {
        Revision rev;
        rev.id = QStringLiteral("rev%1").arg(doc_.revisions.size() + 1);
        rev.author = doc_.author.isEmpty() ? QStringLiteral("IA") : doc_.author;
        rev.kind = QStringLiteral("format");
        rev.blockId = doc_.body[idx.first()].id;
        rev.timestamp = QDateTime::currentMSecsSinceEpoch();
        doc_.revisions.push_back(rev);
    }
    markChanged();
    return ok();
}

QJsonObject Session::applyParagraphFormat(const QJsonObject &args)
{
    const auto idx = indexesFor(idList(args));
    if (idx.isEmpty())
        return fail(QStringLiteral("no matching ids"));
    pushUndo();
    for (int i : idx) {
        if (args.contains(QStringLiteral("align")))
            doc_.body[i].align = args.value(QStringLiteral("align")).toString();
        if (args.contains(QStringLiteral("style")))
            doc_.body[i].style = args.value(QStringLiteral("style")).toString();
        if (args.contains(QStringLiteral("indent_left_mm")))
            doc_.body[i].indentLeftMm = args.value(QStringLiteral("indent_left_mm")).toDouble();
        if (args.contains(QStringLiteral("indent_right_mm")))
            doc_.body[i].indentRightMm = args.value(QStringLiteral("indent_right_mm")).toDouble();
        if (args.contains(QStringLiteral("indent_first_mm")))
            doc_.body[i].indentFirstMm = args.value(QStringLiteral("indent_first_mm")).toDouble();
        if (args.contains(QStringLiteral("space_before_pt")))
            doc_.body[i].spaceBeforePt = args.value(QStringLiteral("space_before_pt")).toDouble();
        if (args.contains(QStringLiteral("space_after_pt")))
            doc_.body[i].spaceAfterPt = args.value(QStringLiteral("space_after_pt")).toDouble();
        if (args.contains(QStringLiteral("line_height")))
            doc_.body[i].lineHeight = args.value(QStringLiteral("line_height")).toDouble();
        if (args.contains(QStringLiteral("keep_with_next")))
            doc_.body[i].keepWithNext = args.value(QStringLiteral("keep_with_next")).toBool();
        if (args.contains(QStringLiteral("orphans")))
            doc_.body[i].orphans = args.value(QStringLiteral("orphans")).toInt();
        if (args.contains(QStringLiteral("widows")))
            doc_.body[i].widows = args.value(QStringLiteral("widows")).toInt();
        if (args.contains(QStringLiteral("drop_cap_lines")))
            doc_.body[i].dropCapLines = args.value(QStringLiteral("drop_cap_lines")).toInt();
        if (args.contains(QStringLiteral("border_pt")))
            doc_.body[i].borderPt = args.value(QStringLiteral("border_pt")).toDouble();
        if (args.contains(QStringLiteral("border_color")))
            doc_.body[i].borderColor = args.value(QStringLiteral("border_color")).toString();
        if (args.contains(QStringLiteral("tabs"))) {
            doc_.body[i].tabs.clear();
            for (const auto &t : args.value(QStringLiteral("tabs")).toArray()) {
                TabStop ts;
                ts.posMm = t.toObject().value(QStringLiteral("pos_mm")).toDouble();
                ts.align = t.toObject().value(QStringLiteral("align")).toString(QStringLiteral("left"));
                doc_.body[i].tabs.push_back(ts);
            }
        }
        if (args.contains(QStringLiteral("list_restart")))
            doc_.body[i].listRestart = args.value(QStringLiteral("list_restart")).toBool();
        if (args.contains(QStringLiteral("list_bullet")))
            doc_.body[i].listBullet = args.value(QStringLiteral("list_bullet")).toString();
        if (args.contains(QStringLiteral("lang")))
            doc_.body[i].lang = args.value(QStringLiteral("lang")).toString();
    }
    markChanged();
    return ok();
}

QJsonObject Session::applyStyle(const QJsonObject &args)
{
    const QString style = args.value(QStringLiteral("style")).toString();
    if (style.isEmpty())
        return fail(QStringLiteral("style is required"));
    QJsonObject para = args;
    para[QStringLiteral("style")] = style;
    auto r = applyParagraphFormat(para);
    if (!r.value(QStringLiteral("ok")).toBool())
        return r;
    for (int i : indexesFor(idList(args))) {
        if (style.startsWith(QLatin1String("Heading"))) {
            bool okN = false;
            const int lv = style.section(QLatin1Char(' '), 1, 1).toInt(&okN);
            if (okN) {
                doc_.body[i].type = QStringLiteral("heading");
                doc_.body[i].level = lv;
            }
        } else if (style == QLatin1String("Title")) {
            doc_.body[i].type = QStringLiteral("heading");
            doc_.body[i].level = 1;
            doc_.body[i].style = QStringLiteral("Title");
        }
        if (const StyleDef *st = styleByName(doc_, style)) {
            if (st->kind == QLatin1String("list")) {
                doc_.body[i].type = QStringLiteral("list-item");
                if (!st->listKind.isEmpty())
                    doc_.body[i].listKind = st->listKind;
                if (!st->listBullet.isEmpty())
                    doc_.body[i].listBullet = st->listBullet;
            } else if (st->kind == QLatin1String("frame")) {
                doc_.body[i].type = QStringLiteral("frame");
                if (doc_.body[i].borderPt <= 0)
                    doc_.body[i].borderPt = 0.75;
                if (doc_.body[i].borderColor.isEmpty() && !st->color.isEmpty())
                    doc_.body[i].borderColor = st->color;
            }
        }
    }
    return ok();
}

QJsonObject Session::createStyle(const QJsonObject &args)
{
    const QString name = args.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        return fail(QStringLiteral("name is required"));
    pushUndo();
    StyleDef s = defaultStyle(QStringLiteral("Normal"));
    s.name = name;
    if (args.contains(QStringLiteral("font")))
        s.font = args.value(QStringLiteral("font")).toString();
    if (args.contains(QStringLiteral("size_pt")))
        s.sizePt = args.value(QStringLiteral("size_pt")).toDouble();
    if (args.contains(QStringLiteral("bold")))
        s.bold = args.value(QStringLiteral("bold")).toBool();
    if (args.contains(QStringLiteral("italic")))
        s.italic = args.value(QStringLiteral("italic")).toBool();
    if (args.contains(QStringLiteral("color")))
        s.color = args.value(QStringLiteral("color")).toString();
    if (args.contains(QStringLiteral("align")))
        s.align = args.value(QStringLiteral("align")).toString();
    if (args.contains(QStringLiteral("space_before_pt")))
        s.spaceBeforePt = args.value(QStringLiteral("space_before_pt")).toDouble();
    if (args.contains(QStringLiteral("space_after_pt")))
        s.spaceAfterPt = args.value(QStringLiteral("space_after_pt")).toDouble();
    if (args.contains(QStringLiteral("kind")))
        s.kind = args.value(QStringLiteral("kind")).toString();
    if (args.contains(QStringLiteral("next")))
        s.next = args.value(QStringLiteral("next")).toString();
    if (args.contains(QStringLiteral("list_kind")))
        s.listKind = args.value(QStringLiteral("list_kind")).toString();
    if (args.contains(QStringLiteral("list_bullet")))
        s.listBullet = args.value(QStringLiteral("list_bullet")).toString();
    if (args.contains(QStringLiteral("page_size")))
        s.pageSize = args.value(QStringLiteral("page_size")).toString();
    if (args.contains(QStringLiteral("page_orientation")))
        s.pageOrientation = args.value(QStringLiteral("page_orientation")).toString();
    bool replaced = false;
    for (auto &ex : doc_.styles) {
        if (ex.name == name) {
            ex = s;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        doc_.styles.push_back(s);
    markChanged();
    return ok();
}

QJsonObject Session::setPageLayout(const QJsonObject &args)
{
    const StyleDef *pageStyle = nullptr;
    if (args.contains(QStringLiteral("style"))) {
        pageStyle = styleByName(doc_, args.value(QStringLiteral("style")).toString());
        if (!pageStyle)
            return fail(QStringLiteral("unknown page style"));
    }
    pushUndo();
    if (pageStyle) {
        if (!pageStyle->pageSize.isEmpty())
            doc_.page.size = pageStyle->pageSize;
        if (!pageStyle->pageOrientation.isEmpty())
            doc_.page.orientation = pageStyle->pageOrientation;
    }
    if (args.contains(QStringLiteral("size")))
        doc_.page.size = args.value(QStringLiteral("size")).toString();
    if (args.contains(QStringLiteral("orientation")))
        doc_.page.orientation = args.value(QStringLiteral("orientation")).toString();
    const auto m = args.value(QStringLiteral("margins_mm")).toArray();
    for (int i = 0; i < 4 && i < m.size(); ++i)
        doc_.page.marginsMm[i] = m.at(i).toDouble();
    if (args.contains(QStringLiteral("columns")))
        doc_.page.columns = qBound(1, args.value(QStringLiteral("columns")).toInt(1), 3);
    if (args.contains(QStringLiteral("hyphenate")))
        doc_.page.hyphenate = args.value(QStringLiteral("hyphenate")).toBool();
    if (args.contains(QStringLiteral("gutter_mm")))
        doc_.page.gutterMm = args.value(QStringLiteral("gutter_mm")).toDouble();
    if (args.contains(QStringLiteral("column_gap_mm")))
        doc_.page.columnGapMm = args.value(QStringLiteral("column_gap_mm")).toDouble();
    if (args.contains(QStringLiteral("column_separator")))
        doc_.page.columnSeparator = args.value(QStringLiteral("column_separator")).toBool();
    if (args.contains(QStringLiteral("border_pt")))
        doc_.page.borderPt = args.value(QStringLiteral("border_pt")).toDouble();
    if (args.contains(QStringLiteral("border_color")))
        doc_.page.borderColor = args.value(QStringLiteral("border_color")).toString();
    if (args.contains(QStringLiteral("background")))
        doc_.page.background = args.value(QStringLiteral("background")).toString();
    if (args.contains(QStringLiteral("number_format")))
        doc_.page.numberFormat = args.value(QStringLiteral("number_format")).toString();
    if (args.contains(QStringLiteral("number_start")))
        doc_.page.numberStart = qMax(1, args.value(QStringLiteral("number_start")).toInt(1));
    markChanged();
    return ok();
}

QJsonObject Session::insertTable(const QJsonObject &args)
{
    const int cols = qMax(1, args.value(QStringLiteral("cols")).toInt(3));
    const bool header = args.value(QStringLiteral("header")).toBool(true);
    int rows = qMax(1, args.value(QStringLiteral("rows")).toInt(3));
    const auto heads = args.value(QStringLiteral("header_texts")).toArray();
    const auto cells = args.value(QStringLiteral("cells")).toArray();
    if (header && rows < 2)
        rows = 2;
    Block b;
    b.type = QStringLiteral("table");
    b.headerRow = header;
    for (const auto &w : args.value(QStringLiteral("col_widths_mm")).toArray())
        b.colWidthsMm.push_back(w.toDouble());
    const int dataStart = header ? 1 : 0;
    for (int r = 0; r < rows; ++r) {
        QVector<TableCell> row;
        for (int c = 0; c < cols; ++c) {
            TableCell cell;
            TextRun run;
            if (header && r == 0) {
                run.text = c < heads.size() ? heads.at(c).toString() : QStringLiteral("Col %1").arg(c + 1);
            } else if (!cells.isEmpty()) {
                const int idx = (r - dataStart) * cols + c;
                if (idx >= 0 && idx < cells.size())
                    run.text = cells.at(idx).toString();
            }
            cell.runs.push_back(run);
            row.push_back(cell);
        }
        b.table.push_back(row);
    }
    QJsonObject wrap;
    wrap[QStringLiteral("after_id")] = args.value(QStringLiteral("after_id")).toString(QStringLiteral("end"));
    wrap[QStringLiteral("blocks")] = QJsonArray{b.toJson()};
    return insertBlocks(wrap);
}

QJsonObject Session::insertImage(const QJsonObject &args)
{
    Block b;
    b.type = QStringLiteral("image");
    b.imagePath = args.value(QStringLiteral("path")).toString();
    b.imageWidthMm = args.value(QStringLiteral("width_mm")).toInt(80);
    if (b.imagePath.isEmpty())
        return fail(QStringLiteral("path is required"));
    QFile img(b.imagePath);
    if (img.open(QIODevice::ReadOnly)) {
        b.imageBytes = img.readAll();
        const QString suf = QFileInfo(b.imagePath).suffix().toLower();
        b.imageMime = suf == QLatin1String("jpg") || suf == QLatin1String("jpeg")
            ? QStringLiteral("image/jpeg")
            : QStringLiteral("image/png");
    }
    QJsonObject wrap;
    wrap[QStringLiteral("after_id")] = args.value(QStringLiteral("after_id")).toString(QStringLiteral("end"));
    wrap[QStringLiteral("blocks")] = QJsonArray{b.toJson()};
    return insertBlocks(wrap);
}

QJsonObject Session::insertList(const QJsonObject &args)
{
    const QString kindRaw = args.value(QStringLiteral("kind")).toString(QStringLiteral("bullet"));
    QString kind = QStringLiteral("bullet");
    if (kindRaw == QLatin1String("number") || kindRaw == QLatin1String("decimal"))
        kind = QStringLiteral("decimal");
    else if (kindRaw == QLatin1String("checkbox") || kindRaw == QLatin1String("task"))
        kind = QStringLiteral("checkbox");
    QJsonArray blocks;
    for (const auto &it : args.value(QStringLiteral("items")).toArray()) {
        Block b;
        b.type = QStringLiteral("list-item");
        b.listKind = kind;
        b.level = 1;
        TextRun r;
        if (it.isObject()) {
            r.text = it.toObject().value(QStringLiteral("text")).toString();
            b.checked = it.toObject().value(QStringLiteral("checked")).toBool();
        } else {
            r.text = it.toString();
        }
        b.runs.push_back(r);
        if (args.contains(QStringLiteral("bullet")))
            b.listBullet = args.value(QStringLiteral("bullet")).toString();
        if (blocks.isEmpty() && args.value(QStringLiteral("restart")).toBool())
            b.listRestart = true;
        blocks.append(b.toJson());
    }
    if (blocks.isEmpty())
        return fail(QStringLiteral("items is required"));
    QJsonObject wrap;
    wrap[QStringLiteral("after_id")] = args.value(QStringLiteral("after_id")).toString(QStringLiteral("end"));
    wrap[QStringLiteral("blocks")] = blocks;
    return insertBlocks(wrap);
}

QJsonObject Session::insertBreak(const QJsonObject &args)
{
    Block b;
    b.type = QStringLiteral("break");
    QString kind = args.value(QStringLiteral("kind")).toString(QStringLiteral("page"));
    b.pageRestart = args.value(QStringLiteral("page_restart")).toBool()
        || kind == QLatin1String("page_restart");
    b.pageNumber = args.value(QStringLiteral("page_number")).toInt();
    if (kind == QLatin1String("page_restart"))
        kind = QStringLiteral("page");
    b.breakKind = kind;
    QJsonObject wrap;
    wrap[QStringLiteral("after_id")] = args.value(QStringLiteral("after_id")).toString(QStringLiteral("end"));
    wrap[QStringLiteral("blocks")] = QJsonArray{b.toJson()};
    return insertBlocks(wrap);
}

QJsonObject Session::findReplace(const QJsonObject &args)
{
    const QString find = args.value(QStringLiteral("find")).toString();
    const QString repl = args.value(QStringLiteral("replace")).toString();
    const bool all = args.value(QStringLiteral("all")).toBool(false);
    const bool useRe = args.value(QStringLiteral("regex")).toBool(false);
    if (find.isEmpty())
        return fail(QStringLiteral("find is required"));
    QRegularExpression re;
    if (useRe) {
        re.setPattern(find);
        if (!re.isValid())
            return fail(QStringLiteral("invalid regex"));
    }
    pushUndo();
    int count = 0;
    auto replaceRuns = [&](QVector<TextRun> &runs) {
        for (auto &r : runs) {
            if (useRe) {
                if (!re.match(r.text).hasMatch())
                    continue;
                if (all) {
                    const int n = r.text.count(re);
                    r.text.replace(re, repl);
                    count += n;
                } else {
                    const auto m = re.match(r.text);
                    if (!m.hasMatch())
                        continue;
                    r.text.replace(m.capturedStart(), m.capturedLength(), repl);
                    ++count;
                    return true;
                }
            } else {
                if (!r.text.contains(find))
                    continue;
                if (all) {
                    count += r.text.count(find);
                    r.text.replace(find, repl);
                } else {
                    r.text.replace(r.text.indexOf(find), find.size(), repl);
                    ++count;
                    return true;
                }
            }
        }
        return false;
    };
    for (auto &b : doc_.body) {
        if (replaceRuns(b.runs) && !all)
            break;
        if (!all && count > 0)
            break;
        for (auto &row : b.table) {
            for (auto &cell : row) {
                if (replaceRuns(cell.runs) && !all)
                    goto done;
            }
        }
    }
done:
    if (count == 0)
        undo_.removeLast();
    else
        markChanged();
    auto r = ok();
    r[QStringLiteral("count")] = count;
    return r;
}

QJsonObject Session::undo(const QJsonObject &)
{
    if (undo_.isEmpty())
        return fail(QStringLiteral("nothing to undo"));
    redo_.push_back(doc_);
    doc_ = undo_.takeLast();
    dirty_ = true;
    markChanged();
    return ok();
}

QJsonObject Session::redo(const QJsonObject &)
{
    if (redo_.isEmpty())
        return fail(QStringLiteral("nothing to redo"));
    undo_.push_back(doc_);
    doc_ = redo_.takeLast();
    dirty_ = true;
    markChanged();
    return ok();
}

QJsonObject Session::pagePreview(const QJsonObject &args)
{
    const double zoom = args.value(QStringLiteral("zoom")).toDouble(1.15);
    const QJsonValue pageV = args.value(QStringLiteral("page"));
    const bool hasFrom = args.contains(QStringLiteral("from"));
    const bool hasTo = args.contains(QStringLiteral("to"));
    if (hasFrom || hasTo) {
        DocumentLayout lay;
        lay.relayout(doc_, zoom);
        int from = args.value(QStringLiteral("from")).toInt(1);
        int to = args.value(QStringLiteral("to")).toInt(lay.pages);
        if (from < 1)
            from = 1;
        if (to > lay.pages)
            to = lay.pages;
        if (to < from)
            return fail(QStringLiteral("to must be >= from"));
        QJsonArray pages;
        for (int p = from; p <= to; ++p) {
            const QByteArray png = renderPagePng(doc_, p, zoom);
            QJsonObject o;
            o[QStringLiteral("page")] = p;
            o[QStringLiteral("png_base64")] = QString::fromLatin1(png.toBase64());
            pages.append(o);
        }
        auto r = ok();
        r[QStringLiteral("pages")] = lay.pages;
        r[QStringLiteral("from")] = from;
        r[QStringLiteral("to")] = to;
        r[QStringLiteral("previews")] = pages;
        return r;
    }
    if (pageV.isString() && pageV.toString() == QLatin1String("all")) {
        DocumentLayout lay;
        lay.relayout(doc_, zoom);
        QJsonArray pages;
        for (int p = 1; p <= lay.pages; ++p) {
            const QByteArray png = renderPagePng(doc_, p, zoom);
            QJsonObject o;
            o[QStringLiteral("page")] = p;
            o[QStringLiteral("png_base64")] = QString::fromLatin1(png.toBase64());
            pages.append(o);
        }
        auto r = ok();
        r[QStringLiteral("pages")] = lay.pages;
        r[QStringLiteral("previews")] = pages;
        return r;
    }
    const int page = pageV.toInt(1);
    const auto prev = renderPage(doc_, page, zoom);
    QByteArray png = renderPagePng(doc_, page, zoom);
    auto r = ok();
    r[QStringLiteral("page")] = prev.page;
    r[QStringLiteral("pages")] = prev.pages;
    r[QStringLiteral("png_base64")] = QString::fromLatin1(png.toBase64());
    r[QStringLiteral("width")] = prev.image.width();
    r[QStringLiteral("height")] = prev.image.height();
    return r;
}

QJsonObject Session::integrateHelp(const QJsonObject &)
{
    auto r = ok();
    r[QStringLiteral("message")] = QStringLiteral(
        "Peça para a IA se integrar ao software para aproveitar todo o poder dele.");
    r[QStringLiteral("how")] = QStringLiteral(
        "This app does not ship a model and does not take API keys. Start the GUI (`aioffice`) "
        "or run `aioffice mcp` as an MCP server. Point Cursor/Claude Code/Grok Build at the "
        "binary with args [\"mcp\"]. Then use document_get, insert_blocks, apply_style, "
        "set_page_layout, page_preview, and the other tools in docs/mcp-tools.md. "
        "If the GUI is open, tools edit the live document.");
    r[QStringLiteral("cli")] = QStringLiteral("aioffice mcp");
    r[QStringLiteral("clients")] = QJsonArray{
        QStringLiteral("Cursor"), QStringLiteral("Claude Code"), QStringLiteral("Grok Build"),
        QStringLiteral("Copilot"), QStringLiteral("Gemini"), QStringLiteral("ChatGPT"),
        QStringLiteral("Kimi")};
    r[QStringLiteral("libreoffice")] = LokEngine::available();
    r[QStringLiteral("title")] = doc_.title;
    r[QStringLiteral("words")] = doc_.wordCount();
    r[QStringLiteral("outline")] = getOutline({})[QStringLiteral("outline")];
    return r;
}

Block *Session::tableBlock(const QString &id)
{
    Block *b = doc_.findBlock(id);
    if (b && b->type == QLatin1String("table"))
        return b;
    return nullptr;
}

QVector<TextRun> *Session::runsAt(Block &b, int row, int col)
{
    if (b.type == QLatin1String("table")) {
        if (row < 0 || row >= b.table.size() || col < 0 || col >= b.table[row].size())
            return nullptr;
        return &b.table[row][col].runs;
    }
    return &b.runs;
}

QJsonObject Session::search(const QJsonObject &args)
{
    const QString find = args.value(QStringLiteral("find")).toString();
    const bool forward = args.value(QStringLiteral("forward")).toBool(true);
    const bool useRe = args.value(QStringLiteral("regex")).toBool(false);
    const bool hasFmt = args.contains(QStringLiteral("bold")) || args.contains(QStringLiteral("italic"))
        || args.contains(QStringLiteral("underline")) || args.contains(QStringLiteral("strike"))
        || args.contains(QStringLiteral("font")) || args.contains(QStringLiteral("highlight"))
        || args.contains(QStringLiteral("char_style")) || args.contains(QStringLiteral("size_pt"))
        || args.contains(QStringLiteral("lang")) || args.contains(QStringLiteral("kerning"));
    if (find.isEmpty() && !hasFmt)
        return fail(QStringLiteral("find or a format filter is required"));
    auto runMatches = [&](const TextRun &r, const Block &b) {
        if (args.contains(QStringLiteral("bold")) && r.bold != args.value(QStringLiteral("bold")).toBool())
            return false;
        if (args.contains(QStringLiteral("italic")) && r.italic != args.value(QStringLiteral("italic")).toBool())
            return false;
        if (args.contains(QStringLiteral("underline")) && r.underline != args.value(QStringLiteral("underline")).toBool())
            return false;
        if (args.contains(QStringLiteral("strike")) && r.strike != args.value(QStringLiteral("strike")).toBool())
            return false;
        if (args.contains(QStringLiteral("font"))
            && r.font.compare(args.value(QStringLiteral("font")).toString(), Qt::CaseInsensitive) != 0)
            return false;
        if (args.contains(QStringLiteral("highlight"))
            && r.highlight.compare(args.value(QStringLiteral("highlight")).toString(), Qt::CaseInsensitive) != 0)
            return false;
        if (args.contains(QStringLiteral("char_style"))
            && r.charStyle != args.value(QStringLiteral("char_style")).toString())
            return false;
        if (args.contains(QStringLiteral("size_pt"))
            && !qFuzzyCompare(r.sizePt + 1.0, args.value(QStringLiteral("size_pt")).toDouble() + 1.0))
            return false;
        if (args.contains(QStringLiteral("lang"))) {
            const QString lang = r.lang.isEmpty() ? b.lang : r.lang;
            if (lang.compare(args.value(QStringLiteral("lang")).toString(), Qt::CaseInsensitive) != 0)
                return false;
        }
        if (args.contains(QStringLiteral("kerning"))
            && !qFuzzyCompare(r.kerning + 1.0, args.value(QStringLiteral("kerning")).toDouble() + 1.0))
            return false;
        return true;
    };
    auto rangeMatches = [&](const Block &b, int start, int end) {
        if (!hasFmt)
            return true;
        int pos = 0;
        for (const auto &r : b.runs) {
            const int a = pos;
            const int c = pos + r.text.size();
            if (c > start && a < end && !runMatches(r, b))
                return false;
            pos = c;
        }
        return true;
    };
    QRegularExpression re;
    if (useRe && !find.isEmpty()) {
        re.setPattern(find);
        re.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid())
            return fail(QStringLiteral("invalid regex"));
    }
    QJsonArray hits;
    for (const auto &b : doc_.body) {
        if (find.isEmpty()) {
            int pos = 0;
            for (const auto &r : b.runs) {
                if (runMatches(r, b) && !r.text.isEmpty()) {
                    QJsonObject h;
                    h[QStringLiteral("id")] = b.id;
                    h[QStringLiteral("start")] = pos;
                    h[QStringLiteral("end")] = pos + r.text.size();
                    hits.append(h);
                }
                pos += r.text.size();
            }
            continue;
        }
        const QString t = b.plainText();
        if (useRe) {
            auto it = re.globalMatch(t);
            while (it.hasNext()) {
                const auto m = it.next();
                const int s = int(m.capturedStart());
                const int e = int(m.capturedEnd());
                if (!rangeMatches(b, s, e))
                    continue;
                QJsonObject h;
                h[QStringLiteral("id")] = b.id;
                h[QStringLiteral("start")] = s;
                h[QStringLiteral("end")] = e;
                hits.append(h);
            }
        } else {
            int from = 0;
            while (true) {
                const int i = t.indexOf(find, from, Qt::CaseInsensitive);
                if (i < 0)
                    break;
                const int e = i + find.size();
                if (rangeMatches(b, i, e)) {
                    QJsonObject h;
                    h[QStringLiteral("id")] = b.id;
                    h[QStringLiteral("start")] = i;
                    h[QStringLiteral("end")] = e;
                    hits.append(h);
                }
                from = i + qMax(1, find.size());
            }
        }
    }
    if (!forward) {
        QJsonArray rev;
        for (int i = hits.size() - 1; i >= 0; --i)
            rev.append(hits.at(i));
        hits = rev;
    }
    auto r = ok();
    r[QStringLiteral("hits")] = hits;
    return r;
}

QJsonObject Session::setCell(const QJsonObject &args)
{
    Block *b = tableBlock(args.value(QStringLiteral("id")).toString());
    if (!b)
        return fail(QStringLiteral("table id not found"));
    const int row = args.value(QStringLiteral("row")).toInt();
    const int col = args.value(QStringLiteral("col")).toInt();
    if (row < 0 || row >= b->table.size() || col < 0 || col >= b->table[row].size())
        return fail(QStringLiteral("cell out of range"));
    pushUndo();
    TableCell &cell = b->table[row][col];
    auto fillRuns = [&](QVector<TextRun> &runs) {
        runs.clear();
        if (args.contains(QStringLiteral("runs"))) {
            for (const auto &v : args.value(QStringLiteral("runs")).toArray())
                runs.push_back(TextRun::fromJson(v.toObject()));
        } else {
            TextRun r;
            r.text = args.value(QStringLiteral("text")).toString();
            runs.push_back(r);
        }
    };
    if (args.contains(QStringLiteral("nested_row"))) {
        const int nr = args.value(QStringLiteral("nested_row")).toInt();
        const int nc = args.value(QStringLiteral("nested_col")).toInt();
        if (nr < 0 || nr >= cell.nested.size() || nc < 0 || nc >= cell.nested[nr].size()) {
            undo_.removeLast();
            return fail(QStringLiteral("nested cell out of range"));
        }
        fillRuns(cell.nested[nr][nc].runs);
    } else {
        fillRuns(cell.runs);
    }
    markChanged();
    return ok();
}

QJsonObject Session::insertRow(const QJsonObject &args)
{
    Block *b = tableBlock(args.value(QStringLiteral("id")).toString());
    if (!b)
        return fail(QStringLiteral("table id not found"));
    const int cols = b->table.isEmpty() ? 1 : b->table[0].size();
    int at = args.value(QStringLiteral("row")).toInt(b->table.size());
    at = qBound(0, at, b->table.size());
    pushUndo();
    QVector<TableCell> row;
    for (int c = 0; c < cols; ++c)
        row.push_back(TableCell{});
    b->table.insert(at, row);
    markChanged();
    return ok();
}

QJsonObject Session::insertCol(const QJsonObject &args)
{
    Block *b = tableBlock(args.value(QStringLiteral("id")).toString());
    if (!b)
        return fail(QStringLiteral("table id not found"));
    int at = args.value(QStringLiteral("col")).toInt();
    pushUndo();
    for (auto &row : b->table) {
        int c = args.contains(QStringLiteral("col")) ? qBound(0, at, row.size()) : row.size();
        row.insert(c, TableCell{});
    }
    markChanged();
    return ok();
}

QJsonObject Session::deleteRow(const QJsonObject &args)
{
    Block *b = tableBlock(args.value(QStringLiteral("id")).toString());
    if (!b)
        return fail(QStringLiteral("table id not found"));
    const int row = args.value(QStringLiteral("row")).toInt();
    if (row < 0 || row >= b->table.size())
        return fail(QStringLiteral("row out of range"));
    pushUndo();
    b->table.removeAt(row);
    markChanged();
    return ok();
}

QJsonObject Session::deleteCol(const QJsonObject &args)
{
    Block *b = tableBlock(args.value(QStringLiteral("id")).toString());
    if (!b)
        return fail(QStringLiteral("table id not found"));
    const int col = args.value(QStringLiteral("col")).toInt();
    pushUndo();
    for (auto &row : b->table) {
        if (col >= 0 && col < row.size())
            row.removeAt(col);
    }
    markChanged();
    return ok();
}

QJsonObject Session::selectionGet(const QJsonObject &)
{
    auto r = ok();
    r[QStringLiteral("selection")] = sel_.toJson();
    return r;
}

QJsonObject Session::setSelectionTool(const QJsonObject &args)
{
    sel_ = Selection::fromJson(args);
    if (sel_.focusId.isEmpty())
        sel_.focusId = sel_.anchorId;
    auto r = ok();
    r[QStringLiteral("selection")] = sel_.toJson();
    markChanged();
    dirty_ = dirty_;
    return r;
}

QJsonObject Session::insertAtCaret(const QJsonObject &args)
{
    const QString text = args.value(QStringLiteral("text")).toString();
    QString id = args.value(QStringLiteral("id")).toString(sel_.focusId);
    int off = args.value(QStringLiteral("offset")).toInt(sel_.focusOff);
    if (id.isEmpty() && !doc_.body.isEmpty()) {
        id = doc_.body.last().id;
        off = doc_.body.last().plainText().size();
    }
    if (id.isEmpty())
        return fail(QStringLiteral("no caret"));
    const bool hasRange = !sel_.anchorId.isEmpty() && sel_.anchorId == id
        && sel_.focusId == id && sel_.anchorOff != sel_.focusOff;
    const int startOff = hasRange ? qMin(sel_.anchorOff, sel_.focusOff) : off;
    const int endOff = hasRange ? qMax(sel_.anchorOff, sel_.focusOff) : off;
    pushUndo();
    Block *b = doc_.findBlock(id);
    auto *runs = b ? runsAt(*b, sel_.tableRow, sel_.tableCol) : nullptr;
    if (!runs) {
        undo_.removeLast();
        return fail(QStringLiteral("cannot insert"));
    }
    TextRun ins;
    ins.text = text;
    *runs = spliceRuns(*runs, startOff, endOff, ins);
    sel_.focusOff = sel_.anchorOff = startOff + text.size();
    sel_.focusId = sel_.anchorId = id;
    markChanged();
    return ok();
}

bool Session::insertTextAt(const QString &id, int offset, const QString &text, int row, int col)
{
    Block *b = doc_.findBlock(id);
    if (!b)
        return false;
    auto *runs = runsAt(*b, row, col);
    if (!runs)
        return false;
    TextRun ins;
    ins.text = text;
    *runs = spliceRuns(*runs, offset, offset, ins);
    if (doc_.trackChanges) {
        Revision rev;
        rev.id = QStringLiteral("rev") + QString::number(doc_.nextCounter(QStringLiteral("rev")));
        // nextCounter only scans body ids; use revisions size
        rev.id = QStringLiteral("rev%1").arg(doc_.revisions.size() + 1);
        rev.author = doc_.author.isEmpty() ? QStringLiteral("IA") : doc_.author;
        rev.kind = QStringLiteral("insert");
        rev.blockId = id;
        rev.start = offset;
        rev.end = offset + text.size();
        rev.text = text;
        rev.timestamp = QDateTime::currentMSecsSinceEpoch();
        doc_.revisions.push_back(rev);
    }
    return true;
}

bool Session::deleteBackward(const QString &id, int offset, int row, int col)
{
    Block *b = doc_.findBlock(id);
    if (!b)
        return false;
    if (offset > 0) {
        auto *runs = runsAt(*b, row, col);
        if (!runs)
            return false;
        *runs = spliceRuns(*runs, offset - 1, offset, TextRun{});
        if (doc_.trackChanges) {
            Revision rev;
            rev.id = QStringLiteral("rev%1").arg(doc_.revisions.size() + 1);
            rev.author = doc_.author.isEmpty() ? QStringLiteral("IA") : doc_.author;
            rev.kind = QStringLiteral("delete");
            rev.blockId = id;
            rev.start = offset - 1;
            rev.end = offset;
            rev.text = QStringLiteral("?");
            rev.timestamp = QDateTime::currentMSecsSinceEpoch();
            doc_.revisions.push_back(rev);
        }
        return true;
    }
    if (row >= 0)
        return false;
    const int idx = doc_.indexOf(id);
    if (idx <= 0)
        return false;
    Block &prev = doc_.body[idx - 1];
    Block &cur = doc_.body[idx];
    if (prev.type == QLatin1String("table") || prev.type == QLatin1String("image")
        || prev.type == QLatin1String("break") || cur.type == QLatin1String("table")
        || cur.type == QLatin1String("image") || cur.type == QLatin1String("break"))
        return false;
    const int joinOff = prev.plainText().size();
    for (const auto &r : cur.runs)
        prev.runs.push_back(r);
    doc_.body.removeAt(idx);
    sel_.anchorId = sel_.focusId = prev.id;
    sel_.anchorOff = sel_.focusOff = joinOff;
    sel_.tableRow = sel_.tableCol = -1;
    return true;
}

bool Session::splitBlock(const QString &id, int offset)
{
    const int idx = doc_.indexOf(id);
    if (idx < 0)
        return false;
    Block &b = doc_.body[idx];
    if (b.type == QLatin1String("table") || b.type == QLatin1String("image") || b.type == QLatin1String("break"))
        return false;
    const QString plain = b.plainText();
    const int off = qBound(0, offset, plain.size());
    Block rest;
    rest.type = b.type == QLatin1String("heading") ? QStringLiteral("paragraph") : b.type;
    rest.style = QStringLiteral("Normal");
    if (const StyleDef *st = styleByName(doc_, b.style); st && !st->next.isEmpty()) {
        rest.style = st->next;
        if (rest.style.startsWith(QLatin1String("Heading")) || rest.style == QLatin1String("Title")) {
            rest.type = QStringLiteral("heading");
            if (rest.style == QLatin1String("Title"))
                rest.level = 1;
            else {
                bool okN = false;
                const int lv = rest.style.section(QLatin1Char(' '), 1, 1).toInt(&okN);
                rest.level = okN ? lv : 1;
            }
        }
    }
    rest.listKind = b.listKind;
    rest.level = rest.type == QLatin1String("heading") ? rest.level : b.level;
    rest.runs = splitRuns(b.runs, off, plain.size());
    b.runs = splitRuns(b.runs, 0, off);
    if (b.runs.isEmpty())
        b.runs.push_back(TextRun{});
    if (rest.runs.isEmpty())
        rest.runs.push_back(TextRun{});
    doc_.assignId(rest);
    doc_.body.insert(idx + 1, rest);
    sel_.anchorId = sel_.focusId = rest.id;
    sel_.anchorOff = sel_.focusOff = 0;
    return true;
}

QJsonObject Session::moveBlocks(const QJsonObject &args)
{
    auto idx = indexesFor(idList(args));
    if (idx.isEmpty())
        return fail(QStringLiteral("no matching ids"));
    QString err;
    int at = insertIndex(args.value(QStringLiteral("after_id")).toString(QStringLiteral("end")), &err);
    if (at < 0)
        return fail(err);
    pushUndo();
    QVector<Block> moved;
    for (int i = idx.size() - 1; i >= 0; --i) {
        moved.push_front(doc_.body[idx[i]]);
        doc_.body.removeAt(idx[i]);
        if (idx[i] < at)
            --at;
    }
    for (const auto &b : moved)
        doc_.body.insert(at++, b);
    markChanged();
    return ok();
}

QJsonObject Session::duplicateBlocks(const QJsonObject &args)
{
    auto idx = indexesFor(idList(args));
    if (idx.isEmpty())
        return fail(QStringLiteral("no matching ids"));
    QString err;
    int at = insertIndex(args.value(QStringLiteral("after_id")).toString(QStringLiteral("end")), &err);
    if (at < 0)
        return fail(err);
    pushUndo();
    QJsonArray ids;
    for (int i : idx) {
        Block copy = doc_.body[i];
        copy.id.clear();
        doc_.assignId(copy);
        doc_.body.insert(at++, copy);
        ids.append(copy.id);
    }
    markChanged();
    auto r = ok();
    r[QStringLiteral("ids")] = ids;
    return r;
}

QJsonObject Session::commentAdd(const QJsonObject &args)
{
    auto idx = indexesFor(idList(args));
    if (idx.isEmpty())
        return fail(QStringLiteral("id not found"));
    pushUndo();
    QJsonArray cids;
    const QString text = args.value(QStringLiteral("text")).toString();
    const QString author = args.value(QStringLiteral("author")).toString(QStringLiteral("IA"));
    for (int i : idx) {
        Comment c;
        c.id = QStringLiteral("c") + QUuid::createUuid().toString(QUuid::Id128).left(8);
        c.author = author;
        c.text = text;
        doc_.body[i].comments.push_back(c);
        cids.append(c.id);
    }
    markChanged();
    auto r = ok();
    r[QStringLiteral("comment_id")] = cids.first().toString();
    r[QStringLiteral("comment_ids")] = cids;
    return r;
}

QJsonObject Session::commentResolve(const QJsonObject &args)
{
    const QString cid = args.value(QStringLiteral("comment_id")).toString();
    pushUndo();
    for (auto &b : doc_.body) {
        for (auto &c : b.comments) {
            if (c.id == cid) {
                c.resolved = true;
                markChanged();
                return ok();
            }
        }
    }
    undo_.removeLast();
    return fail(QStringLiteral("comment not found"));
}

QJsonObject Session::documentSetTitle(const QJsonObject &args)
{
    pushUndo();
    if (args.contains(QStringLiteral("title")))
        doc_.title = args.value(QStringLiteral("title")).toString();
    if (args.contains(QStringLiteral("author")))
        doc_.author = args.value(QStringLiteral("author")).toString();
    markChanged();
    return ok();
}

QJsonObject Session::setHeader(const QJsonObject &args)
{
    pushUndo();
    HeaderFooter *hf = &doc_.header;
    if (args.value(QStringLiteral("first")).toBool())
        hf = &doc_.headerFirst;
    else if (args.value(QStringLiteral("even")).toBool())
        hf = &doc_.headerEven;
    if (args.contains(QStringLiteral("text")))
        hf->text = args.value(QStringLiteral("text")).toString();
    if (args.contains(QStringLiteral("page_numbers")))
        hf->pageNumbers = args.value(QStringLiteral("page_numbers")).toBool();
    markChanged();
    return ok();
}

QJsonObject Session::setFooter(const QJsonObject &args)
{
    pushUndo();
    HeaderFooter *hf = &doc_.footer;
    if (args.value(QStringLiteral("first")).toBool())
        hf = &doc_.footerFirst;
    else if (args.value(QStringLiteral("even")).toBool())
        hf = &doc_.footerEven;
    if (args.contains(QStringLiteral("text")))
        hf->text = args.value(QStringLiteral("text")).toString();
    if (args.contains(QStringLiteral("page_numbers")))
        hf->pageNumbers = args.value(QStringLiteral("page_numbers")).toBool();
    markChanged();
    return ok();
}

QJsonObject Session::documentDiff(const QJsonObject &)
{
    QJsonArray added, removed, changed, moved;
    const Document &prev = baseline_;
    QSet<QString> oldIds, newIds;
    QMap<QString, QString> oldText, newText;
    QMap<QString, int> oldPos, newPos;
    for (int i = 0; i < prev.body.size(); ++i) {
        const auto &b = prev.body[i];
        oldIds.insert(b.id);
        oldText[b.id] = b.plainText();
        oldPos[b.id] = i;
    }
    for (int i = 0; i < doc_.body.size(); ++i) {
        const auto &b = doc_.body[i];
        newIds.insert(b.id);
        newText[b.id] = b.plainText();
        newPos[b.id] = i;
        if (!oldIds.contains(b.id))
            added.append(b.id);
        else if (oldText[b.id] != b.plainText())
            changed.append(b.id);
        else if (oldPos.value(b.id) != i)
            moved.append(b.id);
    }
    for (const auto &id : oldIds) {
        if (!newIds.contains(id))
            removed.append(id);
    }
    auto r = ok();
    r[QStringLiteral("added")] = added;
    r[QStringLiteral("removed")] = removed;
    r[QStringLiteral("changed")] = changed;
    r[QStringLiteral("moved")] = moved;
    return r;
}

static void flushInlineRun(QVector<TextRun> &out, TextRun &cur)
{
    if (cur.text.isEmpty())
        return;
    out.push_back(cur);
    cur.text.clear();
}

static QVector<TextRun> parseInlineMarkdown(const QString &s)
{
    QVector<TextRun> out;
    TextRun cur;
    const int n = s.size();
    int i = 0;
    auto same = [&](int at, const QString &tok) {
        return s.mid(at, tok.size()) == tok;
    };
    while (i < n) {
        if (same(i, QStringLiteral("**"))) {
            const int close = s.indexOf(QStringLiteral("**"), i + 2);
            if (close > i + 1) {
                flushInlineRun(out, cur);
                TextRun r = cur;
                r.bold = true;
                r.text = s.mid(i + 2, close - (i + 2));
                if (!r.text.isEmpty())
                    out.push_back(r);
                i = close + 2;
                continue;
            }
        }
        if (s[i] == QLatin1Char('*') && (i + 1 >= n || s[i + 1] != QLatin1Char(' '))) {
            const int close = s.indexOf(QLatin1Char('*'), i + 1);
            if (close > i + 1 && (close + 1 >= n || s[close + 1] != QLatin1Char('*'))) {
                flushInlineRun(out, cur);
                TextRun r = cur;
                r.italic = true;
                r.text = s.mid(i + 1, close - (i + 1));
                if (!r.text.isEmpty())
                    out.push_back(r);
                i = close + 1;
                continue;
            }
        }
        if (s[i] == QLatin1Char('[')) {
            const int rb = s.indexOf(QLatin1Char(']'), i + 1);
            if (rb > i && rb + 1 < n && s[rb + 1] == QLatin1Char('(')) {
                const int rp = s.indexOf(QLatin1Char(')'), rb + 2);
                if (rp > rb) {
                    flushInlineRun(out, cur);
                    TextRun r = cur;
                    r.text = s.mid(i + 1, rb - (i + 1));
                    r.link = s.mid(rb + 2, rp - (rb + 2));
                    r.underline = true;
                    r.color = QStringLiteral("#B5651D");
                    if (!r.text.isEmpty())
                        out.push_back(r);
                    i = rp + 1;
                    continue;
                }
            }
        }
        cur.text += s[i];
        ++i;
    }
    flushInlineRun(out, cur);
    if (out.isEmpty())
        out.push_back(TextRun{});
    return out;
}

static QString applyTemplateFields(QString md, const QJsonObject &fields)
{
    for (auto it = fields.begin(); it != fields.end(); ++it)
        md.replace(QStringLiteral("{{") + it.key() + QStringLiteral("}}"), it.value().toString());
    return md;
}

static QVector<Block> blocksFromMarkdown(const QString &md)
{
    QVector<Block> out;
    const auto lines = md.split(QLatin1Char('\n'));
    QVector<QVector<TableCell>> table;
    auto flushTable = [&]() {
        if (table.isEmpty())
            return;
        Block t;
        t.type = QStringLiteral("table");
        t.headerRow = table.size() > 1;
        t.table = table;
        out.push_back(t);
        table.clear();
    };
    const QRegularExpression decimalRe(QStringLiteral("^(\\d+)\\.\\s+"));
    for (QString raw : lines) {
        if (raw.startsWith(QLatin1Char('|'))) {
            QVector<TableCell> row;
            const auto parts = raw.split(QLatin1Char('|'), Qt::SkipEmptyParts);
            bool sep = true;
            for (QString p : parts) {
                p = p.trimmed();
                if (!p.contains(QRegularExpression(QStringLiteral("^[\\-: ]+$"))))
                    sep = false;
                TableCell c;
                c.runs = parseInlineMarkdown(p);
                row.push_back(c);
            }
            if (!sep)
                table.push_back(row);
            continue;
        }
        flushTable();
        int pos = 0;
        int indent = 0;
        while (pos < raw.size() && (raw[pos] == QLatin1Char(' ') || raw[pos] == QLatin1Char('\t'))) {
            indent += raw[pos] == QLatin1Char('\t') ? 2 : 1;
            ++pos;
        }
        QString line = raw.mid(pos);
        Block b;
        if (line.startsWith(QLatin1String("### "))) {
            b.type = QStringLiteral("heading");
            b.level = 3;
            b.style = QStringLiteral("Heading 3");
            line = line.mid(4);
        } else if (line.startsWith(QLatin1String("## "))) {
            b.type = QStringLiteral("heading");
            b.level = 2;
            b.style = QStringLiteral("Heading 2");
            line = line.mid(3);
        } else if (line.startsWith(QLatin1String("# "))) {
            b.type = QStringLiteral("heading");
            b.level = 1;
            b.style = QStringLiteral("Heading 1");
            line = line.mid(2);
        } else if (line.startsWith(QLatin1String("- ")) || line.startsWith(QLatin1String("* "))) {
            b.type = QStringLiteral("list-item");
            b.listKind = QStringLiteral("bullet");
            b.level = qBound(1, indent / 2 + 1, 8);
            line = line.mid(2);
        } else if (auto m = decimalRe.match(line); m.hasMatch()) {
            b.type = QStringLiteral("list-item");
            b.listKind = QStringLiteral("decimal");
            b.level = qBound(1, indent / 2 + 1, 8);
            line = line.mid(m.capturedLength());
        } else {
            b.type = QStringLiteral("paragraph");
            b.style = QStringLiteral("Normal");
        }
        b.runs = parseInlineMarkdown(line);
        out.push_back(b);
    }
    flushTable();
    return out;
}

static QVector<Block> blocksFromHtml(const QString &html)
{
    QTextDocument td;
    td.setHtml(html);
    QVector<Block> out;
    for (QTextBlock tb = td.begin(); tb.isValid(); tb = tb.next()) {
        Block b;
        b.type = QStringLiteral("paragraph");
        b.style = QStringLiteral("Normal");
        const int hl = tb.blockFormat().headingLevel();
        if (hl >= 1 && hl <= 3) {
            b.type = QStringLiteral("heading");
            b.level = hl;
            b.style = QStringLiteral("Heading %1").arg(hl);
        }
        if (QTextList *lst = tb.textList()) {
            b.type = QStringLiteral("list-item");
            b.level = qBound(1, lst->format().indent(), 8);
            const auto st = lst->format().style();
            b.listKind = (st == QTextListFormat::ListDecimal || st == QTextListFormat::ListUpperRoman
                          || st == QTextListFormat::ListLowerRoman || st == QTextListFormat::ListUpperAlpha
                          || st == QTextListFormat::ListLowerAlpha)
                ? QStringLiteral("decimal")
                : QStringLiteral("bullet");
        }
        for (auto it = tb.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            QString t = frag.text();
            t.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
            t.remove(QChar::ObjectReplacementCharacter);
            if (t.isEmpty())
                continue;
            TextRun r;
            r.text = t;
            r.bold = frag.charFormat().fontWeight() >= QFont::DemiBold;
            r.italic = frag.charFormat().fontItalic();
            r.underline = frag.charFormat().fontUnderline();
            if (frag.charFormat().isAnchor())
                r.link = frag.charFormat().anchorHref();
            b.runs.push_back(r);
        }
        if (b.runs.isEmpty() && out.isEmpty())
            continue;
        if (b.runs.isEmpty())
            b.runs.push_back(TextRun{});
        out.push_back(b);
    }
    return out;
}

QJsonObject Session::importMarkdown(const QJsonObject &args)
{
    const QString md = args.value(QStringLiteral("markdown")).toString();
    if (md.isEmpty())
        return fail(QStringLiteral("markdown is required"));
    auto blocks = blocksFromMarkdown(md);
    QJsonArray arr;
    for (auto &b : blocks)
        arr.append(b.toJson());
    QJsonObject wrap;
    wrap[QStringLiteral("after_id")] = args.value(QStringLiteral("after_id")).toString(QStringLiteral("end"));
    wrap[QStringLiteral("blocks")] = arr;
    auto r = insertBlocks(wrap);
    if (r.value(QStringLiteral("ok")).toBool()) {
        stripLeadingEmptyParagraph();
        markChanged();
    }
    return r;
}

QJsonObject Session::documentFromTemplate(const QJsonObject &args)
{
    QString kind = args.value(QStringLiteral("template")).toString();
    if (kind.isEmpty())
        kind = args.value(QStringLiteral("name")).toString();
    QString title;
    QString md;
    QJsonObject defaults;
    if (kind == QLatin1String("carta")) {
        title = QStringLiteral("Carta");
        md = QStringLiteral(
            "# Carta\n\n**{{cidade}}**, {{data}}\n\nPrezado(a) **{{nome}}**,\n\n{{corpo}}\n\nAtenciosamente,\n\n{{remetente}}");
        defaults = QJsonObject{{QStringLiteral("cidade"), QStringLiteral("Cidade")},
                               {QStringLiteral("data"), QStringLiteral("data")},
                               {QStringLiteral("nome"), QStringLiteral("Nome")},
                               {QStringLiteral("corpo"), QStringLiteral("Corpo da carta.")},
                               {QStringLiteral("remetente"), QStringLiteral("Remetente")}};
    } else if (kind == QLatin1String("ata")) {
        title = QStringLiteral("Ata");
        md = QStringLiteral(
            "# Ata\n\n**{{orgao}}** — {{data}}\n\n## Pauta\n\n1. Abertura\n2. Encerramento\n\n## Deliberações\n\n- {{pauta}}");
        defaults = QJsonObject{{QStringLiteral("orgao"), QStringLiteral("Órgão")},
                               {QStringLiteral("data"), QStringLiteral("data")},
                               {QStringLiteral("pauta"), QStringLiteral("Item em discussão")}};
    } else if (kind == QLatin1String("contrato")) {
        title = QStringLiteral("Contrato");
        md = QStringLiteral(
            "# Contrato\n\n**LOCADOR:** {{locador}}\n\n**LOCATÁRIO:** {{locatario}}\n\n## Cláusula 1\n\n{{clausula}}");
        defaults = QJsonObject{{QStringLiteral("locador"), QStringLiteral("[nome]")},
                               {QStringLiteral("locatario"), QStringLiteral("[nome]")},
                               {QStringLiteral("clausula"), QStringLiteral("Objeto do contrato.")}};
    } else if (kind == QLatin1String("envelope") || kind == QLatin1String("envelope-dl")) {
        title = QStringLiteral("Envelope");
        md = QStringLiteral("{{remetente}}\n\n\n{{destinatario}}\n{{morada}}\n{{codigo}} {{cidade}}");
        defaults = QJsonObject{{QStringLiteral("remetente"), QStringLiteral("Remetente")},
                               {QStringLiteral("destinatario"), QStringLiteral("Destinatário")},
                               {QStringLiteral("morada"), QStringLiteral("Morada")},
                               {QStringLiteral("codigo"), QStringLiteral("0000-000")},
                               {QStringLiteral("cidade"), QStringLiteral("Cidade")}};
    } else {
        return fail(QStringLiteral("template must be carta, ata, contrato, or envelope"));
    }
    if (dirty_ && !args.value(QStringLiteral("force")).toBool())
        return fail(QStringLiteral("document is unsaved; pass force=true to discard"));
    const auto fieldsIn = args.value(QStringLiteral("fields")).toObject();
    for (auto it = fieldsIn.begin(); it != fieldsIn.end(); ++it)
        defaults[it.key()] = it.value();
    md = applyTemplateFields(md, defaults);
    pushUndo();
    path_.clear();
    doc_ = Document::blank(title);
    doc_.body.clear();
    auto blocks = blocksFromMarkdown(md);
    for (auto &b : blocks) {
        doc_.assignId(b);
        doc_.body.push_back(b);
    }
    if (doc_.body.isEmpty()) {
        Block p;
        p.type = QStringLiteral("paragraph");
        doc_.assignId(p);
        doc_.body.push_back(p);
    }
    if (kind == QLatin1String("envelope") || kind == QLatin1String("envelope-dl")) {
        doc_.page.size = QStringLiteral("DL");
        doc_.page.orientation = QStringLiteral("landscape");
        doc_.page.marginsMm[0] = 12;
        doc_.page.marginsMm[1] = 12;
        doc_.page.marginsMm[2] = 12;
        doc_.page.marginsMm[3] = 12;
    }
    sel_ = {};
    if (!doc_.body.isEmpty()) {
        sel_.anchorId = sel_.focusId = doc_.body.first().id;
        sel_.anchorOff = sel_.focusOff = 0;
    }
    markChanged();
    captureBaseline();
    return ok();
}

QJsonObject Session::linkSet(const QJsonObject &args)
{
    const QString url = args.value(QStringLiteral("url")).toString();
    QJsonObject fmt = args;
    fmt[QStringLiteral("link")] = url;
    if (!fmt.contains(QStringLiteral("ids"))) {
        QJsonArray ids;
        ids.append(sel_.focusId);
        fmt[QStringLiteral("ids")] = ids;
        const int a = qMin(sel_.anchorOff, sel_.focusOff);
        const int b = qMax(sel_.anchorOff, sel_.focusOff);
        if (sel_.anchorId == sel_.focusId && b > a) {
            fmt[QStringLiteral("start")] = a;
            fmt[QStringLiteral("end")] = b;
        }
    }
    if (fmt.value(QStringLiteral("ids")).toArray().isEmpty())
        return fail(QStringLiteral("no selection"));
    return applyCharacterFormat(fmt);
}

QJsonObject Session::bookmarkAdd(const QJsonObject &args)
{
    const QString name = args.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        return fail(QStringLiteral("name is required"));
    QString blockId = args.value(QStringLiteral("id")).toString();
    if (blockId.isEmpty())
        blockId = sel_.focusId;
    if (!doc_.findBlock(blockId))
        return fail(QStringLiteral("block not found"));
    pushUndo();
    Bookmark bm;
    bm.id = QStringLiteral("bm%1").arg(doc_.bookmarks.size() + 1);
    bm.name = name;
    bm.blockId = blockId;
    bm.offset = args.value(QStringLiteral("offset")).toInt(sel_.focusOff);
    doc_.bookmarks.push_back(bm);
    markChanged();
    auto r = ok();
    r[QStringLiteral("bookmark_id")] = bm.id;
    return r;
}

QJsonObject Session::insertCrossref(const QJsonObject &args)
{
    const QString target = args.value(QStringLiteral("target")).toString();
    if (target.isEmpty())
        return fail(QStringLiteral("target is required"));
    QJsonObject field;
    field[QStringLiteral("kind")] = QStringLiteral("xref");
    field[QStringLiteral("arg")] = target;
    return insertField(field);
}

QJsonObject Session::insertField(const QJsonObject &args)
{
    const QString kind = args.value(QStringLiteral("kind")).toString();
    const QStringList okKinds = {QStringLiteral("page_number"), QStringLiteral("page_count"),
                                 QStringLiteral("date"), QStringLiteral("title"),
                                 QStringLiteral("author"), QStringLiteral("xref"),
                                 QStringLiteral("filename"), QStringLiteral("file_name"),
                                 QStringLiteral("word_count"), QStringLiteral("custom"),
                                 QStringLiteral("merge"), QStringLiteral("mail_merge")};
    if (!okKinds.contains(kind))
        return fail(QStringLiteral("kind must be page_number, page_count, date, title, author, xref, filename, word_count, custom, or merge"));
    QString id = args.value(QStringLiteral("id")).toString();
    int off = args.value(QStringLiteral("offset")).toInt(-1);
    if (id.isEmpty()) {
        id = sel_.focusId;
        off = sel_.focusOff;
    }
    Block *b = doc_.findBlock(id);
    if (!b || b->type == QLatin1String("table") || b->type == QLatin1String("image")
        || b->type == QLatin1String("break"))
        return fail(QStringLiteral("cannot insert field here"));
    pushUndo();
    TextRun ins;
    ins.field = kind;
    ins.fieldArg = args.value(QStringLiteral("arg")).toString();
    ins.text = runDisplayText(doc_, ins, 1, 1);
    const int at = off < 0 ? b->plainText().size() : off;
    b->runs = spliceRuns(b->runs, at, at, ins);
    sel_.focusId = sel_.anchorId = id;
    sel_.focusOff = sel_.anchorOff = at + ins.text.size();
    markChanged();
    return ok();
}

static QJsonObject insertNoteAtCaret(Document &doc, Selection &sel, const QJsonObject &args,
                                     const QString &type)
{
    const QString text = args.value(QStringLiteral("text")).toString(QStringLiteral("Nota"));
    QString hostId = args.value(QStringLiteral("id")).toString();
    int off = args.value(QStringLiteral("offset")).toInt(-1);
    if (hostId.isEmpty()) {
        hostId = sel.focusId;
        off = sel.focusOff;
    }
    Block *host = doc.findBlock(hostId);
    if (!host || host->type == QLatin1String("table") || host->type == QLatin1String("image"))
        return toolResultErr(QStringLiteral("cannot attach note here"));
    Block note;
    note.type = type;
    note.style = QStringLiteral("Normal");
    TextRun nr;
    nr.text = text;
    note.runs.push_back(nr);
    doc.assignId(note);
    const int n = int(std::count_if(doc.body.begin(), doc.body.end(), [&](const Block &b) {
        return b.type == type;
    })) + 1;
    TextRun mark;
    mark.text = QString::number(n);
    mark.superscript = true;
    mark.noteId = note.id;
    const int at = off < 0 ? host->plainText().size() : off;
    host->runs = spliceRuns(host->runs, at, at, mark);
    doc.body.push_back(note);
    sel.focusId = sel.anchorId = hostId;
    sel.focusOff = sel.anchorOff = at + mark.text.size();
    QJsonObject r = toolResultOk();
    r[QStringLiteral("id")] = note.id;
    r[QStringLiteral("host_id")] = hostId;
    return r;
}

QJsonObject Session::insertFootnote(const QJsonObject &args)
{
    pushUndo();
    auto r = insertNoteAtCaret(doc_, sel_, args, QStringLiteral("footnote"));
    if (!r.value(QStringLiteral("ok")).toBool()) {
        undo_.removeLast();
        return r;
    }
    markChanged();
    return r;
}

QJsonObject Session::insertEndnote(const QJsonObject &args)
{
    pushUndo();
    auto r = insertNoteAtCaret(doc_, sel_, args, QStringLiteral("endnote"));
    if (!r.value(QStringLiteral("ok")).toBool()) {
        undo_.removeLast();
        return r;
    }
    markChanged();
    return r;
}

QJsonObject Session::tocUpdate(const QJsonObject &)
{
    pushUndo();
    Block *toc = nullptr;
    for (auto &b : doc_.body) {
        if (b.type == QLatin1String("toc")) {
            toc = &b;
            break;
        }
    }
    Block built;
    built.type = QStringLiteral("toc");
    built.style = QStringLiteral("Normal");
    built.runs.clear();
    built.tocTargets.clear();
    for (const auto &b : doc_.body) {
        if (b.type != QLatin1String("heading"))
            continue;
        TextRun r;
        r.text = QString(qMax(0, b.level - 1) * 2, QLatin1Char(' ')) + b.plainText() + QLatin1Char('\n');
        r.link = QStringLiteral("#") + b.id;
        built.runs.push_back(r);
        built.tocTargets.push_back(b.id);
    }
    if (built.runs.isEmpty()) {
        TextRun r;
        r.text = QStringLiteral("(sem títulos)");
        built.runs.push_back(r);
    }
    if (toc) {
        toc->runs = built.runs;
        toc->tocTargets = built.tocTargets;
    } else {
        doc_.assignId(built);
        int at = 0;
        if (!doc_.body.isEmpty() && doc_.body.first().type == QLatin1String("heading"))
            at = 1;
        doc_.body.insert(at, built);
        toc = &doc_.body[at];
    }
    markChanged();
    auto r = ok();
    r[QStringLiteral("id")] = toc->id;
    return r;
}

QJsonObject Session::spellcheck(const QJsonObject &)
{
    auto r = ok();
    r[QStringLiteral("available")] = spellAvailable();
    r[QStringLiteral("hits")] = spellCheckDocument(doc_);
    return r;
}

QJsonObject Session::trackChangesSet(const QJsonObject &args)
{
    pushUndo();
    if (args.contains(QStringLiteral("on")))
        doc_.trackChanges = args.value(QStringLiteral("on")).toBool();
    markChanged();
    auto r = ok();
    r[QStringLiteral("on")] = doc_.trackChanges;
    return r;
}

QJsonObject Session::revisionList(const QJsonObject &)
{
    QJsonArray a;
    for (const auto &rev : doc_.revisions)
        a.append(rev.toJson());
    auto r = ok();
    r[QStringLiteral("revisions")] = a;
    r[QStringLiteral("on")] = doc_.trackChanges;
    return r;
}

QJsonObject Session::revisionAccept(const QJsonObject &args)
{
    const QString id = args.value(QStringLiteral("id")).toString();
    pushUndo();
    for (int i = 0; i < doc_.revisions.size(); ++i) {
        if (doc_.revisions[i].id == id) {
            doc_.revisions.removeAt(i);
            markChanged();
            return ok();
        }
    }
    undo_.removeLast();
    return fail(QStringLiteral("revision not found"));
}

QJsonObject Session::revisionReject(const QJsonObject &args)
{
    const QString id = args.value(QStringLiteral("id")).toString();
    Revision found;
    int idx = -1;
    for (int i = 0; i < doc_.revisions.size(); ++i) {
        if (doc_.revisions[i].id == id) {
            found = doc_.revisions[i];
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return fail(QStringLiteral("revision not found"));
    pushUndo();
    if (found.kind == QLatin1String("insert")) {
        Block *b = doc_.findBlock(found.blockId);
        if (b)
            b->runs = spliceRuns(b->runs, found.start, found.end, TextRun{});
    }
    doc_.revisions.removeAt(idx);
    markChanged();
    return ok();
}

QJsonObject Session::revisionAcceptAll(const QJsonObject &)
{
    pushUndo();
    doc_.revisions.clear();
    markChanged();
    return ok();
}

QJsonObject Session::revisionRejectAll(const QJsonObject &)
{
    pushUndo();
    const auto revs = doc_.revisions;
    for (const auto &found : revs) {
        if (found.kind == QLatin1String("insert")) {
            Block *b = doc_.findBlock(found.blockId);
            if (b)
                b->runs = spliceRuns(b->runs, found.start, found.end, TextRun{});
        }
    }
    doc_.revisions.clear();
    markChanged();
    return ok();
}

QJsonObject Session::autocorrectGet(const QJsonObject &)
{
    QJsonObject map;
    for (auto it = doc_.autocorrect.begin(); it != doc_.autocorrect.end(); ++it)
        map[it.key()] = it.value();
    auto r = ok();
    r[QStringLiteral("replacements")] = map;
    return r;
}

QJsonObject Session::autocorrectSet(const QJsonObject &args)
{
    const auto obj = args.value(QStringLiteral("replacements")).toObject();
    pushUndo();
    if (args.value(QStringLiteral("replace")).toBool())
        doc_.autocorrect.clear();
    for (auto it = obj.begin(); it != obj.end(); ++it)
        doc_.autocorrect.insert(it.key(), it.value().toString());
    markChanged();
    return autocorrectGet({});
}

QJsonObject Session::mergeCells(const QJsonObject &args)
{
    Block *b = tableBlock(args.value(QStringLiteral("id")).toString());
    if (!b)
        return fail(QStringLiteral("table not found"));
    const int row = args.value(QStringLiteral("row")).toInt();
    const int col = args.value(QStringLiteral("col")).toInt();
    if (row < 0 || row >= b->table.size() || col < 0 || col >= b->table[row].size())
        return fail(QStringLiteral("cell out of range"));
    pushUndo();
    b->table[row][col].colSpan = qMax(1, args.value(QStringLiteral("col_span")).toInt(2));
    b->table[row][col].rowSpan = qMax(1, args.value(QStringLiteral("row_span")).toInt(1));
    markChanged();
    return ok();
}

QJsonObject Session::splitCells(const QJsonObject &args)
{
    Block *b = tableBlock(args.value(QStringLiteral("id")).toString());
    if (!b)
        return fail(QStringLiteral("table not found"));
    const int row = args.value(QStringLiteral("row")).toInt();
    const int col = args.value(QStringLiteral("col")).toInt();
    if (row < 0 || row >= b->table.size() || col < 0 || col >= b->table[row].size())
        return fail(QStringLiteral("cell out of range"));
    pushUndo();
    b->table[row][col].colSpan = 1;
    b->table[row][col].rowSpan = 1;
    markChanged();
    return ok();
}

QJsonObject Session::tableSort(const QJsonObject &args)
{
    Block *b = tableBlock(args.value(QStringLiteral("id")).toString());
    if (!b)
        return fail(QStringLiteral("table not found"));
    const int col = args.value(QStringLiteral("col")).toInt(0);
    const int start = b->headerRow ? 1 : 0;
    if (b->table.size() <= start)
        return ok();
    pushUndo();
    std::sort(b->table.begin() + start, b->table.end(), [col](const QVector<TableCell> &a, const QVector<TableCell> &c) {
        auto cellText = [](const QVector<TableCell> &row, int col) {
            if (col < 0 || col >= row.size())
                return QString();
            QString t;
            for (const auto &r : row[col].runs)
                t += r.text;
            return t;
        };
        return cellText(a, col).localeAwareCompare(cellText(c, col)) < 0;
    });
    markChanged();
    return ok();
}

QJsonObject Session::setImage(const QJsonObject &args)
{
    Block *b = doc_.findBlock(args.value(QStringLiteral("id")).toString());
    if (!b || b->type != QLatin1String("image"))
        return fail(QStringLiteral("image not found"));
    pushUndo();
    if (args.contains(QStringLiteral("width_mm")))
        b->imageWidthMm = args.value(QStringLiteral("width_mm")).toInt();
    if (args.contains(QStringLiteral("alt")))
        b->altText = args.value(QStringLiteral("alt")).toString();
    if (args.contains(QStringLiteral("caption")))
        b->caption = args.value(QStringLiteral("caption")).toString();
    if (args.contains(QStringLiteral("wrap")))
        b->wrap = args.value(QStringLiteral("wrap")).toString();
    if (args.contains(QStringLiteral("anchor")))
        b->anchor = args.value(QStringLiteral("anchor")).toString();
    markChanged();
    return ok();
}

QJsonObject Session::insertCaption(const QJsonObject &args)
{
    Block *b = doc_.findBlock(args.value(QStringLiteral("id")).toString());
    if (!b)
        return fail(QStringLiteral("id not found"));
    pushUndo();
    b->caption = args.value(QStringLiteral("text")).toString();
    markChanged();
    return ok();
}

QJsonObject Session::nestTable(const QJsonObject &args)
{
    Block *b = tableBlock(args.value(QStringLiteral("id")).toString());
    if (!b)
        return fail(QStringLiteral("table not found"));
    const int row = args.value(QStringLiteral("row")).toInt();
    const int col = args.value(QStringLiteral("col")).toInt();
    if (row < 0 || row >= b->table.size() || col < 0 || col >= b->table[row].size())
        return fail(QStringLiteral("cell out of range"));
    const int rows = qMax(1, args.value(QStringLiteral("rows")).toInt(2));
    const int cols = qMax(1, args.value(QStringLiteral("cols")).toInt(2));
    pushUndo();
    auto &cell = b->table[row][col];
    cell.nested.clear();
    for (int r = 0; r < rows; ++r) {
        QVector<NestedCell> nrow;
        for (int c = 0; c < cols; ++c)
            nrow.push_back(NestedCell{});
        cell.nested.push_back(nrow);
    }
    markChanged();
    return ok();
}

static void substituteMergeRuns(QVector<TextRun> &runs, const QJsonObject &rec)
{
    for (auto &r : runs) {
        if (r.field == QLatin1String("merge") || r.field == QLatin1String("mail_merge")) {
            const QString key = r.fieldArg.isEmpty() ? r.text : r.fieldArg;
            r.text = rec.value(key).toString();
            r.field.clear();
            r.fieldArg.clear();
        } else {
            QString t = r.text;
            for (auto it = rec.begin(); it != rec.end(); ++it)
                t.replace(QStringLiteral("{{") + it.key() + QStringLiteral("}}"), it.value().toString());
            r.text = t;
        }
    }
}

static void substituteMergeBlock(Block &b, const QJsonObject &rec)
{
    substituteMergeRuns(b.runs, rec);
    for (auto &row : b.table) {
        for (auto &cell : row) {
            substituteMergeRuns(cell.runs, rec);
            for (auto &nrow : cell.nested) {
                for (auto &nc : nrow)
                    substituteMergeRuns(nc.runs, rec);
            }
        }
    }
}

QJsonObject Session::mailMerge(const QJsonObject &args)
{
    const QJsonArray records = args.value(QStringLiteral("records")).toArray();
    if (records.isEmpty())
        return fail(QStringLiteral("records is required"));
    if (doc_.body.isEmpty())
        return fail(QStringLiteral("document is empty"));
    pushUndo();
    const QVector<Block> tmpl = doc_.body;
    doc_.body.clear();
    QJsonArray ids;
    for (int i = 0; i < records.size(); ++i) {
        if (i > 0) {
            Block br;
            br.type = QStringLiteral("break");
            br.breakKind = QStringLiteral("page");
            doc_.assignId(br);
            doc_.body.push_back(br);
        }
        const QJsonObject rec = records.at(i).toObject();
        for (Block b : tmpl) {
            b.id.clear();
            doc_.assignId(b);
            substituteMergeBlock(b, rec);
            ids.append(b.id);
            doc_.body.push_back(b);
        }
    }
    markChanged();
    auto r = ok();
    r[QStringLiteral("copies")] = records.size();
    r[QStringLiteral("ids")] = ids;
    return r;
}

QJsonObject Session::clearFormat(const QJsonObject &args)
{
    const auto idx = indexesFor(idList(args));
    if (idx.isEmpty())
        return fail(QStringLiteral("no matching ids"));
    pushUndo();
    const int start = args.value(QStringLiteral("start")).toInt(-1);
    const int end = args.value(QStringLiteral("end")).toInt(-1);
    for (int i : idx) {
        auto &b = doc_.body[i];
        if (start >= 0 && end > start && b.type != QLatin1String("table")) {
            QVector<TextRun> mid = splitRuns(b.runs, start, end);
            for (auto &r : mid)
                clearRunFormat(r);
            b.runs = splitRuns(b.runs, 0, start) + mid + splitRuns(b.runs, end, b.plainText().size());
            if (b.runs.isEmpty())
                b.runs.push_back(TextRun{});
        } else {
            for (auto &r : b.runs)
                clearRunFormat(r);
            if (b.type == QLatin1String("table")) {
                for (auto &row : b.table)
                    for (auto &cell : row)
                        for (auto &r : cell.runs)
                            clearRunFormat(r);
            }
        }
    }
    markChanged();
    return ok();
}

QJsonObject Session::toggleCheck(const QJsonObject &args)
{
    const auto idx = indexesFor(idList(args));
    if (idx.isEmpty())
        return fail(QStringLiteral("no matching ids"));
    pushUndo();
    for (int i : idx) {
        auto &b = doc_.body[i];
        if (b.type != QLatin1String("list-item"))
            continue;
        if (args.contains(QStringLiteral("checked")))
            b.checked = args.value(QStringLiteral("checked")).toBool();
        else
            b.checked = !b.checked;
        if (b.listKind.isEmpty() || b.listKind == QLatin1String("bullet"))
            b.listKind = QStringLiteral("checkbox");
    }
    markChanged();
    return ok();
}

QJsonObject Session::insertSpecial(const QJsonObject &args)
{
    const QString kind = args.value(QStringLiteral("kind")).toString();
    QString ch;
    if (kind == QLatin1String("nbsp") || kind == QLatin1String("nbspace") || kind == QLatin1String("nonbreaking_space"))
        ch = QChar(0x00A0);
    else if (kind == QLatin1String("shy") || kind == QLatin1String("soft_hyphen"))
        ch = QChar(0x00AD);
    else if (kind == QLatin1String("emdash"))
        ch = QChar(0x2014);
    else if (kind == QLatin1String("endash"))
        ch = QChar(0x2013);
    else if (kind == QLatin1String("ellipsis"))
        ch = QChar(0x2026);
    else if (kind == QLatin1String("copyright"))
        ch = QChar(0x00A9);
    else if (kind == QLatin1String("euro"))
        ch = QChar(0x20AC);
    else if (!args.value(QStringLiteral("text")).toString().isEmpty())
        ch = args.value(QStringLiteral("text")).toString();
    if (ch.isEmpty())
        return fail(QStringLiteral("kind must be nbsp, shy, emdash, endash, ellipsis, copyright, euro, or pass text"));
    QJsonObject a = args;
    a[QStringLiteral("text")] = ch;
    return insertAtCaret(a);
}

QJsonObject Session::paintFormat(const QJsonObject &args)
{
    const QString fromId = args.value(QStringLiteral("from_id")).toString();
    const Block *src = doc_.findBlock(fromId);
    if (!src || src->runs.isEmpty())
        return fail(QStringLiteral("from_id not found"));
    int at = args.value(QStringLiteral("from_offset")).toInt(0);
    const QString plain = src->plainText();
    at = qBound(0, at, qMax(0, plain.size() - 1));
    TextRun sample;
    int pos = 0;
    for (const auto &r : src->runs) {
        if (at < pos + r.text.size() || pos + r.text.size() >= plain.size()) {
            sample = r;
            break;
        }
        pos += r.text.size();
    }
    QJsonObject fmt = runFormatJson(sample);
    fmt[QStringLiteral("ids")] = args.value(QStringLiteral("ids"));
    if (args.contains(QStringLiteral("id")))
        fmt[QStringLiteral("id")] = args.value(QStringLiteral("id"));
    if (args.contains(QStringLiteral("start")))
        fmt[QStringLiteral("start")] = args.value(QStringLiteral("start"));
    if (args.contains(QStringLiteral("end")))
        fmt[QStringLiteral("end")] = args.value(QStringLiteral("end"));
    if (idList(fmt).isEmpty())
        return fail(QStringLiteral("ids or id is required"));
    return applyCharacterFormat(fmt);
}

QJsonObject Session::gotoBlock(const QJsonObject &args)
{
    DocumentLayout lay;
    lay.relayout(doc_, 1.0);
    QString id = args.value(QStringLiteral("id")).toString();
    int page = args.value(QStringLiteral("page")).toInt(0);
    if (!id.isEmpty()) {
        if (!doc_.findBlock(id))
            return fail(QStringLiteral("id not found"));
        page = lay.pageOf(id);
        sel_.anchorId = sel_.focusId = id;
        sel_.anchorOff = sel_.focusOff = 0;
        sel_.tableRow = sel_.tableCol = -1;
    } else if (page > 0) {
        if (page > lay.pages)
            return fail(QStringLiteral("page not found"));
        bool found = false;
        for (const auto &lb : lay.blocks) {
            if (lb.page == page && lb.type != QLatin1String("break")) {
                id = lb.id;
                found = true;
                break;
            }
        }
        if (!found)
            return fail(QStringLiteral("page not found"));
        sel_.anchorId = sel_.focusId = id;
        sel_.anchorOff = sel_.focusOff = 0;
        sel_.tableRow = sel_.tableCol = -1;
    } else {
        return fail(QStringLiteral("page or id is required"));
    }
    auto r = ok();
    r[QStringLiteral("id")] = id;
    r[QStringLiteral("page")] = page;
    r[QStringLiteral("display_page")] = lay.displayPage(page);
    return r;
}

QJsonObject Session::selectByStyle(const QJsonObject &args)
{
    const QString style = args.value(QStringLiteral("style")).toString();
    if (style.isEmpty())
        return fail(QStringLiteral("style is required"));
    QJsonArray ids;
    for (const auto &b : doc_.body) {
        if (b.style == style)
            ids.append(b.id);
    }
    if (!ids.isEmpty()) {
        const QString first = ids.at(0).toString();
        sel_.anchorId = sel_.focusId = first;
        sel_.anchorOff = sel_.focusOff = 0;
        sel_.tableRow = sel_.tableCol = -1;
    }
    auto r = ok();
    r[QStringLiteral("ids")] = ids;
    return r;
}

QJsonObject Session::updateStyle(const QJsonObject &args)
{
    QString id = args.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        id = sel_.focusId;
    const Block *b = doc_.findBlock(id);
    if (!b)
        return fail(QStringLiteral("id not found"));
    QString name = args.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        name = b->style;
    if (name.isEmpty())
        name = QStringLiteral("Normal");
    const TextRun sample = runAtOffset(*b, args.value(QStringLiteral("offset")).toInt(sel_.focusOff));
    pushUndo();
    StyleDef *target = styleByName(doc_, name);
    if (!target) {
        StyleDef s = defaultStyle(QStringLiteral("Normal"));
        s.name = name;
        doc_.styles.push_back(s);
        target = &doc_.styles.last();
    }
    if (!sample.font.isEmpty())
        target->font = sample.font;
    if (sample.sizePt > 0)
        target->sizePt = sample.sizePt;
    target->bold = sample.bold;
    target->italic = sample.italic;
    if (!sample.color.isEmpty())
        target->color = sample.color;
    if (!b->align.isEmpty())
        target->align = b->align;
    if (b->spaceBeforePt >= 0)
        target->spaceBeforePt = b->spaceBeforePt;
    if (b->spaceAfterPt >= 0)
        target->spaceAfterPt = b->spaceAfterPt;
    if (b->type == QLatin1String("list-item")) {
        target->kind = QStringLiteral("list");
        target->listKind = b->listKind;
        target->listBullet = b->listBullet;
    }
    markChanged();
    auto r = ok();
    r[QStringLiteral("name")] = name;
    return r;
}

QJsonObject Session::insertLabels(const QJsonObject &args)
{
    const auto recs = args.value(QStringLiteral("records")).toArray();
    if (recs.isEmpty())
        return fail(QStringLiteral("records is required"));
    const int cols = qMax(1, args.value(QStringLiteral("cols")).toInt(2));
    int rows = args.value(QStringLiteral("rows")).toInt(0);
    if (rows <= 0)
        rows = (recs.size() + cols - 1) / cols;
    Block b;
    b.type = QStringLiteral("table");
    b.headerRow = false;
    int idx = 0;
    for (int r = 0; r < rows; ++r) {
        QVector<TableCell> row;
        for (int c = 0; c < cols; ++c) {
            TableCell cell;
            TextRun run;
            if (idx < recs.size()) {
                if (recs.at(idx).isObject())
                    run.text = recs.at(idx).toObject().value(QStringLiteral("text")).toString();
                else
                    run.text = recs.at(idx).toString();
                ++idx;
            }
            cell.runs.push_back(run);
            row.push_back(cell);
        }
        b.table.push_back(row);
    }
    QJsonObject wrap;
    wrap[QStringLiteral("after_id")] = args.value(QStringLiteral("after_id")).toString(QStringLiteral("end"));
    wrap[QStringLiteral("blocks")] = QJsonArray{b.toJson()};
    return insertBlocks(wrap);
}

QJsonObject Session::splitBlockTool(const QJsonObject &args)
{
    QString id = args.value(QStringLiteral("id")).toString();
    int off = args.contains(QStringLiteral("offset")) ? args.value(QStringLiteral("offset")).toInt() : -1;
    if (id.isEmpty()) {
        id = sel_.focusId;
        if (off < 0)
            off = sel_.focusOff;
    }
    if (id.isEmpty())
        return fail(QStringLiteral("id is required"));
    if (off < 0)
        off = 0;
    if (!doc_.findBlock(id))
        return fail(QStringLiteral("id not found"));
    pushUndo();
    if (!splitBlock(id, off)) {
        undo_.removeLast();
        return fail(QStringLiteral("cannot split this block"));
    }
    markChanged();
    auto r = ok();
    r[QStringLiteral("id")] = sel_.focusId;
    return r;
}

QJsonObject Session::joinBlocks(const QJsonObject &args)
{
    QString id = args.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        id = sel_.focusId;
    if (id.isEmpty())
        return fail(QStringLiteral("id is required"));
    const int idx = doc_.indexOf(id);
    if (idx <= 0)
        return fail(QStringLiteral("cannot join this block"));
    pushUndo();
    Block &prev = doc_.body[idx - 1];
    Block &cur = doc_.body[idx];
    if (prev.type == QLatin1String("table") || prev.type == QLatin1String("image")
        || prev.type == QLatin1String("break") || cur.type == QLatin1String("table")
        || cur.type == QLatin1String("image") || cur.type == QLatin1String("break")) {
        undo_.removeLast();
        return fail(QStringLiteral("cannot join this block"));
    }
    const int joinOff = prev.plainText().size();
    for (const auto &r : cur.runs)
        prev.runs.push_back(r);
    const QString keep = prev.id;
    doc_.body.removeAt(idx);
    sel_.anchorId = sel_.focusId = keep;
    sel_.anchorOff = sel_.focusOff = joinOff;
    sel_.tableRow = sel_.tableCol = -1;
    markChanged();
    auto r = ok();
    r[QStringLiteral("id")] = keep;
    return r;
}

QJsonObject Session::insertHtml(const QJsonObject &args)
{
    const QString html = args.value(QStringLiteral("html")).toString();
    if (html.trimmed().isEmpty())
        return fail(QStringLiteral("html is required"));
    auto blocks = blocksFromHtml(html);
    if (blocks.isEmpty())
        return fail(QStringLiteral("html produced no blocks"));
    QJsonArray arr;
    for (auto &b : blocks)
        arr.append(b.toJson());
    QJsonObject wrap;
    wrap[QStringLiteral("after_id")] = args.value(QStringLiteral("after_id")).toString(QStringLiteral("end"));
    wrap[QStringLiteral("blocks")] = arr;
    return insertBlocks(wrap);
}

QJsonObject Session::insertPlain(const QJsonObject &args)
{
    QString text = args.value(QStringLiteral("text")).toString();
    if (text.isEmpty())
        return fail(QStringLiteral("text is required"));
    text.replace(QLatin1Char('\r'), QString());
    QJsonArray arr;
    const auto paras = text.split(QLatin1Char('\n'));
    for (const QString &para : paras) {
        Block b;
        b.type = QStringLiteral("paragraph");
        b.style = QStringLiteral("Normal");
        TextRun r;
        r.text = para;
        b.runs.push_back(r);
        arr.append(b.toJson());
    }
    QJsonObject wrap;
    wrap[QStringLiteral("after_id")] = args.value(QStringLiteral("after_id")).toString(QStringLiteral("end"));
    wrap[QStringLiteral("blocks")] = arr;
    return insertBlocks(wrap);
}

QJsonObject Session::insertFrame(const QJsonObject &args)
{
    Block b;
    b.type = QStringLiteral("frame");
    b.style = args.value(QStringLiteral("style")).toString();
    b.borderPt = args.contains(QStringLiteral("border_pt"))
        ? args.value(QStringLiteral("border_pt")).toDouble()
        : 0.75;
    b.borderColor = args.value(QStringLiteral("border_color")).toString(QStringLiteral("#B5651D"));
    b.imageWidthMm = args.contains(QStringLiteral("width_mm"))
        ? args.value(QStringLiteral("width_mm")).toInt()
        : 80;
    TextRun r;
    r.text = args.value(QStringLiteral("text")).toString();
    b.runs.push_back(r);
    QJsonObject wrap;
    wrap[QStringLiteral("after_id")] = args.value(QStringLiteral("after_id")).toString(QStringLiteral("end"));
    wrap[QStringLiteral("blocks")] = QJsonArray{b.toJson()};
    return insertBlocks(wrap);
}

static void autoFormatRuns(QVector<TextRun> &runs, const QString &lang, bool *quoteOpen)
{
    const bool pt = lang.startsWith(QLatin1String("pt"), Qt::CaseInsensitive);
    const QChar qOpen = pt ? QChar(0x00AB) : QChar(0x201C);
    const QChar qClose = pt ? QChar(0x00BB) : QChar(0x201D);
    bool open = quoteOpen ? *quoteOpen : true;
    const QRegularExpression urlRe(QStringLiteral(R"(https?://[^\s<>]+)"));
    for (auto &r : runs) {
        QString t = r.text;
        t.replace(QStringLiteral("..."), QString(QChar(0x2026)));
        t.replace(QStringLiteral("--"), QString(QChar(0x2014)));
        t.replace(QStringLiteral("1/4"), QString(QChar(0x00BC)));
        t.replace(QStringLiteral("1/2"), QString(QChar(0x00BD)));
        t.replace(QStringLiteral("3/4"), QString(QChar(0x00BE)));
        QString quoted;
        quoted.reserve(t.size());
        for (const QChar ch : t) {
            if (ch == QLatin1Char('"') || ch == QChar(0x201C) || ch == QChar(0x201D)) {
                quoted += open ? qOpen : qClose;
                open = !open;
            } else {
                quoted += ch;
            }
        }
        r.text = quoted;
        if (r.link.isEmpty()) {
            const auto m = urlRe.match(r.text.trimmed());
            if (m.hasMatch() && m.captured() == r.text.trimmed())
                r.link = m.captured();
        }
    }
    if (quoteOpen)
        *quoteOpen = open;
}

QJsonObject Session::autoFormat(const QJsonObject &args)
{
    QString lang = args.value(QStringLiteral("lang")).toString();
    if (lang.isEmpty()) {
        for (const auto &b : doc_.body) {
            if (!b.lang.isEmpty()) {
                lang = b.lang;
                break;
            }
            for (const auto &r : b.runs) {
                if (!r.lang.isEmpty()) {
                    lang = r.lang;
                    break;
                }
            }
            if (!lang.isEmpty())
                break;
        }
    }
    if (lang.isEmpty())
        lang = QStringLiteral("pt-PT");
    pushUndo();
    bool quoteOpen = true;
    for (auto &b : doc_.body) {
        autoFormatRuns(b.runs, lang, &quoteOpen);
        for (auto &row : b.table) {
            for (auto &cell : row)
                autoFormatRuns(cell.runs, lang, &quoteOpen);
        }
    }
    markChanged();
    auto r = ok();
    r[QStringLiteral("lang")] = lang;
    return r;
}

QJsonObject Session::loadStyles(const QJsonObject &args)
{
    const QString path = args.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
        return fail(QStringLiteral("path is required"));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("cannot read path"));
    const auto docj = QJsonDocument::fromJson(f.readAll());
    if (!docj.isObject())
        return fail(QStringLiteral("not a document JSON"));
    const Document other = Document::fromJson(docj.object());
    pushUndo();
    int n = 0;
    for (const auto &s : other.styles) {
        if (StyleDef *ex = styleByName(doc_, s.name))
            *ex = s;
        else
            doc_.styles.push_back(s);
        ++n;
    }
    markChanged();
    auto r = ok();
    r[QStringLiteral("count")] = n;
    return r;
}

QJsonObject Session::validate(const QJsonObject &)
{
    QJsonArray errors;
    QSet<QString> ids;
    QSet<QString> noteIds;
    const QStringList known{QStringLiteral("paragraph"), QStringLiteral("heading"),
                            QStringLiteral("list-item"), QStringLiteral("table"), QStringLiteral("image"),
                            QStringLiteral("break"),     QStringLiteral("footnote"), QStringLiteral("endnote"),
                            QStringLiteral("toc"),       QStringLiteral("frame")};
    auto err = [&](const QString &code, const QString &id, const QString &msg) {
        QJsonObject e;
        e[QStringLiteral("code")] = code;
        if (!id.isEmpty())
            e[QStringLiteral("id")] = id;
        e[QStringLiteral("message")] = msg;
        errors.append(e);
    };
    if (doc_.body.isEmpty())
        err(QStringLiteral("empty_body"), {}, QStringLiteral("document body is empty"));
    for (const auto &b : doc_.body) {
        if (b.id.isEmpty()) {
            err(QStringLiteral("missing_id"), {}, QStringLiteral("block without id"));
            continue;
        }
        if (ids.contains(b.id))
            err(QStringLiteral("duplicate_id"), b.id, QStringLiteral("duplicate block id"));
        ids.insert(b.id);
        if (!known.contains(b.type))
            err(QStringLiteral("unknown_type"), b.id, QStringLiteral("unknown block type: ") + b.type);
        if (b.type == QLatin1String("footnote") || b.type == QLatin1String("endnote"))
            noteIds.insert(b.id);
    }
    for (const auto &b : doc_.body) {
        for (const auto &r : b.runs) {
            if (!r.noteId.isEmpty() && !noteIds.contains(r.noteId) && !ids.contains(r.noteId))
                err(QStringLiteral("broken_note_id"), b.id, QStringLiteral("note_id not found: ") + r.noteId);
            if (r.field == QLatin1String("xref") && !r.fieldArg.isEmpty()) {
                bool okRef = ids.contains(r.fieldArg);
                if (!okRef) {
                    for (const auto &bm : doc_.bookmarks) {
                        if (bm.id == r.fieldArg || bm.name == r.fieldArg) {
                            okRef = true;
                            break;
                        }
                    }
                }
                if (!okRef)
                    err(QStringLiteral("broken_xref"), b.id, QStringLiteral("xref target missing: ") + r.fieldArg);
            }
        }
        if (b.type == QLatin1String("table")) {
            for (const auto &row : b.table) {
                for (const auto &cell : row) {
                    for (const auto &r : cell.runs) {
                        if (r.field == QLatin1String("xref") && !r.fieldArg.isEmpty() && !ids.contains(r.fieldArg))
                            err(QStringLiteral("broken_xref"), b.id, QStringLiteral("xref target missing: ") + r.fieldArg);
                    }
                }
            }
        }
    }
    for (const auto &bm : doc_.bookmarks) {
        if (!ids.contains(bm.blockId))
            err(QStringLiteral("broken_bookmark"), bm.id, QStringLiteral("bookmark block missing: ") + bm.blockId);
    }
    auto r = ok();
    r[QStringLiteral("valid")] = errors.isEmpty();
    r[QStringLiteral("errors")] = errors;
    r[QStringLiteral("blocks")] = ids.size();
    return r;
}

QJsonObject Session::batch(const QJsonObject &args)
{
    if (undoSuppress_ > 0)
        return fail(QStringLiteral("nested batch is not allowed"));
    const QJsonArray calls = args.value(QStringLiteral("calls")).toArray();
    if (calls.isEmpty())
        return fail(QStringLiteral("calls is required"));
    const Document snap = doc_;
    const QString snapPath = path_;
    const bool snapDirty = dirty_;
    const Selection snapSel = sel_;
    pushUndo();
    ++undoSuppress_;
    QJsonArray results;
    bool failed = false;
    QString err;
    for (const auto &v : calls) {
        const QJsonObject c = v.toObject();
        const QString t = c.value(QStringLiteral("tool")).toString();
        if (t.isEmpty()) {
            failed = true;
            err = QStringLiteral("each call needs tool");
            break;
        }
        if (t == QLatin1String("batch") || t == QLatin1String("undo") || t == QLatin1String("redo")
            || t == QLatin1String("checkpoint") || t == QLatin1String("restore")) {
            failed = true;
            err = QStringLiteral("tool not allowed inside batch: ") + t;
            break;
        }
        const QJsonObject a = c.value(QStringLiteral("args")).toObject();
        const auto one = call(t, a);
        results.append(one);
        if (!one.value(QStringLiteral("ok")).toBool()) {
            failed = true;
            err = one.value(QStringLiteral("error")).toString();
            break;
        }
    }
    --undoSuppress_;
    if (failed) {
        doc_ = snap;
        path_ = snapPath;
        dirty_ = snapDirty;
        sel_ = snapSel;
        if (!undo_.isEmpty())
            undo_.pop_back();
        auto r = fail(err.isEmpty() ? QStringLiteral("batch failed") : err);
        r[QStringLiteral("results")] = results;
        return r;
    }
    markChanged();
    auto r = ok();
    r[QStringLiteral("results")] = results;
    r[QStringLiteral("count")] = results.size();
    return r;
}

QJsonObject Session::checkpoint(const QJsonObject &)
{
    checkpointDoc_ = doc_;
    checkpointPath_ = path_;
    checkpointDirty_ = dirty_;
    checkpointSel_ = sel_;
    hasCheckpoint_ = true;
    auto r = ok();
    r[QStringLiteral("blocks")] = doc_.body.size();
    return r;
}

QJsonObject Session::restoreCheckpoint(const QJsonObject &)
{
    if (!hasCheckpoint_)
        return fail(QStringLiteral("no checkpoint"));
    pushUndo();
    doc_ = checkpointDoc_;
    path_ = checkpointPath_;
    dirty_ = checkpointDirty_;
    sel_ = checkpointSel_;
    markChanged();
    return ok();
}

QJsonObject Session::insertMarkdown(const QJsonObject &args)
{
    return importMarkdown(args);
}

QJsonObject Session::call(const QString &tool, const QJsonObject &args)
{
    if (tool == QLatin1String("document_new"))
        return newDocument(args);
    if (tool == QLatin1String("document_open"))
        return openDocument(args);
    if (tool == QLatin1String("document_save"))
        return saveDocument(args);
    if (tool == QLatin1String("document_export_pdf"))
        return exportPdf(args);
    if (tool == QLatin1String("document_get"))
        return getDocument(args);
    if (tool == QLatin1String("document_get_outline"))
        return getOutline(args);
    if (tool == QLatin1String("insert_blocks"))
        return insertBlocks(args);
    if (tool == QLatin1String("replace_range"))
        return replaceRange(args);
    if (tool == QLatin1String("delete_range"))
        return deleteRange(args);
    if (tool == QLatin1String("apply_character_format"))
        return applyCharacterFormat(args);
    if (tool == QLatin1String("apply_paragraph_format"))
        return applyParagraphFormat(args);
    if (tool == QLatin1String("apply_style"))
        return applyStyle(args);
    if (tool == QLatin1String("create_style"))
        return createStyle(args);
    if (tool == QLatin1String("set_page_layout"))
        return setPageLayout(args);
    if (tool == QLatin1String("insert_table"))
        return insertTable(args);
    if (tool == QLatin1String("insert_image"))
        return insertImage(args);
    if (tool == QLatin1String("insert_list"))
        return insertList(args);
    if (tool == QLatin1String("insert_break"))
        return insertBreak(args);
    if (tool == QLatin1String("find_replace"))
        return findReplace(args);
    if (tool == QLatin1String("search"))
        return search(args);
    if (tool == QLatin1String("undo"))
        return undo(args);
    if (tool == QLatin1String("redo"))
        return redo(args);
    if (tool == QLatin1String("page_preview"))
        return pagePreview(args);
    if (tool == QLatin1String("integrate_help"))
        return integrateHelp(args);
    if (tool == QLatin1String("set_cell"))
        return setCell(args);
    if (tool == QLatin1String("insert_row"))
        return insertRow(args);
    if (tool == QLatin1String("insert_col") || tool == QLatin1String("insert_column"))
        return insertCol(args);
    if (tool == QLatin1String("delete_row"))
        return deleteRow(args);
    if (tool == QLatin1String("delete_col") || tool == QLatin1String("delete_column"))
        return deleteCol(args);
    if (tool == QLatin1String("selection_get"))
        return selectionGet(args);
    if (tool == QLatin1String("set_selection"))
        return setSelectionTool(args);
    if (tool == QLatin1String("insert_at_caret"))
        return insertAtCaret(args);
    if (tool == QLatin1String("move_blocks"))
        return moveBlocks(args);
    if (tool == QLatin1String("duplicate_blocks"))
        return duplicateBlocks(args);
    if (tool == QLatin1String("comment_add"))
        return commentAdd(args);
    if (tool == QLatin1String("comment_resolve"))
        return commentResolve(args);
    if (tool == QLatin1String("document_set_title"))
        return documentSetTitle(args);
    if (tool == QLatin1String("set_header"))
        return setHeader(args);
    if (tool == QLatin1String("set_footer"))
        return setFooter(args);
    if (tool == QLatin1String("document_diff"))
        return documentDiff(args);
    if (tool == QLatin1String("import_markdown"))
        return importMarkdown(args);
    if (tool == QLatin1String("document_from_template"))
        return documentFromTemplate(args);
    if (tool == QLatin1String("link_set"))
        return linkSet(args);
    if (tool == QLatin1String("bookmark_add"))
        return bookmarkAdd(args);
    if (tool == QLatin1String("insert_crossref"))
        return insertCrossref(args);
    if (tool == QLatin1String("insert_field"))
        return insertField(args);
    if (tool == QLatin1String("insert_footnote"))
        return insertFootnote(args);
    if (tool == QLatin1String("insert_endnote"))
        return insertEndnote(args);
    if (tool == QLatin1String("toc_update"))
        return tocUpdate(args);
    if (tool == QLatin1String("spellcheck"))
        return spellcheck(args);
    if (tool == QLatin1String("track_changes"))
        return trackChangesSet(args);
    if (tool == QLatin1String("revision_list"))
        return revisionList(args);
    if (tool == QLatin1String("revision_accept"))
        return revisionAccept(args);
    if (tool == QLatin1String("revision_reject"))
        return revisionReject(args);
    if (tool == QLatin1String("revision_accept_all"))
        return revisionAcceptAll(args);
    if (tool == QLatin1String("revision_reject_all"))
        return revisionRejectAll(args);
    if (tool == QLatin1String("autocorrect_get"))
        return autocorrectGet(args);
    if (tool == QLatin1String("autocorrect_set"))
        return autocorrectSet(args);
    if (tool == QLatin1String("merge_cells"))
        return mergeCells(args);
    if (tool == QLatin1String("split_cells"))
        return splitCells(args);
    if (tool == QLatin1String("table_sort"))
        return tableSort(args);
    if (tool == QLatin1String("set_image"))
        return setImage(args);
    if (tool == QLatin1String("insert_caption"))
        return insertCaption(args);
    if (tool == QLatin1String("nest_table"))
        return nestTable(args);
    if (tool == QLatin1String("mail_merge"))
        return mailMerge(args);
    if (tool == QLatin1String("clear_format") || tool == QLatin1String("clear_character_format"))
        return clearFormat(args);
    if (tool == QLatin1String("toggle_check"))
        return toggleCheck(args);
    if (tool == QLatin1String("insert_special"))
        return insertSpecial(args);
    if (tool == QLatin1String("paint_format"))
        return paintFormat(args);
    if (tool == QLatin1String("goto") || tool == QLatin1String("go_to"))
        return gotoBlock(args);
    if (tool == QLatin1String("select_by_style"))
        return selectByStyle(args);
    if (tool == QLatin1String("update_style"))
        return updateStyle(args);
    if (tool == QLatin1String("insert_labels"))
        return insertLabels(args);
    if (tool == QLatin1String("split_block"))
        return splitBlockTool(args);
    if (tool == QLatin1String("join_blocks") || tool == QLatin1String("join_block"))
        return joinBlocks(args);
    if (tool == QLatin1String("insert_html"))
        return insertHtml(args);
    if (tool == QLatin1String("insert_plain") || tool == QLatin1String("paste_plain"))
        return insertPlain(args);
    if (tool == QLatin1String("insert_frame"))
        return insertFrame(args);
    if (tool == QLatin1String("autoformat") || tool == QLatin1String("auto_format"))
        return autoFormat(args);
    if (tool == QLatin1String("load_styles"))
        return loadStyles(args);
    if (tool == QLatin1String("insert_line_break")) {
        QJsonObject a = args;
        a[QStringLiteral("text")] = QString(QChar::LineFeed);
        return insertAtCaret(a);
    }
    if (tool == QLatin1String("insert_tab")) {
        QJsonObject a = args;
        a[QStringLiteral("text")] = QString(QLatin1Char('\t'));
        return insertAtCaret(a);
    }
    if (tool == QLatin1String("validate") || tool == QLatin1String("document_validate"))
        return validate(args);
    if (tool == QLatin1String("batch"))
        return batch(args);
    if (tool == QLatin1String("checkpoint"))
        return checkpoint(args);
    if (tool == QLatin1String("restore") || tool == QLatin1String("restore_checkpoint"))
        return restoreCheckpoint(args);
    if (tool == QLatin1String("insert_markdown"))
        return insertMarkdown(args);
    return fail(QStringLiteral("unknown tool: ") + tool);
}

bool Session::toggleTableHeader()
{
    Block *b = doc_.findBlock(sel_.focusId);
    if (!b || b->type != QLatin1String("table"))
        return false;
    pushUndo();
    b->headerRow = !b->headerRow;
    markChanged();
    return true;
}

bool Session::bumpListLevel(const QString &id, int delta)
{
    Block *b = doc_.findBlock(id);
    if (!b || b->type != QLatin1String("list-item"))
        return false;
    pushUndo();
    b->level = qBound(1, b->level + delta, 8);
    markChanged();
    return true;
}

void Session::restoreSnapshot(const Document &d, const QString &path, bool dirty)
{
    doc_ = d;
    path_ = path;
    dirty_ = dirty;
    undo_.clear();
    redo_.clear();
    sel_ = {};
    if (!doc_.body.isEmpty()) {
        sel_.anchorId = sel_.focusId = doc_.body.first().id;
        sel_.anchorOff = sel_.focusOff = 0;
    }
    captureBaseline();
    if (onChanged_)
        onChanged_();
}

void Session::commitCaretInsert(const QString &text)
{
    if (text.isEmpty())
        return;
    if (sel_.focusId.isEmpty() && !doc_.body.isEmpty()) {
        sel_.focusId = sel_.anchorId = doc_.body.first().id;
        sel_.focusOff = sel_.anchorOff = 0;
    }
    QString t = text;
    static const QMap<QString, QString> autoDefault = {
        {QStringLiteral("teh"), QStringLiteral("the")},
        {QStringLiteral("recieve"), QStringLiteral("receive")},
        {QStringLiteral("adn"), QStringLiteral("and")},
        {QStringLiteral("nao"), QStringLiteral("não")},
        {QStringLiteral("voce"), QStringLiteral("você")},
        {QStringLiteral("vc"), QStringLiteral("você")},
        {QStringLiteral("(c)"), QStringLiteral("©")},
        {QStringLiteral("(r)"), QStringLiteral("®")},
    };
    const bool boundary = t.size() == 1 && (t[0].isSpace() || t[0].isPunct());
    if (boundary) {
        Block *b = doc_.findBlock(sel_.focusId);
        if (b && sel_.tableRow < 0) {
            const QString plain = b->plainText().left(sel_.focusOff);
            int i = plain.size() - 1;
            while (i >= 0 && (plain[i].isLetter() || plain[i] == QLatin1Char(')')))
                --i;
            const QString word = plain.mid(i + 1);
            QString repl = doc_.autocorrect.value(word.toLower());
            if (repl.isEmpty())
                repl = autoDefault.value(word.toLower());
            if (!repl.isEmpty() && !word.isEmpty()) {
                if (word[0].isUpper())
                    repl[0] = repl[0].toUpper();
                pushUndo();
                b->runs = spliceRuns(b->runs, i + 1, sel_.focusOff, TextRun{});
                TextRun ins;
                ins.text = repl;
                b->runs = spliceRuns(b->runs, i + 1, i + 1, ins);
                sel_.focusOff = sel_.anchorOff = i + 1 + repl.size();
                sel_.anchorId = sel_.focusId;
            }
        }
    }
    pushUndo();
    if (!insertTextAt(sel_.focusId, sel_.focusOff, t, sel_.tableRow, sel_.tableCol)) {
        undo_.removeLast();
        return;
    }
    sel_.focusOff = sel_.anchorOff = sel_.focusOff + t.size();
    sel_.anchorId = sel_.focusId;
    markChanged();
}

void Session::commitBackspace()
{
    if (sel_.focusId.isEmpty())
        return;
    pushUndo();
    const int off = sel_.focusOff;
    if (!deleteBackward(sel_.focusId, off, sel_.tableRow, sel_.tableCol)) {
        undo_.removeLast();
        return;
    }
    if (off > 0) {
        sel_.focusOff = sel_.anchorOff = off - 1;
        sel_.anchorId = sel_.focusId;
    }
    markChanged();
}

void Session::commitSplit()
{
    if (sel_.focusId.isEmpty())
        return;
    pushUndo();
    if (!splitBlock(sel_.focusId, sel_.focusOff)) {
        undo_.removeLast();
        return;
    }
    markChanged();
}

} // namespace aioffice
