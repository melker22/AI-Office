#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>

namespace aioffice {

struct TextRun {
    QString text;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool superscript = false;
    bool subscript = false;
    QString font;
    double sizePt = 0;
    QString color;
    QString highlight;
    bool smallCaps = false;
    QString caps; // upper | lower | title
    bool hidden = false;
    QString charStyle;
    QString lang;
    double kerning = 0; // extra letter-spacing percent (0 = default)
    QString link;
    QString field;    // page_number | page_count | date | title | author | xref
    QString fieldArg; // xref target id
    QString noteId;   // footnote/endnote block id

    QJsonObject toJson() const;
    static TextRun fromJson(const QJsonObject &o);
};

struct Bookmark {
    QString id;
    QString name;
    QString blockId;
    int offset = 0;

    QJsonObject toJson() const;
    static Bookmark fromJson(const QJsonObject &o);
};

struct Revision {
    QString id;
    QString author;
    QString kind; // insert | delete | format
    QString blockId;
    int start = 0;
    int end = 0;
    QString text;
    qint64 timestamp = 0;

    QJsonObject toJson() const;
    static Revision fromJson(const QJsonObject &o);
};

struct NestedCell {
    QVector<TextRun> runs;
};

struct TabStop {
    double posMm = 0;
    QString align; // left | right | center | decimal
};

struct TableCell {
    QVector<TextRun> runs;
    int colSpan = 1;
    int rowSpan = 1;
    QVector<QVector<NestedCell>> nested;
};

struct Comment {
    QString id;
    QString author;
    QString text;
    bool resolved = false;

    QJsonObject toJson() const;
    static Comment fromJson(const QJsonObject &o);
};

struct Block {
    QString id;
    QString type; // paragraph, heading, list-item, table, image, break, footnote, endnote, toc
    int level = 0;
    QString style;
    QString align;
    QString listKind;
    QString breakKind;
    QVector<TextRun> runs;
    QVector<QVector<TableCell>> table;
    QString imagePath;
    QByteArray imageBytes;
    QString imageMime;
    int imageWidthMm = 80;
    bool headerRow = false;
    QVector<double> colWidthsMm;
    QVector<Comment> comments;
    QStringList tocTargets;
    double indentLeftMm = 0;
    double indentRightMm = 0;
    double indentFirstMm = 0;
    double spaceBeforePt = -1;
    double spaceAfterPt = -1;
    double lineHeight = 1.0;
    bool keepWithNext = false;
    int orphans = 0;
    int widows = 0;
    int dropCapLines = 0;
    QVector<TabStop> tabs;
    QString borderColor;
    double borderPt = 0;
    QString altText;
    QString caption;
    QString wrap;
    QString anchor;
    bool checked = false;
    bool listRestart = false;
    QString listBullet;
    QString lang;
    bool pageRestart = false;
    int pageNumber = 0;

    QJsonObject toJson() const;
    static Block fromJson(const QJsonObject &o);
    QString plainText() const;
};

struct StyleDef {
    QString name;
    QString font = QStringLiteral("Liberation Serif");
    double sizePt = 12;
    bool bold = false;
    bool italic = false;
    QString color = QStringLiteral("#1A1F24");
    QString align;
    double spaceBeforePt = 0;
    double spaceAfterPt = 8;
    QString kind; // paragraph | character | list | page
    QString next;
    QString listKind;
    QString listBullet;
    QString pageSize;
    QString pageOrientation;

    QJsonObject toJson() const;
    static StyleDef fromJson(const QJsonObject &o);
};

struct PageLayout {
    QString size = QStringLiteral("A4");
    QString orientation = QStringLiteral("portrait");
    double marginsMm[4] = {25, 20, 25, 20};
    int columns = 1;
    bool hyphenate = false;
    double gutterMm = 0;
    double columnGapMm = 5;
    bool columnSeparator = false;
    QString borderColor;
    double borderPt = 0;
    QString background;
    QString numberFormat; // arabic | roman | alpha
    int numberStart = 1;

    QJsonObject toJson() const;
    static PageLayout fromJson(const QJsonObject &o);
    void pageSizeMm(double &w, double &h) const;
};

struct HeaderFooter {
    QString text;
    bool pageNumbers = false;

    bool active() const { return pageNumbers || !text.isEmpty(); }
    QJsonObject toJson() const;
    static HeaderFooter fromJson(const QJsonObject &o);
};

struct Selection {
    QString anchorId;
    int anchorOff = 0;
    QString focusId;
    int focusOff = 0;
    int tableRow = -1;
    int tableCol = -1;

    QJsonObject toJson() const;
    static Selection fromJson(const QJsonObject &o);
    bool empty() const { return anchorId.isEmpty(); }
};

struct Document {
    QString title = QStringLiteral("Sem título");
    QString author;
    PageLayout page;
    HeaderFooter header;
    HeaderFooter footer;
    HeaderFooter headerEven;
    HeaderFooter footerEven;
    HeaderFooter headerFirst;
    HeaderFooter footerFirst;
    QVector<StyleDef> styles;
    QVector<Block> body;
    QVector<Bookmark> bookmarks;
    QVector<Revision> revisions;
    bool trackChanges = false;
    QString fileName;
    QMap<QString, QString> autocorrect;

    static Document blank(const QString &title = QString());
    QJsonObject toJson() const;
    static Document fromJson(const QJsonObject &o);
    QByteArray toCanonicalJson() const;

    int nextCounter(const QString &prefix) const;
    QString assignId(Block &b);
    Block *findBlock(const QString &id);
    const Block *findBlock(const QString &id) const;
    int indexOf(const QString &id) const;
    int wordCount() const;
};

QString runDisplayText(const Document &doc, const TextRun &r, int page = 1, int pages = 1);
QString formatPageNumber(int n, const QString &format);
StyleDef defaultStyle(const QString &name);
QVector<TextRun> splitRuns(const QVector<TextRun> &runs, int start, int end);
QVector<TextRun> spliceRuns(QVector<TextRun> runs, int start, int end, const TextRun &insert);
void applyFormatToRun(TextRun &r, const QJsonObject &fmt);
void clearRunFormat(TextRun &r);
QJsonObject runFormatJson(const TextRun &r);
void applyFormatToRange(QVector<TextRun> &runs, int start, int end, const QJsonObject &fmt);

} // namespace aioffice
