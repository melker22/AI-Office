#include "core/RtfLang.h"

namespace aioffice {
namespace {

struct LcidRow {
    const char *tag;
    int lcid;
};

// Canonical tags. rtfTagForLcid returns these spellings, not a regionless alias.
constexpr LcidRow kRows[] = {
    {"pt-BR", 1046},
    {"pt-PT", 2070},
    {"en-US", 1033},
    {"en-GB", 2057},
    {"es-ES", 3082},
    {"es-MX", 2058},
    {"fr-FR", 1036},
    {"de-DE", 1031},
    {"it-IT", 1040},
    {"ar", 1025},
    {"he", 1037},
    {"ru", 1049},
    {"zh-CN", 2052},
    {"zh-TW", 1028},
    {"ja", 1041},
    {"ko", 1042},
};

QString normalizedTag(QString tag)
{
    tag = tag.trimmed();
    tag.replace(QLatin1Char('_'), QLatin1Char('-'));
    return tag;
}

} // namespace

int rtfLcidForTag(const QString &tag)
{
    const QString t = normalizedTag(tag);
    if (t.isEmpty())
        return 0;
    for (const LcidRow &row : kRows) {
        if (t.compare(QLatin1String(row.tag), Qt::CaseInsensitive) == 0)
            return row.lcid;
    }
    // A regionless tag uses the principal variant. A tag that already names a
    // region and missed the table is unknown (pt-XX must not become pt-BR).
    if (t.contains(QLatin1Char('-')))
        return 0;
    const QString l = t.toLower();
    if (l == QLatin1String("pt"))
        return 1046;
    if (l == QLatin1String("en"))
        return 1033;
    if (l == QLatin1String("es"))
        return 3082;
    if (l == QLatin1String("fr"))
        return 1036;
    if (l == QLatin1String("de"))
        return 1031;
    if (l == QLatin1String("it"))
        return 1040;
    if (l == QLatin1String("zh"))
        return 2052;
    return 0;
}

QString rtfTagForLcid(int lcid)
{
    for (const LcidRow &row : kRows) {
        if (row.lcid == lcid)
            return QString::fromLatin1(row.tag);
    }
    return {};
}

} // namespace aioffice
