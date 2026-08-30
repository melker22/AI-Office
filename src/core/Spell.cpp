#include "core/Spell.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

#ifdef AIOFFICE_HAS_HUNSPELL
#include <hunspell/hunspell.h>
#endif

namespace aioffice {
namespace {

QStringList dictStems()
{
    QStringList stems;
    const QString dicpath = qEnvironmentVariable("DICPATH");
    if (!dicpath.isEmpty()) {
        for (const auto &dir : dicpath.split(QLatin1Char(':'))) {
            if (dir.isEmpty())
                continue;
            stems << dir + QStringLiteral("/pt_BR") << dir + QStringLiteral("/en_US")
                  << dir + QStringLiteral("/pt_PT");
        }
    }
    const QStringList bases = {
        QStringLiteral("/run/current-system/sw/share/hunspell"),
        QStringLiteral("/usr/share/hunspell"),
        QStringLiteral("/usr/share/myspell/dicts"),
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/hunspell"),
    };
    const QStringList names = {QStringLiteral("pt_BR"), QStringLiteral("en_US"), QStringLiteral("pt_PT")};
    for (const auto &base : bases) {
        for (const auto &n : names)
            stems << base + QLatin1Char('/') + n;
    }
    return stems;
}

#ifdef AIOFFICE_HAS_HUNSPELL
Hunhandle *loadSpell()
{
    static Hunhandle *h = nullptr;
    static bool tried = false;
    if (tried)
        return h;
    tried = true;
    for (const auto &stem : dictStems()) {
        const QString aff = stem + QStringLiteral(".aff");
        const QString dic = stem + QStringLiteral(".dic");
        if (!QFileInfo::exists(aff) || !QFileInfo::exists(dic))
            continue;
        h = Hunspell_create(aff.toLocal8Bit().constData(), dic.toLocal8Bit().constData());
        if (h)
            break;
    }
    return h;
}
#endif

} // namespace

bool spellAvailable()
{
#ifdef AIOFFICE_HAS_HUNSPELL
    return loadSpell() != nullptr;
#else
    return false;
#endif
}

QJsonArray spellCheckDocument(const Document &doc)
{
    QJsonArray hits;
#ifdef AIOFFICE_HAS_HUNSPELL
    Hunhandle *h = loadSpell();
    if (!h)
        return hits;
    const QRegularExpression wordRe(QStringLiteral(R"(\p{L}{3,})"));
    for (const auto &b : doc.body) {
        if (b.type == QLatin1String("break") || b.type == QLatin1String("image"))
            continue;
        const QString t = b.plainText();
        auto it = wordRe.globalMatch(t);
        while (it.hasNext()) {
            const auto m = it.next();
            const QByteArray utf = m.captured().toUtf8();
            if (Hunspell_spell(h, utf.constData()) != 0)
                continue;
            bool extra = false;
            for (const auto &u : doc.userDic) {
                if (u.compare(m.captured(), Qt::CaseInsensitive) == 0) {
                    extra = true;
                    break;
                }
            }
            if (extra)
                continue;
            QJsonObject o;
            o[QStringLiteral("id")] = b.id;
            o[QStringLiteral("start")] = int(m.capturedStart());
            o[QStringLiteral("end")] = int(m.capturedEnd());
            o[QStringLiteral("word")] = m.captured();
            char **list = nullptr;
            const int n = Hunspell_suggest(h, &list, utf.constData());
            QJsonArray sug;
            for (int i = 0; i < n && i < 5; ++i)
                sug.append(QString::fromUtf8(list[i]));
            if (list)
                Hunspell_free_list(h, &list, n);
            o[QStringLiteral("suggestions")] = sug;
            hits.append(o);
        }
    }
#else
    Q_UNUSED(doc);
#endif
    return hits;
}

} // namespace aioffice
