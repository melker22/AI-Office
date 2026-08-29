#include "app/AppController.h"

#include "core/Preview.h"
#include "lok/LokEngine.h"

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>
#include <QUrl>

namespace aioffice {

AppController::AppController(QObject *parent)
    : QObject(parent)
    , ipc_(&session_)
{
    loadSettings();
    session_.setLive(true);
    tabSnaps_.push_back(TabSnap{session_.documentRef(), {}, false, session_.documentRef().title});
    rebuildTabsUi();
    session_.setOnChanged([this]() {
        refreshPages();
        emit documentChanged();
        emit previewChanged();
        emit selectionChanged();
        snapshotActive();
        rebuildTabsUi();
    });
    ipc_.start();
    aiFlashTimer_.setSingleShot(true);
    aiFlashTimer_.setInterval(1600);
    connect(&aiFlashTimer_, &QTimer::timeout, this, [this]() {
        aiEditedFlash_ = false;
        emit aiChanged();
    });
    connect(&ipc_, &IpcServer::toolUsed, this, [this](const QString &name) {
        aiConnected_ = true;
        lastTool_ = name;
        aiEditedFlash_ = true;
        aiFlashTimer_.start();
        welcomeVisible_ = false;
        emit aiChanged();
        emit welcomeChanged();
        refreshPages();
        emit documentChanged();
        emit previewChanged();
        emit selectionChanged();
        emit statusMessage(QStringLiteral("IA editou"));
        snapshotActive();
        rebuildTabsUi();
    });
    refreshPages();
}

QString AppController::title() const
{
    return session_.documentRef().title;
}

int AppController::words() const
{
    return session_.documentRef().wordCount();
}

bool AppController::libreOffice() const
{
    return LokEngine::available();
}

QString AppController::integrateBlurb() const
{
    return QStringLiteral("Peça para a IA se integrar ao software para aproveitar todo o poder dele.");
}

QString AppController::integratePrompt() const
{
    return QStringLiteral(
        "Integre-se ao AI Office Writer. Rode o binário aioffice com o servidor MCP (`aioffice mcp`) "
        "ou, se a GUI estiver aberta, as tools já editam o documento vivo via socket local. "
        "Leia skills/SKILL.md e docs/mcp-tools.md. Use document_get, insert_blocks, apply_style, "
        "set_page_layout, page_preview, set_cell, search e selection_get. Não peça API keys — o app não vende IA.");
}

bool AppController::tableSelected() const
{
    return !caretTableId().isEmpty();
}

const Block *AppController::caretBlock() const
{
    const auto s = session_.selection();
    if (!s.focusId.isEmpty())
        return session_.documentRef().findBlock(s.focusId);
    if (!session_.documentRef().body.isEmpty())
        return &session_.documentRef().body.first();
    return nullptr;
}

QString AppController::caretTableId() const
{
    const Block *b = caretBlock();
    if (b && b->type == QLatin1String("table"))
        return b->id;
    return {};
}

QString AppController::afterId() const
{
    const Block *b = caretBlock();
    return b ? b->id : QStringLiteral("end");
}

QString AppController::headingLabel() const
{
    const Block *b = caretBlock();
    if (!b)
        return QStringLiteral("Normal");
    if (b->type == QLatin1String("heading"))
        return QStringLiteral("Título %1").arg(b->level > 0 ? b->level : 1);
    if (b->style == QLatin1String("Quote") || b->style == QLatin1String("Citação"))
        return QStringLiteral("Citação");
    if (b->style == QLatin1String("Title"))
        return QStringLiteral("Título");
    return b->style.isEmpty() ? QStringLiteral("Normal") : b->style;
}

QString AppController::pageSize() const
{
    return session_.documentRef().page.size;
}

QString AppController::orientation() const
{
    return session_.documentRef().page.orientation;
}

QVariantList AppController::styleNames() const
{
    QVariantList list;
    for (const auto &s : session_.documentRef().styles) {
        QString label = s.name;
        if (s.name == QLatin1String("Heading 1"))
            label = QStringLiteral("Título 1");
        else if (s.name == QLatin1String("Heading 2"))
            label = QStringLiteral("Título 2");
        else if (s.name == QLatin1String("Heading 3"))
            label = QStringLiteral("Título 3");
        else if (s.name == QLatin1String("Title"))
            label = QStringLiteral("Título");
        else if (s.name == QLatin1String("Quote"))
            label = QStringLiteral("Citação");
        list.push_back(QVariantMap{
            {QStringLiteral("name"), s.name},
            {QStringLiteral("label"), label},
            {QStringLiteral("size"), s.sizePt},
            {QStringLiteral("bold"), s.bold},
            {QStringLiteral("italic"), s.italic},
        });
    }
    return list;
}

QVariantList AppController::marginsMm() const
{
    QVariantList list;
    for (double v : session_.documentRef().page.marginsMm)
        list.push_back(v);
    return list;
}

QStringList AppController::styleLabels() const
{
    QStringList list;
    for (const auto &s : styleNames())
        list.push_back(s.toMap().value(QStringLiteral("label")).toString());
    return list;
}

int AppController::tableRows() const
{
    const QString id = caretTableId();
    const Block *b = session_.documentRef().findBlock(id);
    return b ? b->table.size() : 0;
}

int AppController::tableCols() const
{
    const QString id = caretTableId();
    const Block *b = session_.documentRef().findBlock(id);
    if (!b || b->table.isEmpty())
        return 0;
    return b->table.first().size();
}

bool AppController::tableHeader() const
{
    const QString id = caretTableId();
    const Block *b = session_.documentRef().findBlock(id);
    return b && b->headerRow;
}

void AppController::setPage(int p)
{
    page_ = qBound(1, p, qMax(1, pages_));
    emit previewChanged();
}

void AppController::setZoom(double z)
{
    zoom_ = qBound(0.5, z, 2.5);
    emit previewChanged();
}

void AppController::setDarkMode(bool v)
{
    if (darkMode_ == v)
        return;
    darkMode_ = v;
    saveSettings();
    emit darkModeChanged();
}

void AppController::setWelcomeVisible(bool v)
{
    if (welcomeVisible_ == v)
        return;
    welcomeVisible_ = v;
    emit welcomeChanged();
}

void AppController::setFindVisible(bool v)
{
    if (findVisible_ == v)
        return;
    findVisible_ = v;
    emit findChanged();
}

void AppController::setFindText(const QString &t)
{
    if (findText_ == t)
        return;
    findText_ = t;
    emit findChanged();
}

void AppController::setReplaceText(const QString &t)
{
    if (replaceText_ == t)
        return;
    replaceText_ = t;
    emit findChanged();
}

void AppController::setInspectorTab(int t)
{
    t = qBound(0, t, 1);
    if (inspectorTab_ == t)
        return;
    inspectorTab_ = t;
    emit inspectorTabChanged();
}

QJsonObject AppController::formatArgs() const
{
    const auto s = session_.selection();
    QString id = s.focusId;
    if (id.isEmpty() && !session_.documentRef().body.isEmpty())
        id = session_.documentRef().body.first().id;
    QJsonObject o;
    o[QStringLiteral("ids")] = QJsonArray{id};
    if (s.anchorId == s.focusId && !id.isEmpty()) {
        const int a = qMin(s.anchorOff, s.focusOff);
        const int b = qMax(s.anchorOff, s.focusOff);
        if (b > a) {
            o[QStringLiteral("start")] = a;
            o[QStringLiteral("end")] = b;
        }
    }
    return o;
}

void AppController::setFontSizePt(int pt)
{
    pt = qBound(8, pt, 72);
    if (fontSizePt_ == pt)
        return;
    fontSizePt_ = pt;
    auto args = formatArgs();
    args[QStringLiteral("size_pt")] = fontSizePt_;
    session_.applyCharacterFormat(args);
}

void AppController::setPageSize(const QString &s)
{
    session_.setPageLayout({{QStringLiteral("size"), s}});
}

void AppController::setOrientation(const QString &s)
{
    session_.setPageLayout({{QStringLiteral("orientation"), s}});
}

void AppController::setSaveFormat(const QString &f)
{
    if (saveFormat_ == f)
        return;
    saveFormat_ = f;
    emit saveFormatChanged();
    if (f == QLatin1String("doc") && !libreOffice())
        showLoHint();
}

void AppController::setMarginMm(int edge, double mm)
{
    auto page = session_.documentRef().page;
    if (edge < 0 || edge > 3)
        return;
    page.marginsMm[edge] = mm;
    QJsonArray arr;
    for (double v : page.marginsMm)
        arr.append(v);
    session_.setPageLayout({{QStringLiteral("margins_mm"), arr}});
}

void AppController::toggleDark()
{
    setDarkMode(!darkMode_);
}

void AppController::refreshPages()
{
    const auto prev = renderPage(session_.documentRef(), page_, zoom_);
    pages_ = prev.pages;
    page_ = qBound(1, page_, pages_);
}

void AppController::handleResult(const QJsonObject &r, const QString &okStatus)
{
    if (!r.value(QStringLiteral("ok")).toBool()) {
        emit errorMessage(r.value(QStringLiteral("error")).toString());
        return;
    }
    if (!okStatus.isEmpty())
        emit statusMessage(okStatus);
}

void AppController::newDocument()
{
    if (gateUnsaved(QStringLiteral("new")))
        return;
    welcomeVisible_ = false;
    inspectorTab_ = 0;
    emit welcomeChanged();
    emit inspectorTabChanged();
    handleResult(session_.newDocument({{QStringLiteral("force"), true}}), QStringLiteral("Novo documento"));
    snapshotActive();
    rebuildTabsUi();
}

void AppController::openPath(const QString &path)
{
    if (gateUnsaved(QStringLiteral("open")))
        return;
    QString p = path;
    if (p.startsWith(QLatin1String("file:")))
        p = QUrl(p).toLocalFile();
    welcomeVisible_ = false;
    inspectorTab_ = 0;
    emit welcomeChanged();
    emit inspectorTabChanged();
    handleResult(session_.openDocument({{QStringLiteral("path"), p}}), QStringLiteral("Documento aberto"));
    if (!session_.path().isEmpty())
        rememberRecent(session_.path());
    snapshotActive();
    rebuildTabsUi();
}

void AppController::savePath(const QString &path)
{
    QString p = path;
    if (p.startsWith(QLatin1String("file:")))
        p = QUrl(p).toLocalFile();
    handleResult(session_.saveDocument({{QStringLiteral("path"), p}}), QStringLiteral("Documento salvo"));
    if (!session_.path().isEmpty())
        rememberRecent(session_.path());
}

void AppController::save()
{
    if (session_.path().isEmpty()) {
        emit needSaveAs();
        return;
    }
    handleResult(session_.saveDocument({}), QStringLiteral("Documento salvo"));
}

void AppController::exportPdf(const QString &path)
{
    QString p = path;
    if (p.startsWith(QLatin1String("file:")))
        p = QUrl(p).toLocalFile();
    handleResult(session_.exportPdf({{QStringLiteral("path"), p}}), QStringLiteral("PDF exportado"));
}

void AppController::undo()
{
    handleResult(session_.undo({}), {});
}

void AppController::redo()
{
    handleResult(session_.redo({}), {});
}

void AppController::applyBold()
{
    auto args = formatArgs();
    args[QStringLiteral("bold")] = true;
    session_.applyCharacterFormat(args);
}

void AppController::applyItalic()
{
    auto args = formatArgs();
    args[QStringLiteral("italic")] = true;
    session_.applyCharacterFormat(args);
}

void AppController::applyUnderline()
{
    auto args = formatArgs();
    args[QStringLiteral("underline")] = true;
    session_.applyCharacterFormat(args);
}

void AppController::setAlign(const QString &align)
{
    auto args = formatArgs();
    args[QStringLiteral("align")] = align;
    session_.applyParagraphFormat(args);
}

void AppController::applyHeading(int level)
{
    applyNamedStyle(QStringLiteral("Heading %1").arg(level));
}

void AppController::applyNamedStyle(const QString &name)
{
    const Block *b = caretBlock();
    if (!b)
        return;
    QString style = name;
    if (name.startsWith(QStringLiteral("Título ")))
        style = QStringLiteral("Heading ") + name.mid(7);
    else if (name == QLatin1String("Título"))
        style = QStringLiteral("Title");
    else if (name == QLatin1String("Citação"))
        style = QStringLiteral("Quote");
    session_.applyStyle({{QStringLiteral("ids"), QJsonArray{b->id}},
                         {QStringLiteral("style"), style}});
}

void AppController::insertParagraph()
{
    session_.insertBlocks({{QStringLiteral("after_id"), afterId()},
                           {QStringLiteral("blocks"),
                            QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("paragraph")},
                                                   {QStringLiteral("runs"),
                                                    QJsonArray{QJsonObject{{QStringLiteral("text"), QString()}}}}}}
                           }});
}

void AppController::insertTable()
{
    welcomeVisible_ = false;
    emit welcomeChanged();
    session_.insertTable({{QStringLiteral("after_id"), afterId()},
                          {QStringLiteral("rows"), 3},
                          {QStringLiteral("cols"), 3},
                          {QStringLiteral("header"), true}});
}

void AppController::insertList(const QString &kind)
{
    welcomeVisible_ = false;
    emit welcomeChanged();
    session_.insertList({{QStringLiteral("after_id"), afterId()},
                         {QStringLiteral("kind"), kind},
                         {QStringLiteral("items"), QJsonArray{QStringLiteral("Item 1"), QStringLiteral("Item 2"),
                                                              QStringLiteral("Item 3")}}});
}

void AppController::insertBreak()
{
    session_.insertBreak({{QStringLiteral("after_id"), afterId()}});
}

void AppController::insertImage(const QString &path)
{
    QString p = path;
    if (p.startsWith(QLatin1String("file:")))
        p = QUrl(p).toLocalFile();
    session_.insertImage({{QStringLiteral("after_id"), afterId()}, {QStringLiteral("path"), p}});
}

void AppController::deleteTable()
{
    const QString id = caretTableId();
    if (id.isEmpty())
        return;
    session_.deleteRange({{QStringLiteral("ids"), QJsonArray{id}}});
}

QVariantList AppController::outline() const
{
    QVariantList list;
    for (const auto &b : session_.documentRef().body) {
        if (b.type != QLatin1String("heading"))
            continue;
        list.push_back(QVariantMap{
            {QStringLiteral("id"), b.id},
            {QStringLiteral("level"), b.level},
            {QStringLiteral("text"), b.plainText()},
        });
    }
    return list;
}

QString AppController::copyIntegratePrompt()
{
    if (auto *clip = QGuiApplication::clipboard())
        clip->setText(integratePrompt());
    emit statusMessage(QStringLiteral("Instrução copiada"));
    return integratePrompt();
}

void AppController::findReplace(bool all)
{
    if (findText_.isEmpty())
        return;
    handleResult(session_.findReplace({{QStringLiteral("find"), findText_},
                                       {QStringLiteral("replace"), replaceText_},
                                       {QStringLiteral("all"), all}}),
                 {});
}

void AppController::rememberRecent(const QString &path)
{
    QVariantMap item;
    item[QStringLiteral("path")] = path;
    item[QStringLiteral("name")] = QFileInfo(path).fileName();
    item[QStringLiteral("ext")] = QFileInfo(path).suffix().toUpper();
    QVariantList next;
    next.push_back(item);
    for (const auto &r : recents_) {
        if (r.toMap().value(QStringLiteral("path")).toString() == path)
            continue;
        next.push_back(r);
        if (next.size() >= 8)
            break;
    }
    recents_ = next;
    saveSettings();
    emit recentsChanged();
}

void AppController::loadSettings()
{
    QSettings s;
    darkMode_ = s.value(QStringLiteral("darkMode"), false).toBool();
    const int n = s.beginReadArray(QStringLiteral("recents"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        QVariantMap item;
        item[QStringLiteral("path")] = s.value(QStringLiteral("path"));
        item[QStringLiteral("name")] = s.value(QStringLiteral("name"));
        item[QStringLiteral("ext")] = s.value(QStringLiteral("ext"));
        recents_.push_back(item);
    }
    s.endArray();
}

void AppController::saveSettings() const
{
    QSettings s;
    s.setValue(QStringLiteral("darkMode"), darkMode_);
    s.beginWriteArray(QStringLiteral("recents"), recents_.size());
    for (int i = 0; i < recents_.size(); ++i) {
        s.setArrayIndex(i);
        const auto m = recents_[i].toMap();
        s.setValue(QStringLiteral("path"), m.value(QStringLiteral("path")));
        s.setValue(QStringLiteral("name"), m.value(QStringLiteral("name")));
        s.setValue(QStringLiteral("ext"), m.value(QStringLiteral("ext")));
    }
    s.endArray();
}

void AppController::placeCaret(const QString &id, int offset, int row, int col, bool extend)
{
    Selection s = session_.selection();
    if (!extend) {
        s.anchorId = id;
        s.anchorOff = offset;
    }
    s.focusId = id;
    s.focusOff = offset;
    s.tableRow = row;
    s.tableCol = col;
    session_.setSelection(s);
    emit selectionChanged();
    emit previewChanged();
}

void AppController::notifyEdited()
{
    refreshPages();
    emit documentChanged();
    emit previewChanged();
    emit selectionChanged();
}

QVariantList AppController::comments() const
{
    QVariantList list;
    for (const auto &b : session_.documentRef().body) {
        for (const auto &c : b.comments) {
            list.push_back(QVariantMap{
                {QStringLiteral("id"), c.id},
                {QStringLiteral("block_id"), b.id},
                {QStringLiteral("author"), c.author},
                {QStringLiteral("text"), c.text},
                {QStringLiteral("resolved"), c.resolved},
            });
        }
    }
    return list;
}

QString AppController::headerText() const
{
    return session_.documentRef().header.text;
}

QString AppController::footerText() const
{
    return session_.documentRef().footer.text;
}

bool AppController::pageNumbers() const
{
    return session_.documentRef().header.pageNumbers || session_.documentRef().footer.pageNumbers;
}

void AppController::setHeaderText(const QString &t)
{
    session_.setHeader({{QStringLiteral("text"), t},
                        {QStringLiteral("page_numbers"), session_.documentRef().header.pageNumbers}});
}

void AppController::setFooterText(const QString &t)
{
    session_.setFooter({{QStringLiteral("text"), t},
                        {QStringLiteral("page_numbers"), session_.documentRef().footer.pageNumbers}});
}

void AppController::setPageNumbers(bool v)
{
    session_.setHeader({{QStringLiteral("text"), session_.documentRef().header.text},
                        {QStringLiteral("page_numbers"), v}});
    session_.setFooter({{QStringLiteral("text"), session_.documentRef().footer.text},
                        {QStringLiteral("page_numbers"), v}});
}

void AppController::insertRow()
{
    const QString id = caretTableId();
    if (id.isEmpty())
        return;
    session_.insertRow({{QStringLiteral("id"), id},
                        {QStringLiteral("row"), session_.selection().tableRow + 1}});
}

void AppController::insertCol()
{
    const QString id = caretTableId();
    if (id.isEmpty())
        return;
    session_.insertCol({{QStringLiteral("id"), id},
                        {QStringLiteral("col"), session_.selection().tableCol + 1}});
}

void AppController::deleteRow()
{
    const QString id = caretTableId();
    if (id.isEmpty())
        return;
    session_.deleteRow({{QStringLiteral("id"), id}, {QStringLiteral("row"), qMax(0, session_.selection().tableRow)}});
}

void AppController::deleteCol()
{
    const QString id = caretTableId();
    if (id.isEmpty())
        return;
    session_.deleteCol({{QStringLiteral("id"), id}, {QStringLiteral("col"), qMax(0, session_.selection().tableCol)}});
}

void AppController::toggleTableHeader()
{
    session_.toggleTableHeader();
}

void AppController::jumpToBlock(const QString &id)
{
    DocumentLayout lay;
    lay.relayout(session_.documentRef(), zoom_);
    const int pg = lay.pageOf(id);
    if (pg > 0)
        setPage(pg);
    placeCaret(id, 0, -1, -1, false);
}

void AppController::findNext()
{
    jumpFind(true);
}

void AppController::findPrev()
{
    jumpFind(false);
}

void AppController::jumpFind(bool forward)
{
    if (findText_.isEmpty())
        return;
    const auto r = session_.search({{QStringLiteral("find"), findText_}, {QStringLiteral("forward"), forward}});
    const auto hits = r.value(QStringLiteral("hits")).toArray();
    if (hits.isEmpty()) {
        emit statusMessage(QStringLiteral("Nenhuma ocorrência"));
        return;
    }
    const Selection sel = session_.selection();
    int pick = 0;
    for (int i = 0; i < hits.size(); ++i) {
        const auto h = hits.at(i).toObject();
        if (h.value(QStringLiteral("id")).toString() == sel.focusId
            && h.value(QStringLiteral("start")).toInt() > sel.focusOff) {
            pick = i;
            break;
        }
        if (h.value(QStringLiteral("id")).toString() != sel.focusId && pick == 0
            && session_.documentRef().indexOf(h.value(QStringLiteral("id")).toString())
                   > session_.documentRef().indexOf(sel.focusId))
            pick = i;
    }
    if (!forward)
        pick = hits.size() - 1;
    const auto h = hits.at(pick).toObject();
    placeCaret(h.value(QStringLiteral("id")).toString(), h.value(QStringLiteral("end")).toInt(), -1, -1, false);
    Selection s = session_.selection();
    s.anchorId = h.value(QStringLiteral("id")).toString();
    s.anchorOff = h.value(QStringLiteral("start")).toInt();
    session_.setSelection(s);
    jumpToBlock(s.focusId);
}

void AppController::snapshotActive()
{
    if (tabSnaps_.isEmpty())
        return;
    auto &t = tabSnaps_[activeTab_];
    t.doc = session_.documentRef();
    t.path = session_.path();
    t.dirty = session_.dirty();
    t.title = session_.documentRef().title;
}

void AppController::loadTab(int index)
{
    if (index < 0 || index >= tabSnaps_.size())
        return;
    activeTab_ = index;
    const auto &t = tabSnaps_[index];
    session_.restoreSnapshot(t.doc, t.path, t.dirty);
    welcomeVisible_ = false;
    emit welcomeChanged();
    rebuildTabsUi();
}

void AppController::rebuildTabsUi()
{
    QVariantList list;
    for (int i = 0; i < tabSnaps_.size(); ++i) {
        const auto &t = tabSnaps_[i];
        QString title = t.title.isEmpty() ? QStringLiteral("Sem título") : t.title;
        if (i == activeTab_) {
            title = session_.documentRef().title.isEmpty() ? QStringLiteral("Sem título")
                                                           : session_.documentRef().title;
        }
        list.push_back(QVariantMap{
            {QStringLiteral("title"), title},
            {QStringLiteral("dirty"), i == activeTab_ ? session_.dirty() : t.dirty},
            {QStringLiteral("active"), i == activeTab_},
        });
    }
    tabsUi_ = list;
    emit tabsChanged();
}

bool AppController::gateUnsaved(const QString &action)
{
    if (!session_.dirty())
        return false;
    pendingAction_ = action;
    unsavedVisible_ = true;
    emit sheetsChanged();
    return true;
}

void AppController::runPending()
{
    const QString a = pendingAction_;
    pendingAction_.clear();
    if (a == QLatin1String("new"))
        newDocument();
    else if (a == QLatin1String("open"))
        emit statusMessage(QStringLiteral("Escolha o arquivo"));
    else if (a == QLatin1String("tab"))
        newTab();
    else if (a == QLatin1String("switch") && pendingTab_ >= 0)
        switchTab(pendingTab_);
}

void AppController::newTab()
{
    if (gateUnsaved(QStringLiteral("tab")))
        return;
    snapshotActive();
    TabSnap blank;
    blank.doc = Document::blank();
    blank.title = blank.doc.title;
    tabSnaps_.push_back(blank);
    activeTab_ = tabSnaps_.size() - 1;
    session_.restoreSnapshot(blank.doc, {}, false);
    welcomeVisible_ = false;
    emit welcomeChanged();
    rebuildTabsUi();
}

void AppController::switchTab(int index)
{
    if (index == activeTab_ || index < 0 || index >= tabSnaps_.size())
        return;
    if (session_.dirty()) {
        pendingAction_ = QStringLiteral("switch");
        pendingTab_ = index;
        unsavedVisible_ = true;
        emit sheetsChanged();
        return;
    }
    snapshotActive();
    loadTab(index);
}

void AppController::closeTab(int index)
{
    if (tabSnaps_.size() <= 1)
        return;
    if (index == activeTab_ && session_.dirty()) {
        pendingAction_ = QStringLiteral("close");
        pendingTab_ = index;
        unsavedVisible_ = true;
        emit sheetsChanged();
        return;
    }
    tabSnaps_.removeAt(index);
    if (activeTab_ >= tabSnaps_.size())
        activeTab_ = tabSnaps_.size() - 1;
    loadTab(activeTab_);
}

void AppController::confirmSave()
{
    unsavedVisible_ = false;
    emit sheetsChanged();
    if (session_.path().isEmpty()) {
        emit needSaveAs();
        return;
    }
    save();
    runPending();
}

void AppController::confirmDiscard()
{
    unsavedVisible_ = false;
    emit sheetsChanged();
    session_.newDocument({{QStringLiteral("force"), true}});
    if (pendingAction_ == QLatin1String("close") && pendingTab_ >= 0) {
        tabSnaps_.removeAt(pendingTab_);
        pendingTab_ = -1;
        pendingAction_.clear();
        if (activeTab_ >= tabSnaps_.size())
            activeTab_ = tabSnaps_.size() - 1;
        loadTab(activeTab_);
        return;
    }
    runPending();
}

void AppController::cancelUnsaved()
{
    unsavedVisible_ = false;
    pendingAction_.clear();
    pendingTab_ = -1;
    emit sheetsChanged();
}

void AppController::showLoHint()
{
    loHintVisible_ = true;
    emit sheetsChanged();
}

void AppController::hideLoHint()
{
    loHintVisible_ = false;
    emit sheetsChanged();
}

void AppController::resolveComment(const QString &commentId)
{
    session_.commentResolve({{QStringLiteral("comment_id"), commentId}});
}

} // namespace aioffice
