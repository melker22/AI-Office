#pragma once

#include "core/Session.h"
#include "mcp/Ipc.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

namespace aioffice {

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY documentChanged)
    Q_PROPERTY(QString path READ path NOTIFY documentChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY documentChanged)
    Q_PROPERTY(int words READ words NOTIFY documentChanged)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY previewChanged)
    Q_PROPERTY(int pages READ pages NOTIFY previewChanged)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY previewChanged)
    Q_PROPERTY(bool aiConnected READ aiConnected NOTIFY aiChanged)
    Q_PROPERTY(QString lastTool READ lastTool NOTIFY aiChanged)
    Q_PROPERTY(bool aiEditedFlash READ aiEditedFlash NOTIFY aiChanged)
    Q_PROPERTY(bool libreOffice READ libreOffice NOTIFY documentChanged)
    Q_PROPERTY(QString integrateBlurb READ integrateBlurb CONSTANT)
    Q_PROPERTY(QString integratePrompt READ integratePrompt CONSTANT)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(bool welcomeVisible READ welcomeVisible WRITE setWelcomeVisible NOTIFY welcomeChanged)
    Q_PROPERTY(bool findVisible READ findVisible WRITE setFindVisible NOTIFY findChanged)
    Q_PROPERTY(QString findText READ findText WRITE setFindText NOTIFY findChanged)
    Q_PROPERTY(QString replaceText READ replaceText WRITE setReplaceText NOTIFY findChanged)
    Q_PROPERTY(int inspectorTab READ inspectorTab WRITE setInspectorTab NOTIFY inspectorTabChanged)
    Q_PROPERTY(bool tableSelected READ tableSelected NOTIFY selectionChanged)
    Q_PROPERTY(QString headingLabel READ headingLabel NOTIFY selectionChanged)
    Q_PROPERTY(int fontSizePt READ fontSizePt WRITE setFontSizePt NOTIFY documentChanged)
    Q_PROPERTY(QString pageSize READ pageSize WRITE setPageSize NOTIFY documentChanged)
    Q_PROPERTY(QString orientation READ orientation WRITE setOrientation NOTIFY documentChanged)
    Q_PROPERTY(QVariantList recentFiles READ recentFiles NOTIFY recentsChanged)
    Q_PROPERTY(QString saveFormat READ saveFormat WRITE setSaveFormat NOTIFY saveFormatChanged)
    Q_PROPERTY(QVariantList styleNames READ styleNames NOTIFY documentChanged)
    Q_PROPERTY(QStringList styleLabels READ styleLabels NOTIFY documentChanged)
    Q_PROPERTY(QVariantList marginsMm READ marginsMm NOTIFY documentChanged)
    Q_PROPERTY(int tableRows READ tableRows NOTIFY selectionChanged)
    Q_PROPERTY(int tableCols READ tableCols NOTIFY selectionChanged)
    Q_PROPERTY(bool tableHeader READ tableHeader NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(int activeTab READ activeTab NOTIFY tabsChanged)
    Q_PROPERTY(bool unsavedVisible READ unsavedVisible NOTIFY sheetsChanged)
    Q_PROPERTY(bool loHintVisible READ loHintVisible NOTIFY sheetsChanged)
    Q_PROPERTY(QVariantList comments READ comments NOTIFY documentChanged)
    Q_PROPERTY(QString headerText READ headerText WRITE setHeaderText NOTIFY documentChanged)
    Q_PROPERTY(QString footerText READ footerText WRITE setFooterText NOTIFY documentChanged)
    Q_PROPERTY(bool pageNumbers READ pageNumbers WRITE setPageNumbers NOTIFY documentChanged)
    Q_PROPERTY(bool findRegex READ findRegex WRITE setFindRegex NOTIFY findChanged)
    Q_PROPERTY(int pageColumns READ pageColumns WRITE setPageColumns NOTIFY documentChanged)
    Q_PROPERTY(bool hyphenate READ hyphenate WRITE setHyphenate NOTIFY documentChanged)
    Q_PROPERTY(bool trackChanges READ trackChanges WRITE setTrackChanges NOTIFY documentChanged)
    Q_PROPERTY(QString linkUrl READ linkUrl WRITE setLinkUrl NOTIFY linkChanged)
    Q_PROPERTY(QVariantList revisions READ revisions NOTIFY documentChanged)
    Q_PROPERTY(QVariantList bookmarks READ bookmarks NOTIFY documentChanged)
    Q_PROPERTY(QVariantList spellHits READ spellHits NOTIFY documentChanged)

public:
    explicit AppController(QObject *parent = nullptr);

    Session *session() { return &session_; }
    const Document &document() const { return session_.documentRef(); }

    QString title() const;
    QString path() const { return session_.path(); }
    bool dirty() const { return session_.dirty(); }
    int words() const;
    int page() const { return page_; }
    int pages() const { return pages_; }
    double zoom() const { return zoom_; }
    bool aiConnected() const { return aiConnected_; }
    QString lastTool() const { return lastTool_; }
    bool aiEditedFlash() const { return aiEditedFlash_; }
    bool libreOffice() const;
    QString integrateBlurb() const;
    QString integratePrompt() const;
    bool darkMode() const { return darkMode_; }
    bool welcomeVisible() const { return welcomeVisible_; }
    bool findVisible() const { return findVisible_; }
    QString findText() const { return findText_; }
    QString replaceText() const { return replaceText_; }
    int inspectorTab() const { return inspectorTab_; }
    bool tableSelected() const;
    QString headingLabel() const;
    int fontSizePt() const { return fontSizePt_; }
    QString pageSize() const;
    QString orientation() const;
    QVariantList recentFiles() const { return recents_; }
    QString saveFormat() const { return saveFormat_; }
    QVariantList styleNames() const;
    QStringList styleLabels() const;
    QVariantList marginsMm() const;
    int tableRows() const;
    int tableCols() const;
    bool tableHeader() const;
    QVariantList tabs() const { return tabsUi_; }
    int activeTab() const { return activeTab_; }
    bool unsavedVisible() const { return unsavedVisible_; }
    bool loHintVisible() const { return loHintVisible_; }
    QVariantList comments() const;
    QString headerText() const;
    QString footerText() const;
    bool pageNumbers() const;

    bool findRegex() const { return findRegex_; }
    int pageColumns() const;
    bool hyphenate() const;
    bool trackChanges() const;
    QString linkUrl() const { return linkUrl_; }
    QVariantList revisions() const;
    QVariantList bookmarks() const;
    QVariantList spellHits() const { return spellHits_; }

    void setPage(int p);
    void setZoom(double z);
    void setDarkMode(bool v);
    void setWelcomeVisible(bool v);
    void setFindVisible(bool v);
    void setFindText(const QString &t);
    void setReplaceText(const QString &t);
    void setInspectorTab(int t);
    void setFontSizePt(int pt);
    void setPageSize(const QString &s);
    void setOrientation(const QString &s);
    void setSaveFormat(const QString &f);
    void setHeaderText(const QString &t);
    void setFooterText(const QString &t);
    void setPageNumbers(bool v);
    void setFindRegex(bool v);
    void setPageColumns(int n);
    void setHyphenate(bool v);
    void setTrackChanges(bool v);
    void setLinkUrl(const QString &u);

    void placeCaret(const QString &id, int offset, int row, int col, bool extend);
    void notifyEdited();

    Q_INVOKABLE void newDocument();
    Q_INVOKABLE void openPath(const QString &path);
    Q_INVOKABLE void savePath(const QString &path);
    Q_INVOKABLE void save();
    Q_INVOKABLE void exportPdf(const QString &path);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void applyBold();
    Q_INVOKABLE void applyItalic();
    Q_INVOKABLE void applyUnderline();
    Q_INVOKABLE void applyStrike();
    Q_INVOKABLE void applySuper();
    Q_INVOKABLE void applySub();
    Q_INVOKABLE void applyLink();
    Q_INVOKABLE void insertField(const QString &kind);
    Q_INVOKABLE void insertFootnote();
    Q_INVOKABLE void insertEndnote();
    Q_INVOKABLE void updateToc();
    Q_INVOKABLE void addBookmark(const QString &name);
    Q_INVOKABLE void insertCrossref(const QString &target);
    Q_INVOKABLE void acceptRevision(const QString &id);
    Q_INVOKABLE void rejectRevision(const QString &id);
    Q_INVOKABLE void runSpellcheck();
    Q_INVOKABLE void setAlign(const QString &align);
    Q_INVOKABLE void applyHeading(int level);
    Q_INVOKABLE void applyNamedStyle(const QString &name);
    Q_INVOKABLE void insertParagraph();
    Q_INVOKABLE void insertTable();
    Q_INVOKABLE void insertList(const QString &kind);
    Q_INVOKABLE void insertBreak();
    Q_INVOKABLE void insertImage(const QString &path);
    Q_INVOKABLE QVariantList outline() const;
    Q_INVOKABLE QString copyIntegratePrompt();
    Q_INVOKABLE void findReplace(bool all);
    Q_INVOKABLE void findNext();
    Q_INVOKABLE void findPrev();
    Q_INVOKABLE void toggleDark();
    Q_INVOKABLE void setMarginMm(int edge, double mm);
    Q_INVOKABLE void deleteTable();
    Q_INVOKABLE void insertRow();
    Q_INVOKABLE void insertCol();
    Q_INVOKABLE void deleteRow();
    Q_INVOKABLE void deleteCol();
    Q_INVOKABLE void toggleTableHeader();
    Q_INVOKABLE void jumpToBlock(const QString &id);
    Q_INVOKABLE void newTab();
    Q_INVOKABLE void switchTab(int index);
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void confirmSave();
    Q_INVOKABLE void confirmDiscard();
    Q_INVOKABLE void cancelUnsaved();
    Q_INVOKABLE void showLoHint();
    Q_INVOKABLE void hideLoHint();
    Q_INVOKABLE void resolveComment(const QString &commentId);

signals:
    void documentChanged();
    void previewChanged();
    void selectionChanged();
    void aiChanged();
    void errorMessage(const QString &msg);
    void statusMessage(const QString &msg);
    void darkModeChanged();
    void welcomeChanged();
    void findChanged();
    void inspectorTabChanged();
    void recentsChanged();
    void saveFormatChanged();
    void linkChanged();
    void needSaveAs();
    void tabsChanged();
    void sheetsChanged();

private:
    struct TabSnap {
        Document doc;
        QString path;
        bool dirty = false;
        QString title;
    };

    const Block *caretBlock() const;
    QString caretTableId() const;
    QJsonObject formatArgs() const;
    QString afterId() const;
    void refreshPages();
    void handleResult(const QJsonObject &r, const QString &okStatus);
    void rememberRecent(const QString &path);
    void loadSettings();
    void saveSettings() const;
    void snapshotActive();
    void loadTab(int index);
    void rebuildTabsUi();
    bool gateUnsaved(const QString &action);
    void runPending();
    void jumpFind(bool forward);
    void refreshSpell();

    Session session_;
    IpcServer ipc_;
    int page_ = 1;
    int pages_ = 1;
    double zoom_ = 1.15;
    bool aiConnected_ = false;
    QString lastTool_;
    bool aiEditedFlash_ = false;
    QTimer aiFlashTimer_;
    bool darkMode_ = false;
    bool welcomeVisible_ = true;
    bool findVisible_ = false;
    QString findText_;
    QString replaceText_;
    bool findRegex_ = false;
    QString linkUrl_;
    QVariantList spellHits_;
    QTimer spellTimer_;
    int inspectorTab_ = 1;
    int fontSizePt_ = 16;
    QString saveFormat_ = QStringLiteral("docx");
    QVariantList recents_;
    QVector<TabSnap> tabSnaps_;
    int activeTab_ = 0;
    QVariantList tabsUi_;
    bool unsavedVisible_ = false;
    bool loHintVisible_ = false;
    QString pendingAction_;
    int pendingTab_ = -1;
};

} // namespace aioffice
