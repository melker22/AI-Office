#pragma once

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aioffice {

// Well-formed XML writer for EPUB content documents and the package.
// Text and attribute values are escaped. Element names are closed in order.
class XhtmlWriter {
public:
    void startDocument();
    void doctypeHtml();
    void start(const QString &tag, const QVector<QPair<QString, QString>> &attrs = {});
    void end(const QString &tag);
    void empty(const QString &tag, const QVector<QPair<QString, QString>> &attrs = {});
    void text(const QString &s);
    void nl();

    QString result() const { return out_; }
    QString error() const { return error_; }
    bool ok() const { return error_.isEmpty() && stack_.isEmpty(); }

private:
    QString out_;
    QStringList stack_;
    QString error_;

    void fail(const QString &message);
    void writeAttrs(const QVector<QPair<QString, QString>> &attrs);
    static bool isName(const QString &s);
    static QString escapeText(const QString &s, bool attribute);
};

} // namespace aioffice
