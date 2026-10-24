#include "core/Xml.h"

namespace aioffice {

QString xmlEscape(const QString &s)
{
    QString o;
    o.reserve(s.size() + 8);
    for (const QChar c : s) {
        switch (c.unicode()) {
        case '&':
            o += QLatin1String("&amp;");
            break;
        case '<':
            o += QLatin1String("&lt;");
            break;
        case '>':
            o += QLatin1String("&gt;");
            break;
        case '"':
            o += QLatin1String("&quot;");
            break;
        case '\'':
            o += QLatin1String("&apos;");
            break;
        default:
            o += c;
            break;
        }
    }
    return o;
}

} // namespace aioffice
