#pragma once

#include "core/Document.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <functional>

namespace aioffice {

class Session {
public:
    Session();

    Document document() const { return doc_; }
    const Document &documentRef() const { return doc_; }
    QString path() const { return path_; }
    bool dirty() const { return dirty_; }
    bool liveConnected() const { return live_; }
    void setLive(bool v) { live_ = v; }
    Selection selection() const { return sel_; }
    void setSelection(const Selection &s) { sel_ = s; }

    QJsonObject newDocument(const QJsonObject &args);
    QJsonObject openDocument(const QJsonObject &args);
    QJsonObject saveDocument(const QJsonObject &args);
    QJsonObject exportPdf(const QJsonObject &args);
    QJsonObject getDocument(const QJsonObject &args);
    QJsonObject getOutline(const QJsonObject &args);
    QJsonObject insertBlocks(const QJsonObject &args);
    QJsonObject replaceRange(const QJsonObject &args);
    QJsonObject deleteRange(const QJsonObject &args);
    QJsonObject applyCharacterFormat(const QJsonObject &args);
    QJsonObject applyParagraphFormat(const QJsonObject &args);
    QJsonObject applyStyle(const QJsonObject &args);
    QJsonObject createStyle(const QJsonObject &args);
    QJsonObject setPageLayout(const QJsonObject &args);
    QJsonObject insertTable(const QJsonObject &args);
    QJsonObject insertImage(const QJsonObject &args);
    QJsonObject insertList(const QJsonObject &args);
    QJsonObject insertBreak(const QJsonObject &args);
    QJsonObject findReplace(const QJsonObject &args);
    QJsonObject search(const QJsonObject &args);
    QJsonObject undo(const QJsonObject &args);
    QJsonObject redo(const QJsonObject &args);
    QJsonObject pagePreview(const QJsonObject &args);
    QJsonObject caretGeometry(const QJsonObject &args);
    QJsonObject hitTestTool(const QJsonObject &args);
    QJsonObject caretMove(const QJsonObject &args);
    QJsonObject integrateHelp(const QJsonObject &args);
    QJsonObject setCell(const QJsonObject &args);
    QJsonObject insertRow(const QJsonObject &args);
    QJsonObject insertCol(const QJsonObject &args);
    QJsonObject deleteRow(const QJsonObject &args);
    QJsonObject deleteCol(const QJsonObject &args);
    QJsonObject selectionGet(const QJsonObject &args);
    QJsonObject setSelectionTool(const QJsonObject &args);
    QJsonObject insertAtCaret(const QJsonObject &args);
    QJsonObject moveBlocks(const QJsonObject &args);
    QJsonObject duplicateBlocks(const QJsonObject &args);
    QJsonObject commentAdd(const QJsonObject &args);
    QJsonObject commentResolve(const QJsonObject &args);
    QJsonObject documentSetTitle(const QJsonObject &args);
    QJsonObject setHeader(const QJsonObject &args);
    QJsonObject setFooter(const QJsonObject &args);
    QJsonObject documentDiff(const QJsonObject &args);
    QJsonObject importMarkdown(const QJsonObject &args);
    QJsonObject documentFromTemplate(const QJsonObject &args);

    QJsonObject linkSet(const QJsonObject &args);
    QJsonObject bookmarkAdd(const QJsonObject &args);
    QJsonObject insertCrossref(const QJsonObject &args);
    QJsonObject insertField(const QJsonObject &args);
    QJsonObject insertFootnote(const QJsonObject &args);
    QJsonObject insertEndnote(const QJsonObject &args);
    QJsonObject tocUpdate(const QJsonObject &args);
    QJsonObject spellcheck(const QJsonObject &args);
    QJsonObject trackChangesSet(const QJsonObject &args);
    QJsonObject revisionList(const QJsonObject &args);
    QJsonObject revisionAccept(const QJsonObject &args);
    QJsonObject revisionReject(const QJsonObject &args);
    QJsonObject revisionAcceptAll(const QJsonObject &args);
    QJsonObject revisionRejectAll(const QJsonObject &args);
    QJsonObject autocorrectGet(const QJsonObject &args);
    QJsonObject autocorrectSet(const QJsonObject &args);
    QJsonObject mergeCells(const QJsonObject &args);
    QJsonObject splitCells(const QJsonObject &args);
    QJsonObject tableSort(const QJsonObject &args);
    QJsonObject setImage(const QJsonObject &args);
    QJsonObject insertCaption(const QJsonObject &args);
    QJsonObject nestTable(const QJsonObject &args);
    QJsonObject mailMerge(const QJsonObject &args);
    QJsonObject clearFormat(const QJsonObject &args);
    QJsonObject toggleCheck(const QJsonObject &args);
    QJsonObject insertSpecial(const QJsonObject &args);
    QJsonObject paintFormat(const QJsonObject &args);
    QJsonObject gotoBlock(const QJsonObject &args);
    QJsonObject selectByStyle(const QJsonObject &args);
    QJsonObject updateStyle(const QJsonObject &args);
    QJsonObject insertLabels(const QJsonObject &args);
    QJsonObject splitBlockTool(const QJsonObject &args);
    QJsonObject joinBlocks(const QJsonObject &args);
    QJsonObject insertHtml(const QJsonObject &args);
    QJsonObject insertPlain(const QJsonObject &args);
    QJsonObject insertFrame(const QJsonObject &args);
    QJsonObject autoFormat(const QJsonObject &args);
    QJsonObject loadStyles(const QJsonObject &args);
    QJsonObject promoteHeading(const QJsonObject &args);
    QJsonObject wrapList(const QJsonObject &args);
    QJsonObject unwrapList(const QJsonObject &args);
    QJsonObject textToTable(const QJsonObject &args);
    QJsonObject tableToText(const QJsonObject &args);
    QJsonObject splitTable(const QJsonObject &args);
    QJsonObject exportMarkdown(const QJsonObject &args);
    QJsonObject exportText(const QJsonObject &args);
    QJsonObject copyMarkdown(const QJsonObject &args);
    QJsonObject exportHtml(const QJsonObject &args);
    QJsonObject convertNote(const QJsonObject &args);
    QJsonObject setVars(const QJsonObject &args);
    QJsonObject setReadOnly(const QJsonObject &args);
    QJsonObject insertConcordance(const QJsonObject &args);
    QJsonObject tableFormulas(const QJsonObject &args);
    QJsonObject tableAutoWidth(const QJsonObject &args);

    QJsonObject insertShape(const QJsonObject &args);
    QJsonObject insertChart(const QJsonObject &args);
    QJsonObject insertQr(const QJsonObject &args);
    QJsonObject insertEquation(const QJsonObject &args);
    QJsonObject insertForm(const QJsonObject &args);
    QJsonObject bibliographySet(const QJsonObject &args);
    QJsonObject insertCitation(const QJsonObject &args);
    QJsonObject figureIndex(const QJsonObject &args);
    QJsonObject autotextSet(const QJsonObject &args);
    QJsonObject autotextInsert(const QJsonObject &args);
    QJsonObject documentStats(const QJsonObject &args);
    QJsonObject documentCompare(const QJsonObject &args);
    QJsonObject documentA11y(const QJsonObject &args);
    QJsonObject redactRuns(const QJsonObject &args);
    QJsonObject snapshotSave(const QJsonObject &args);
    QJsonObject snapshotList(const QJsonObject &args);
    QJsonObject snapshotRestore(const QJsonObject &args);
    QJsonObject setUserDic(const QJsonObject &args);
    QJsonObject thesaurusGet(const QJsonObject &args);
    QJsonObject thesaurusSet(const QJsonObject &args);
    QJsonObject grammarCheck(const QJsonObject &args);
    QJsonObject documentLinks(const QJsonObject &args);
    QJsonObject alignObjects(const QJsonObject &args);
    QJsonObject revisionView(const QJsonObject &args);
    QJsonObject imageLibraryAdd(const QJsonObject &args);
    QJsonObject imageLibraryList(const QJsonObject &args);
    QJsonObject imageLibraryInsert(const QJsonObject &args);
    QJsonObject insertBibliography(const QJsonObject &args);
    QJsonObject documentInclude(const QJsonObject &args);

    QJsonObject validate(const QJsonObject &args);
    QJsonObject batch(const QJsonObject &args);
    QJsonObject checkpoint(const QJsonObject &args);
    QJsonObject restoreCheckpoint(const QJsonObject &args);
    QJsonObject insertMarkdown(const QJsonObject &args);

    QJsonObject call(const QString &tool, const QJsonObject &args);

    bool toggleTableHeader();
    using ChangeCallback = std::function<void()>;
    void setOnChanged(ChangeCallback cb) { onChanged_ = std::move(cb); }

    bool insertTextAt(const QString &id, int offset, const QString &text, int row = -1, int col = -1);
    bool deleteBackward(const QString &id, int offset, int row = -1, int col = -1);
    bool splitBlock(const QString &id, int offset);
    bool bumpListLevel(const QString &id, int delta);
    void restoreSnapshot(const Document &d, const QString &path, bool dirty);
    void commitCaretInsert(const QString &text);
    void commitBackspace();
    void commitSplit();

private:
    void pushUndo();
    void markChanged();
    void captureBaseline();
    void stripLeadingEmptyParagraph();
    void selectInserted(const QString &id);
    QJsonObject ok() const;
    QJsonObject fail(const QString &err) const;
    int insertIndex(const QString &afterId, QString *err) const;
    QVector<int> indexesFor(const QJsonArray &ids) const;
    QJsonArray idList(const QJsonObject &args) const;
    QJsonArray idListOrCaret(const QJsonObject &args, bool spanSelection) const;
    QJsonObject writeExport(const QJsonObject &args, const QString &payload, const QString &key);
    bool saveTo(const QString &path, QString *error);
    bool loadFrom(const QString &path, QString *error);
    Block *tableBlock(const QString &id);
    QVector<TextRun> *runsAt(Block &b, int row, int col);
    void shiftRevisionOffsets(const QString &blockId, int atOffset, int delta, const QString &excludeId = QString());
    QJsonObject dispatchTool(const QString &tool, const QJsonObject &args);

    Document doc_;
    Document baseline_;
    QString path_;
    bool dirty_ = false;
    bool live_ = false;
    int undoSuppress_ = 0;
    bool hasCheckpoint_ = false;
    Document checkpointDoc_;
    QString checkpointPath_;
    bool checkpointDirty_ = false;
    Selection checkpointSel_;
    QVector<Document> undo_;
    QVector<Document> redo_;
    Selection sel_;
    ChangeCallback onChanged_;
    QString lastTool_;
    QJsonObject lastArgs_;
    QString revisionView_;
    struct Snap {
        QString name;
        Document doc;
        QString path;
        bool dirty = false;
        Selection sel;
    };
    QVector<Snap> snaps_;
};

QJsonObject toolResultOk(const QJsonObject &extra = {});
QJsonObject toolResultErr(const QString &error);

} // namespace aioffice
