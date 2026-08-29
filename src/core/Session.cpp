#include "core/Session.h"

#include "core/Docx.h"
#include "core/Odf.h"
#include "core/Preview.h"
#include "lok/LokEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
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
        f.write(QJsonDocument(doc_.toJson()).toJson(QJsonDocument::Indented));
        return true;
    }
    if (ext == QLatin1String("odt"))
        return saveOdt(doc_, path, error);
    if (ext == QLatin1String("docx"))
        return saveDocx(doc_, path, error);
    if (ext == QLatin1String("doc")) {
        QTemporaryDir tmp;
        const QString odt = tmp.filePath(QStringLiteral("tmp.odt"));
        if (!saveOdt(doc_, odt, error))
            return false;
        return LokEngine::convertTo(odt, path, QStringLiteral("doc:MS Word 97"), error);
    }
    if (ext == QLatin1String("pdf"))
        return exportPdf(QJsonObject{{QStringLiteral("path"), path}})[QStringLiteral("ok")].toBool();
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
        return true;
    }
    if (ext == QLatin1String("odt"))
        return loadOdtFile(path, doc_, error);
    if (ext == QLatin1String("docx"))
        return loadDocxFile(path, doc_, error);
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
    if (exportNativePdf(doc_, pdf, &nativeErr))
        return ok();
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
        if (b.type != QLatin1String("heading"))
            continue;
        QJsonObject n;
        n[QStringLiteral("id")] = b.id;
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
    const auto idx = indexesFor(args.value(QStringLiteral("ids")).toArray());
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
    auto idx = indexesFor(args.value(QStringLiteral("ids")).toArray());
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
    if (args.contains(QStringLiteral("bold")))
        r.bold = args.value(QStringLiteral("bold")).toBool();
    if (args.contains(QStringLiteral("italic")))
        r.italic = args.value(QStringLiteral("italic")).toBool();
    if (args.contains(QStringLiteral("underline")))
        r.underline = args.value(QStringLiteral("underline")).toBool();
    if (args.contains(QStringLiteral("font")))
        r.font = args.value(QStringLiteral("font")).toString();
    if (args.contains(QStringLiteral("size_pt")))
        r.sizePt = args.value(QStringLiteral("size_pt")).toDouble();
    if (args.contains(QStringLiteral("color")))
        r.color = args.value(QStringLiteral("color")).toString();
    if (args.contains(QStringLiteral("highlight")))
        r.highlight = args.value(QStringLiteral("highlight")).toString();
}

QJsonObject Session::applyCharacterFormat(const QJsonObject &args)
{
    const auto idx = indexesFor(args.value(QStringLiteral("ids")).toArray());
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
    markChanged();
    return ok();
}

QJsonObject Session::applyParagraphFormat(const QJsonObject &args)
{
    const auto idx = indexesFor(args.value(QStringLiteral("ids")).toArray());
    if (idx.isEmpty())
        return fail(QStringLiteral("no matching ids"));
    pushUndo();
    for (int i : idx) {
        if (args.contains(QStringLiteral("align")))
            doc_.body[i].align = args.value(QStringLiteral("align")).toString();
        if (args.contains(QStringLiteral("style")))
            doc_.body[i].style = args.value(QStringLiteral("style")).toString();
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
    for (int i : indexesFor(args.value(QStringLiteral("ids")).toArray())) {
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
    pushUndo();
    if (args.contains(QStringLiteral("size")))
        doc_.page.size = args.value(QStringLiteral("size")).toString();
    if (args.contains(QStringLiteral("orientation")))
        doc_.page.orientation = args.value(QStringLiteral("orientation")).toString();
    const auto m = args.value(QStringLiteral("margins_mm")).toArray();
    for (int i = 0; i < 4 && i < m.size(); ++i)
        doc_.page.marginsMm[i] = m.at(i).toDouble();
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
    const QString kind = (kindRaw == QLatin1String("number") || kindRaw == QLatin1String("decimal"))
        ? QStringLiteral("decimal")
        : QStringLiteral("bullet");
    QJsonArray blocks;
    for (const auto &it : args.value(QStringLiteral("items")).toArray()) {
        Block b;
        b.type = QStringLiteral("list-item");
        b.listKind = kind;
        b.level = 1;
        TextRun r;
        r.text = it.toString();
        b.runs.push_back(r);
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
    b.breakKind = args.value(QStringLiteral("kind")).toString(QStringLiteral("page"));
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
    if (find.isEmpty())
        return fail(QStringLiteral("find is required"));
    pushUndo();
    int count = 0;
    auto replaceRuns = [&](QVector<TextRun> &runs) {
        for (auto &r : runs) {
            if (!r.text.contains(find))
                continue;
            if (all) {
                count += r.text.count(find);
                r.text.replace(find, repl);
            } else if (r.text.contains(find)) {
                r.text.replace(r.text.indexOf(find), find.size(), repl);
                ++count;
                return true;
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
    if (find.isEmpty())
        return fail(QStringLiteral("find is required"));
    QJsonArray hits;
    for (const auto &b : doc_.body) {
        const QString t = b.plainText();
        int from = 0;
        while (true) {
            const int i = t.indexOf(find, from, Qt::CaseInsensitive);
            if (i < 0)
                break;
            QJsonObject h;
            h[QStringLiteral("id")] = b.id;
            h[QStringLiteral("start")] = i;
            h[QStringLiteral("end")] = i + find.size();
            hits.append(h);
            from = i + qMax(1, find.size());
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
        TableCell cell;
        if (args.contains(QStringLiteral("runs"))) {
            for (const auto &v : args.value(QStringLiteral("runs")).toArray())
                cell.runs.push_back(TextRun::fromJson(v.toObject()));
        } else {
            TextRun r;
            r.text = args.value(QStringLiteral("text")).toString();
            cell.runs.push_back(r);
        }
    b->table[row][col] = cell;
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
    rest.listKind = b.listKind;
    rest.level = b.level;
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
    auto idx = indexesFor(args.value(QStringLiteral("ids")).toArray());
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
    auto idx = indexesFor(args.value(QStringLiteral("ids")).toArray());
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
    Block *b = doc_.findBlock(args.value(QStringLiteral("id")).toString());
    if (!b)
        return fail(QStringLiteral("id not found"));
    pushUndo();
    Comment c;
    c.id = QStringLiteral("c") + QUuid::createUuid().toString(QUuid::Id128).left(8);
    c.author = args.value(QStringLiteral("author")).toString(QStringLiteral("IA"));
    c.text = args.value(QStringLiteral("text")).toString();
    b->comments.push_back(c);
    markChanged();
    auto r = ok();
    r[QStringLiteral("comment_id")] = c.id;
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
    doc_.header.text = args.value(QStringLiteral("text")).toString();
    if (args.contains(QStringLiteral("page_numbers")))
        doc_.header.pageNumbers = args.value(QStringLiteral("page_numbers")).toBool();
    markChanged();
    return ok();
}

QJsonObject Session::setFooter(const QJsonObject &args)
{
    pushUndo();
    doc_.footer.text = args.value(QStringLiteral("text")).toString();
    if (args.contains(QStringLiteral("page_numbers")))
        doc_.footer.pageNumbers = args.value(QStringLiteral("page_numbers")).toBool();
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
    } else {
        return fail(QStringLiteral("template must be carta, ata, or contrato"));
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
    sel_ = {};
    if (!doc_.body.isEmpty()) {
        sel_.anchorId = sel_.focusId = doc_.body.first().id;
        sel_.anchorOff = sel_.focusOff = 0;
    }
    markChanged();
    captureBaseline();
    return ok();
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
    pushUndo();
    if (!insertTextAt(sel_.focusId, sel_.focusOff, text, sel_.tableRow, sel_.tableCol)) {
        undo_.removeLast();
        return;
    }
    sel_.focusOff = sel_.anchorOff = sel_.focusOff + text.size();
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
