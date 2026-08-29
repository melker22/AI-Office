#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace aioffice {

struct TextRun {
    QString text;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    QString font;
    double sizePt = 0;
    QString color;
    QString highlight;
    QString link;

    QJsonObject toJson() const;
    static TextRun fromJson(const QJsonObject &o);
};

struct TableCell {
    QVector<TextRun> runs;
    int colSpan = 1;
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
    QString type; // paragraph, heading, list-item, table, image, break
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

    QJsonObject toJson() const;
    static StyleDef fromJson(const QJsonObject &o);
};

struct PageLayout {
    QString size = QStringLiteral("A4");
    QString orientation = QStringLiteral("portrait");
    double marginsMm[4] = {25, 20, 25, 20};

    QJsonObject toJson() const;
    static PageLayout fromJson(const QJsonObject &o);
    void pageSizeMm(double &w, double &h) const;
};

struct HeaderFooter {
    QString text;
    bool pageNumbers = false;

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
    QVector<StyleDef> styles;
    QVector<Block> body;

    static Document blank(const QString &title = QString());
    QJsonObject toJson() const;
    static Document fromJson(const QJsonObject &o);

    int nextCounter(const QString &prefix) const;
    QString assignId(Block &b);
    Block *findBlock(const QString &id);
    const Block *findBlock(const QString &id) const;
    int indexOf(const QString &id) const;
    int wordCount() const;
};

StyleDef defaultStyle(const QString &name);
QVector<TextRun> splitRuns(const QVector<TextRun> &runs, int start, int end);
QVector<TextRun> spliceRuns(QVector<TextRun> runs, int start, int end, const TextRun &insert);
void applyFormatToRange(QVector<TextRun> &runs, int start, int end, const QJsonObject &fmt);

} // namespace aioffice
