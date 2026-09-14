#include "core/Hyphenation.h"

#include <QFile>
#include <QHash>
#include <QTextStream>
#include <QVector>

namespace aioffice {
namespace {

struct PatternSet {
    QHash<QString, QVector<int>> patterns;
    int leftMin = 2;
    int rightMin = 3;
    int maxPatternLen = 1;
    bool loaded = false;
};

void parsePatternLine(const QString &line, QString &key, QVector<int> &vals)
{
    key.clear();
    vals.clear();
    vals.push_back(0);
    for (const QChar &c : line) {
        if (c.isDigit()) {
            vals[vals.size() - 1] = c.digitValue();
        } else {
            key += c;
            vals.push_back(0);
        }
    }
}

PatternSet loadPatternSet(const QString &resourcePath)
{
    PatternSet ps;
    QFile f(resourcePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return ps;
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    bool first = true;
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (first) {
            first = false;
            // First line is the dictionary charset tag (UTF-8/ISO8859-1); our
            // vendored files are both UTF-8, so nothing to convert.
            continue;
        }
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1String("LEFTHYPHENMIN"))) {
            ps.leftMin = line.section(QLatin1Char(' '), 1, 1).toInt();
            if (ps.leftMin <= 0)
                ps.leftMin = 2;
            continue;
        }
        if (line.startsWith(QLatin1String("RIGHTHYPHENMIN"))) {
            ps.rightMin = line.section(QLatin1Char(' '), 1, 1).toInt();
            if (ps.rightMin <= 0)
                ps.rightMin = 3;
            continue;
        }
        if (line.startsWith(QLatin1String("COMPOUND")))
            continue;
        if (line.contains(QLatin1Char('/')))
            continue; // exception/replacement entries (e.g. German ck->k-k) — not supported here.
        QString key;
        QVector<int> vals;
        parsePatternLine(line, key, vals);
        if (key.isEmpty())
            continue;
        ps.patterns.insert(key, vals);
        ps.maxPatternLen = qMax(ps.maxPatternLen, key.size());
    }
    ps.loaded = !ps.patterns.isEmpty();
    return ps;
}

const PatternSet &patternSetFor(const QString &lang)
{
    static const PatternSet en = loadPatternSet(QStringLiteral(":/hyphen/hyph_en_US.dic"));
    static const PatternSet pt = loadPatternSet(QStringLiteral(":/hyphen/hyph_pt_BR.dic"));
    return lang.startsWith(QLatin1String("en"), Qt::CaseInsensitive) ? en : pt;
}

} // namespace

QString hyphenateWord(const QString &word, const QString &lang)
{
    const PatternSet &ps = patternSetFor(lang);
    const int L = word.size();
    if (!ps.loaded || L < ps.leftMin + ps.rightMin)
        return word;

    const QString padded = QLatin1Char('.') + word.toLower() + QLatin1Char('.');
    QVector<int> values(padded.size() + 1, 0);
    for (int i = 0; i < padded.size(); ++i) {
        for (int len = 1; len <= ps.maxPatternLen && i + len <= padded.size(); ++len) {
            const auto it = ps.patterns.constFind(padded.mid(i, len));
            if (it == ps.patterns.constEnd())
                continue;
            const QVector<int> &vals = it.value();
            for (int k = 0; k < vals.size(); ++k)
                values[i + k] = qMax(values[i + k], vals[k]);
        }
    }

    QString out;
    out.reserve(L + 4);
    for (int i = 0; i < L; ++i) {
        out += word[i];
        // gap g (in padded coordinates, padded[k] == word[k-1]) sits right after word[i].
        const int g = i + 2;
        const int bp = g - 1; // word-local break position: between word[bp-1] and word[bp]
        if (bp >= ps.leftMin && (L - bp) >= ps.rightMin && (values[g] % 2) == 1)
            out += QChar(0x00AD);
    }
    return out;
}

} // namespace aioffice
