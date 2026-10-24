#include "core/Xhtml.h"

namespace aioffice {

void XhtmlWriter::fail(const QString &message)
{
    if (error_.isEmpty())
        error_ = message;
}

bool XhtmlWriter::isName(const QString &s)
{
    if (s.isEmpty())
        return false;
    int colons = 0;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        const bool ok = c.isLetter() || c == QLatin1Char('_') || (i > 0 && (c.isDigit() || c == QLatin1Char('-') || c == QLatin1Char('.')))
            || c == QLatin1Char(':');
        if (!ok)
            return false;
        if (c == QLatin1Char(':')) {
            if (i == 0 || i + 1 == s.size())
                return false;
            if (++colons > 1)
                return false;
        }
    }
    return true;
}

QString XhtmlWriter::escapeText(const QString &s, bool attribute)
{
    QString o;
    o.reserve(s.size());
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if (u == 0xFFFE || u == 0xFFFF)
            continue;
        if (u < 0x20) {
            if (attribute || (u != 0x9 && u != 0xA && u != 0xD)) {
                if (attribute)
                    o += QLatin1Char(' ');
                continue;
            }
        }
        if (c == QLatin1Char('&'))
            o += QLatin1String("&amp;");
        else if (c == QLatin1Char('<'))
            o += QLatin1String("&lt;");
        else if (c == QLatin1Char('>'))
            o += QLatin1String("&gt;");
        else if (attribute && c == QLatin1Char('"'))
            o += QLatin1String("&quot;");
        else
            o += c;
    }
    return o;
}

void XhtmlWriter::startDocument()
{
    out_ += QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
}

void XhtmlWriter::doctypeHtml()
{
    out_ += QStringLiteral("<!DOCTYPE html>\n");
}

void XhtmlWriter::writeAttrs(const QVector<QPair<QString, QString>> &attrs)
{
    for (const auto &a : attrs) {
        if (!isName(a.first)) {
            fail(QStringLiteral("bad XML attribute name"));
            return;
        }
        out_ += QLatin1Char(' ');
        out_ += a.first;
        out_ += QStringLiteral("=\"");
        out_ += escapeText(a.second, true);
        out_ += QLatin1Char('"');
    }
}

void XhtmlWriter::start(const QString &tag, const QVector<QPair<QString, QString>> &attrs)
{
    if (!error_.isEmpty())
        return;
    if (!isName(tag)) {
        fail(QStringLiteral("bad XML element name"));
        return;
    }
    out_ += QLatin1Char('<');
    out_ += tag;
    writeAttrs(attrs);
    if (!error_.isEmpty())
        return;
    out_ += QLatin1Char('>');
    stack_.push_back(tag);
}

void XhtmlWriter::end(const QString &tag)
{
    if (!error_.isEmpty())
        return;
    if (stack_.isEmpty() || stack_.back() != tag) {
        fail(QStringLiteral("mismatched end tag ") + tag);
        return;
    }
    stack_.removeLast();
    out_ += QStringLiteral("</");
    out_ += tag;
    out_ += QLatin1Char('>');
}

void XhtmlWriter::empty(const QString &tag, const QVector<QPair<QString, QString>> &attrs)
{
    if (!error_.isEmpty())
        return;
    if (!isName(tag)) {
        fail(QStringLiteral("bad XML element name"));
        return;
    }
    out_ += QLatin1Char('<');
    out_ += tag;
    writeAttrs(attrs);
    if (!error_.isEmpty())
        return;
    out_ += QStringLiteral("/>");
}

void XhtmlWriter::text(const QString &s)
{
    if (!error_.isEmpty())
        return;
    out_ += escapeText(s, false);
}

void XhtmlWriter::nl()
{
    if (error_.isEmpty())
        out_ += QLatin1Char('\n');
}

} // namespace aioffice
