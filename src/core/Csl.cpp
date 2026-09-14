#include "core/Csl.h"

namespace aioffice {
namespace csl {
namespace {

struct Name {
    QString last;
    QString first;
};

// "Last, First" is taken as-is; "First Last" is split on the last space (a documented heuristic —
// compound surnames without a comma will parse wrong, same limitation any plain-text-field
// bibliography tool has without structured name fields).
Name parseOneName(QString raw)
{
    raw = raw.trimmed();
    Name n;
    const int comma = raw.indexOf(QLatin1Char(','));
    if (comma >= 0) {
        n.last = raw.left(comma).trimmed();
        n.first = raw.mid(comma + 1).trimmed();
        return n;
    }
    const int sp = raw.lastIndexOf(QLatin1Char(' '));
    if (sp < 0) {
        n.last = raw;
        return n;
    }
    n.last = raw.mid(sp + 1).trimmed();
    n.first = raw.left(sp).trimmed();
    return n;
}

QVector<Name> parseAuthors(const QString &raw)
{
    QVector<Name> out;
    for (const QString &part : raw.split(QLatin1Char(';'), Qt::SkipEmptyParts))
        out.push_back(parseOneName(part));
    return out;
}

// Avoids a doubled "M.." when `s` already ends in a period (initials do).
QString endSentence(const QString &s) { return s.endsWith(QLatin1Char('.')) ? QStringLiteral(" ") : QStringLiteral(". "); }

QString initials(const QString &first)
{
    QString out;
    for (const QString &w : first.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (!w.isEmpty())
            out += w.left(1).toUpper() + QStringLiteral(". ");
    }
    return out.trimmed();
}

// -------- in-text --------

QString inTextAuthors(const QVector<Name> &names, const QString &style)
{
    if (names.isEmpty())
        return {};
    const bool abnt = style == QLatin1String("abnt");
    auto surname = [&](const Name &n) { return abnt ? n.last.toUpper() : n.last; };
    if (names.size() == 1)
        return surname(names.first());
    if (names.size() >= 3) {
        return surname(names.first()) + QStringLiteral(" et al.");
    }
    // exactly 2
    if (style == QLatin1String("apa"))
        return surname(names[0]) + QStringLiteral(" & ") + surname(names[1]);
    if (style == QLatin1String("abnt"))
        return surname(names[0]) + QStringLiteral("; ") + surname(names[1]);
    // mla / chicago
    return surname(names[0]) + QStringLiteral(" and ") + surname(names[1]);
}

// -------- bibliography author list --------

QString biblioAuthors(const QVector<Name> &names, const QString &style)
{
    if (names.isEmpty())
        return {};
    const bool abnt = style == QLatin1String("abnt");
    auto first = [&](const Name &n) {
        if (abnt)
            return n.last.toUpper() + (n.first.isEmpty() ? QString() : QStringLiteral(", ") + n.first);
        if (style == QLatin1String("mla"))
            return n.last + (n.first.isEmpty() ? QString() : QStringLiteral(", ") + n.first);
        // apa / chicago use initials for the first author
        return n.last + (n.first.isEmpty() ? QString() : QStringLiteral(", ") + initials(n.first));
    };
    auto rest = [&](const Name &n) {
        // APA/Chicago format every author the same way ("Last, F. M."); MLA spells out "First Last".
        if (style == QLatin1String("mla"))
            return n.first + QStringLiteral(" ") + n.last;
        return first(n);
    };
    QString out = first(names.first());
    for (int i = 1; i < names.size(); ++i) {
        const bool last = i == names.size() - 1;
        if (abnt) {
            out += QStringLiteral("; ") + rest(names[i]);
        } else if (last) {
            const QString conj = style == QLatin1String("apa") ? QStringLiteral("&") : QStringLiteral("and");
            out += QStringLiteral(", ") + conj + QLatin1Char(' ') + rest(names[i]);
        } else {
            out += QStringLiteral(", ") + rest(names[i]);
        }
    }
    return out;
}

// -------- small text-run builder --------

struct RunBuilder {
    QVector<TextRun> runs;
    void plain(const QString &t)
    {
        if (t.isEmpty())
            return;
        TextRun r;
        r.text = t;
        runs.push_back(r);
    }
    void italic(const QString &t)
    {
        if (t.isEmpty())
            return;
        TextRun r;
        r.text = t;
        r.italic = true;
        runs.push_back(r);
    }
};

} // namespace

QStringList supportedStyles() { return {QStringLiteral("apa"), QStringLiteral("abnt"), QStringLiteral("mla"), QStringLiteral("chicago")}; }

QString inText(const BiblioEntry &e, const QString &styleArg)
{
    const QString style = supportedStyles().contains(styleArg) ? styleArg : QStringLiteral("apa");
    const auto names = parseAuthors(e.author);
    const QString who = names.isEmpty() ? e.title : inTextAuthors(names, style);
    if (style == QLatin1String("mla")) {
        // MLA cites by page, not year; without a locator we fall back to just the author.
        return QStringLiteral("(") + (who.isEmpty() ? QStringLiteral("s.a.") : who)
            + (e.pages.isEmpty() ? QString() : QStringLiteral(" ") + e.pages) + QLatin1Char(')');
    }
    QString y = e.year.isEmpty() ? QStringLiteral("s.d.") : e.year;
    if (who.isEmpty())
        return QStringLiteral("(") + y + QLatin1Char(')');
    const QString sep = style == QLatin1String("abnt") ? QStringLiteral(", ") : QStringLiteral(", ");
    return QStringLiteral("(") + who + sep + y + QLatin1Char(')');
}

QVector<TextRun> bibliographyEntry(const BiblioEntry &e, const QString &styleArg)
{
    const QString style = supportedStyles().contains(styleArg) ? styleArg : QStringLiteral("apa");
    const auto names = parseAuthors(e.author);
    const QString authors = biblioAuthors(names, style);
    const QString year = e.year.isEmpty() ? QStringLiteral("s.d.") : e.year;
    RunBuilder rb;

    if (style == QLatin1String("apa")) {
        if (!authors.isEmpty())
            rb.plain(authors + QStringLiteral(" "));
        rb.plain(QStringLiteral("(") + year + QStringLiteral("). "));
        if (e.type == QLatin1String("chapter")) {
            rb.plain(e.title + QStringLiteral(". In "));
            if (!e.editor.isEmpty())
                rb.plain(e.editor + QStringLiteral(" (Ed.), "));
            rb.italic(e.containerTitle);
            if (!e.pages.isEmpty())
                rb.plain(QStringLiteral(" (pp. ") + e.pages + QStringLiteral(")"));
            rb.plain(QStringLiteral(". "));
            if (!e.publisher.isEmpty())
                rb.plain(e.publisher + QLatin1Char('.'));
        } else if (e.type == QLatin1String("article-journal")) {
            rb.plain(e.title + QStringLiteral(". "));
            rb.italic(e.containerTitle);
            if (!e.volume.isEmpty()) {
                rb.italic(QStringLiteral(", ") + e.volume);
                if (!e.issue.isEmpty())
                    rb.plain(QStringLiteral("(") + e.issue + QStringLiteral(")"));
            }
            if (!e.pages.isEmpty())
                rb.plain(QStringLiteral(", ") + e.pages);
            rb.plain(QStringLiteral("."));
            if (!e.doi.isEmpty())
                rb.plain(QStringLiteral(" https://doi.org/") + e.doi);
        } else if (e.type == QLatin1String("webpage")) {
            rb.plain(e.title + QStringLiteral(". "));
            if (!e.containerTitle.isEmpty())
                rb.italic(e.containerTitle + QStringLiteral(". "));
            if (!e.url.isEmpty())
                rb.plain(e.url);
        } else { // book
            rb.italic(e.title);
            if (!e.edition.isEmpty())
                rb.plain(QStringLiteral(" (") + e.edition + QStringLiteral(" ed.)"));
            rb.plain(QStringLiteral(". "));
            if (!e.publisher.isEmpty())
                rb.plain(e.publisher + QLatin1Char('.'));
        }
        return rb.runs;
    }

    if (style == QLatin1String("abnt")) {
        if (!authors.isEmpty())
            rb.plain(authors + endSentence(authors));
        if (e.type == QLatin1String("chapter")) {
            rb.plain(e.title + QStringLiteral(". In: "));
            if (!e.editor.isEmpty())
                rb.plain(e.editor.toUpper() + QStringLiteral(" (Org.). "));
            rb.italic(e.containerTitle + QStringLiteral(". "));
            if (!e.city.isEmpty())
                rb.plain(e.city + QStringLiteral(": "));
            if (!e.publisher.isEmpty())
                rb.plain(e.publisher + QStringLiteral(", "));
            rb.plain(year + QLatin1Char('.'));
            if (!e.pages.isEmpty())
                rb.plain(QStringLiteral(" p. ") + e.pages + QLatin1Char('.'));
        } else if (e.type == QLatin1String("article-journal")) {
            rb.plain(e.title + QStringLiteral(". "));
            rb.italic(e.containerTitle);
            if (!e.city.isEmpty())
                rb.plain(QStringLiteral(", ") + e.city);
            if (!e.volume.isEmpty())
                rb.plain(QStringLiteral(", v. ") + e.volume);
            if (!e.issue.isEmpty())
                rb.plain(QStringLiteral(", n. ") + e.issue);
            if (!e.pages.isEmpty())
                rb.plain(QStringLiteral(", p. ") + e.pages);
            rb.plain(QStringLiteral(", ") + year + QLatin1Char('.'));
        } else if (e.type == QLatin1String("webpage")) {
            rb.italic(e.title + QStringLiteral(". "));
            if (!e.containerTitle.isEmpty())
                rb.plain(e.containerTitle + QStringLiteral(", "));
            rb.plain(year + QStringLiteral(". "));
            if (!e.url.isEmpty())
                rb.plain(QStringLiteral("Disponível em: ") + e.url + QStringLiteral(". "));
            if (!e.accessed.isEmpty())
                rb.plain(QStringLiteral("Acesso em: ") + e.accessed + QLatin1Char('.'));
        } else { // book
            rb.italic(e.title);
            if (!e.edition.isEmpty())
                rb.plain(QStringLiteral(". ") + e.edition + QStringLiteral(". ed"));
            rb.plain(QStringLiteral(". "));
            if (!e.city.isEmpty())
                rb.plain(e.city + QStringLiteral(": "));
            if (!e.publisher.isEmpty())
                rb.plain(e.publisher + QStringLiteral(", "));
            rb.plain(year + QLatin1Char('.'));
        }
        return rb.runs;
    }

    if (style == QLatin1String("mla")) {
        if (!authors.isEmpty())
            rb.plain(authors + QStringLiteral(". "));
        if (e.type == QLatin1String("chapter")) {
            rb.plain(QStringLiteral("\"") + e.title + QStringLiteral(".\" "));
            rb.italic(e.containerTitle);
            if (!e.editor.isEmpty())
                rb.plain(QStringLiteral(", edited by ") + e.editor);
            rb.plain(QStringLiteral(", ") + (e.publisher.isEmpty() ? QString() : e.publisher + QStringLiteral(", "))
                + year + (e.pages.isEmpty() ? QString() : QStringLiteral(", pp. ") + e.pages) + QLatin1Char('.'));
        } else if (e.type == QLatin1String("article-journal")) {
            rb.plain(QStringLiteral("\"") + e.title + QStringLiteral(".\" "));
            rb.italic(e.containerTitle);
            QString rest;
            if (!e.volume.isEmpty())
                rest += QStringLiteral(", vol. ") + e.volume;
            if (!e.issue.isEmpty())
                rest += QStringLiteral(", no. ") + e.issue;
            rest += QStringLiteral(", ") + year;
            if (!e.pages.isEmpty())
                rest += QStringLiteral(", pp. ") + e.pages;
            rb.plain(rest + QLatin1Char('.'));
        } else if (e.type == QLatin1String("webpage")) {
            rb.plain(QStringLiteral("\"") + e.title + QStringLiteral(".\" "));
            rb.italic(e.containerTitle);
            rb.plain(QStringLiteral(", ") + year + (e.url.isEmpty() ? QString() : QStringLiteral(", ") + e.url) + QLatin1Char('.'));
        } else { // book
            rb.italic(e.title);
            rb.plain(QStringLiteral(". ") + (e.publisher.isEmpty() ? QString() : e.publisher + QStringLiteral(", ")) + year
                + QLatin1Char('.'));
        }
        return rb.runs;
    }

    // chicago (author-date)
    if (!authors.isEmpty())
        rb.plain(authors + endSentence(authors));
    rb.plain(year + QStringLiteral(". "));
    if (e.type == QLatin1String("chapter")) {
        rb.plain(QStringLiteral("\"") + e.title + QStringLiteral(".\" In "));
        rb.italic(e.containerTitle);
        if (!e.editor.isEmpty())
            rb.plain(QStringLiteral(", edited by ") + e.editor);
        if (!e.pages.isEmpty())
            rb.plain(QStringLiteral(", ") + e.pages);
        rb.plain(QStringLiteral(". "));
        if (!e.city.isEmpty())
            rb.plain(e.city + QStringLiteral(": "));
        if (!e.publisher.isEmpty())
            rb.plain(e.publisher + QLatin1Char('.'));
    } else if (e.type == QLatin1String("article-journal")) {
        rb.plain(QStringLiteral("\"") + e.title + QStringLiteral(".\" "));
        rb.italic(e.containerTitle);
        if (!e.volume.isEmpty())
            rb.plain(QStringLiteral(" ") + e.volume);
        if (!e.issue.isEmpty())
            rb.plain(QStringLiteral(" (") + e.issue + QStringLiteral(")"));
        if (!e.pages.isEmpty())
            rb.plain(QStringLiteral(": ") + e.pages);
        rb.plain(QStringLiteral("."));
    } else if (e.type == QLatin1String("webpage")) {
        rb.plain(QStringLiteral("\"") + e.title + QStringLiteral(".\" "));
        if (!e.containerTitle.isEmpty())
            rb.plain(e.containerTitle + QStringLiteral(". "));
        if (!e.accessed.isEmpty())
            rb.plain(QStringLiteral("Accessed ") + e.accessed + QStringLiteral(". "));
        if (!e.url.isEmpty())
            rb.plain(e.url + QLatin1Char('.'));
    } else { // book
        rb.italic(e.title);
        rb.plain(QStringLiteral(". "));
        if (!e.city.isEmpty())
            rb.plain(e.city + QStringLiteral(": "));
        if (!e.publisher.isEmpty())
            rb.plain(e.publisher + QLatin1Char('.'));
    }
    return rb.runs;
}

} // namespace csl
} // namespace aioffice
