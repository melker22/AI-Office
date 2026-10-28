#pragma once

#include <QString>

namespace aioffice {

// Shared LCID table for the RTF writer and the reader (wave 25 D1).
// Header-only so the reader can include it without a new CMake source.
// rtfLcidForTag returns 0 when the tag is unknown: the writer then omits \lang.
// rtfTagForLcid returns the canonical tag, or empty when the LCID is not listed.

int rtfLcidForTag(const QString &tag);
QString rtfTagForLcid(int lcid);

struct RtfLangEntry {
    const char *tag;
    int lcid;
};

inline const RtfLangEntry *rtfLangTable(int *count)
{
    static const RtfLangEntry kEntries[] = {
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
    if (count)
        *count = int(sizeof(kEntries) / sizeof(kEntries[0]));
    return kEntries;
}

inline QString rtfNormalizeTag(const QString &tag)
{
    QString n = tag.trimmed();
    n.replace(QLatin1Char('_'), QLatin1Char('-'));
    return n.toLower();
}

inline int rtfLcidForTag(const QString &tag)
{
    const QString n = rtfNormalizeTag(tag);
    if (n.isEmpty())
        return 0;
    int count = 0;
    const RtfLangEntry *entries = rtfLangTable(&count);
    for (int i = 0; i < count; ++i) {
        if (n == QString::fromLatin1(entries[i].tag).toLower())
            return entries[i].lcid;
    }
    // A region that is not in the table (pt-AO, he-IL) is unknown.
    // Do not fall back to the language prefix.
    if (n.contains(QLatin1Char('-')))
        return 0;
    if (n == QLatin1String("pt"))
        return 1046;
    if (n == QLatin1String("en"))
        return 1033;
    if (n == QLatin1String("es"))
        return 3082;
    if (n == QLatin1String("fr"))
        return 1036;
    if (n == QLatin1String("de"))
        return 1031;
    if (n == QLatin1String("it"))
        return 1040;
    if (n == QLatin1String("zh"))
        return 2052;
    return 0;
}

inline QString rtfTagForLcid(int lcid)
{
    int count = 0;
    const RtfLangEntry *entries = rtfLangTable(&count);
    for (int i = 0; i < count; ++i) {
        if (entries[i].lcid == lcid)
            return QString::fromLatin1(entries[i].tag);
    }
    return {};
}

} // namespace aioffice
