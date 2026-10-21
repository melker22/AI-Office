#include "core/FieldExpr.h"

#include "core/Document.h"
#include "core/Preview.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cmath>

namespace aioffice {
namespace {

enum class Kind { Num, Str, Bool };

struct Val {
    Kind kind = Kind::Str;
    double num = 0;
    QString str;
    bool b = false;
};

QString stringifyNumber(double n)
{
    if (std::isfinite(n) && std::floor(n) == n && std::abs(n) < 1e15)
        return QString::number(static_cast<qlonglong>(n));
    return QString::number(n, 'g', 15);
}

QString stringify(const Val &v)
{
    if (v.kind == Kind::Num)
        return stringifyNumber(v.num);
    if (v.kind == Kind::Bool)
        return v.b ? QStringLiteral("true") : QStringLiteral("false");
    return v.str;
}

bool isTruthyVal(const Val &v)
{
    if (v.kind == Kind::Bool)
        return v.b;
    if (v.kind == Kind::Num)
        return v.num != 0.0;
    return !v.str.isEmpty() && v.str != QLatin1String("0") && v.str != QLatin1String("false");
}

Val fromNumber(double n)
{
    Val v;
    v.kind = Kind::Num;
    v.num = n;
    return v;
}

Val fromString(const QString &s)
{
    Val v;
    v.kind = Kind::Str;
    v.str = s;
    return v;
}

Val fromBool(bool b)
{
    Val v;
    v.kind = Kind::Bool;
    v.b = b;
    return v;
}

Val compareVals(const Val &a, const Val &b, const QString &op)
{
    bool r = false;
    if (a.kind == Kind::Num && b.kind == Kind::Num) {
        if (op == QLatin1String("=="))
            r = a.num == b.num;
        else if (op == QLatin1String("!="))
            r = a.num != b.num;
        else if (op == QLatin1String("<"))
            r = a.num < b.num;
        else if (op == QLatin1String(">"))
            r = a.num > b.num;
        else if (op == QLatin1String("<="))
            r = a.num <= b.num;
        else
            r = a.num >= b.num;
    } else {
        const QString sa = stringify(a);
        const QString sb = stringify(b);
        if (op == QLatin1String("=="))
            r = sa == sb;
        else if (op == QLatin1String("!="))
            r = sa != sb;
        else if (op == QLatin1String("<"))
            r = sa < sb;
        else if (op == QLatin1String(">"))
            r = sa > sb;
        else if (op == QLatin1String("<="))
            r = sa <= sb;
        else
            r = sa >= sb;
    }
    return fromBool(r);
}

class Parser {
public:
    Parser(const QString &expr, const Document &doc, const FieldContext &ctx, QSet<QString> *identifiersOut = nullptr)
        : s_(expr)
        , doc_(doc)
        , ctx_(ctx)
        , identifiersOut_(identifiersOut)
    {
    }

    FieldExprResult run()
    {
        FieldExprResult out;
        if (s_.trimmed().isEmpty()) {
            out.error = QStringLiteral("invalid expression");
            return out;
        }
        const Val v = parseOr();
        if (!error_.isEmpty()) {
            out.error = error_;
            return out;
        }
        skip();
        if (i_ < s_.size()) {
            out.error = QStringLiteral("invalid expression");
            return out;
        }
        out.ok = true;
        if (v.kind == Kind::Num)
            out.value = QVariant::fromValue(v.num);
        else if (v.kind == Kind::Bool)
            out.value = QVariant::fromValue(v.b);
        else
            out.value = v.str;
        return out;
    }

private:
    const QString &s_;
    const Document &doc_;
    const FieldContext &ctx_;
    QSet<QString> *identifiersOut_ = nullptr;
    int i_ = 0;
    int depth_ = 0;
    QString error_;
    static constexpr int kMaxDepth = 64;

    bool pushDepth()
    {
        if (depth_ >= kMaxDepth) {
            if (error_.isEmpty())
                error_ = QStringLiteral("expression too deeply nested");
            return false;
        }
        ++depth_;
        return true;
    }

    void popDepth()
    {
        if (depth_ > 0)
            --depth_;
    }

    void skip()
    {
        while (i_ < s_.size() && s_.at(i_).isSpace())
            ++i_;
    }

    bool startsWith(const QString &op) const
    {
        return s_.mid(i_, op.size()) == op;
    }

    void fail()
    {
        if (error_.isEmpty())
            error_ = QStringLiteral("invalid expression");
    }

    Val parseOr()
    {
        Val left = parseAnd();
        if (!error_.isEmpty())
            return left;
        while (true) {
            skip();
            if (!startsWith(QStringLiteral("||")))
                break;
            i_ += 2;
            const Val right = parseAnd();
            if (!error_.isEmpty())
                return left;
            left = fromBool(isTruthyVal(left) || isTruthyVal(right));
        }
        return left;
    }

    Val parseAnd()
    {
        Val left = parseCmp();
        if (!error_.isEmpty())
            return left;
        while (true) {
            skip();
            if (!startsWith(QStringLiteral("&&")))
                break;
            i_ += 2;
            const Val right = parseCmp();
            if (!error_.isEmpty())
                return left;
            left = fromBool(isTruthyVal(left) && isTruthyVal(right));
        }
        return left;
    }

    Val parseCmp()
    {
        Val left = parseUnary();
        if (!error_.isEmpty())
            return left;
        while (true) {
            skip();
            QString op;
            if (startsWith(QStringLiteral("==")))
                op = QStringLiteral("==");
            else if (startsWith(QStringLiteral("!=")))
                op = QStringLiteral("!=");
            else if (startsWith(QStringLiteral("<=")))
                op = QStringLiteral("<=");
            else if (startsWith(QStringLiteral(">=")))
                op = QStringLiteral(">=");
            else if (startsWith(QStringLiteral("<")))
                op = QStringLiteral("<");
            else if (startsWith(QStringLiteral(">")))
                op = QStringLiteral(">");
            else
                break;
            i_ += op.size();
            const Val right = parseUnary();
            if (!error_.isEmpty())
                return left;
            left = compareVals(left, right, op);
        }
        return left;
    }

    Val parseUnary()
    {
        skip();
        if (i_ < s_.size() && s_.at(i_) == QLatin1Char('!') && !startsWith(QStringLiteral("!="))) {
            if (!pushDepth())
                return fromString({});
            ++i_;
            const Val v = parseUnary();
            popDepth();
            if (!error_.isEmpty())
                return v;
            return fromBool(!isTruthyVal(v));
        }
        return parsePrimary();
    }

    Val parsePrimary()
    {
        skip();
        if (i_ >= s_.size()) {
            fail();
            return fromString({});
        }
        const QChar c = s_.at(i_);
        if (c == QLatin1Char('(')) {
            if (!pushDepth())
                return fromString({});
            ++i_;
            const Val v = parseOr();
            popDepth();
            if (!error_.isEmpty())
                return v;
            skip();
            if (i_ >= s_.size() || s_.at(i_) != QLatin1Char(')')) {
                fail();
                return v;
            }
            ++i_;
            return v;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\''))
            return parseString();
        if (c == QLatin1Char('-') || c == QLatin1Char('.') || c.isDigit())
            return parseNumber();
        if (c.isLetter() || c == QLatin1Char('_'))
            return parseIdent();
        fail();
        return fromString({});
    }

    Val parseString()
    {
        const QChar quote = s_.at(i_);
        ++i_;
        QString out;
        while (i_ < s_.size() && s_.at(i_) != quote) {
            if (s_.at(i_) == QLatin1Char('\\') && i_ + 1 < s_.size()) {
                const QChar n = s_.at(i_ + 1);
                if (n == QLatin1Char('n'))
                    out += QLatin1Char('\n');
                else if (n == QLatin1Char('t'))
                    out += QLatin1Char('\t');
                else
                    out += n;
                i_ += 2;
                continue;
            }
            out += s_.at(i_);
            ++i_;
        }
        if (i_ >= s_.size() || s_.at(i_) != quote) {
            fail();
            return fromString({});
        }
        ++i_;
        return fromString(out);
    }

    Val parseNumber()
    {
        const int start = i_;
        if (s_.at(i_) == QLatin1Char('-')) {
            if (i_ + 1 >= s_.size() || !(s_.at(i_ + 1).isDigit()
                                         || (s_.at(i_ + 1) == QLatin1Char('.') && i_ + 2 < s_.size()
                                             && s_.at(i_ + 2).isDigit()))) {
                fail();
                return fromString({});
            }
            ++i_;
        }
        bool sawDigit = false;
        if (i_ < s_.size() && s_.at(i_).isDigit()) {
            sawDigit = true;
            while (i_ < s_.size() && s_.at(i_).isDigit())
                ++i_;
            if (i_ + 1 < s_.size() && s_.at(i_) == QLatin1Char('.') && s_.at(i_ + 1).isDigit()) {
                ++i_;
                while (i_ < s_.size() && s_.at(i_).isDigit())
                    ++i_;
            }
        } else if (i_ + 1 < s_.size() && s_.at(i_) == QLatin1Char('.') && s_.at(i_ + 1).isDigit()) {
            sawDigit = true;
            ++i_;
            while (i_ < s_.size() && s_.at(i_).isDigit())
                ++i_;
        }
        if (!sawDigit) {
            fail();
            return fromString({});
        }
        bool ok = false;
        const double n = s_.mid(start, i_ - start).toDouble(&ok);
        if (!ok) {
            fail();
            return fromString({});
        }
        return fromNumber(n);
    }

    Val parseIdent()
    {
        const int start = i_;
        ++i_;
        while (i_ < s_.size()) {
            const QChar c = s_.at(i_);
            if (c.isLetter() || c.isDigit() || c == QLatin1Char('_') || c == QLatin1Char('.'))
                ++i_;
            else
                break;
        }
        while (i_ > start && s_.at(i_ - 1) == QLatin1Char('.'))
            --i_;
        const QString name = s_.mid(start, i_ - start);
        if (name.isEmpty()) {
            fail();
            return fromString({});
        }
        if (identifiersOut_)
            identifiersOut_->insert(name);
        if (name == QLatin1String("page"))
            return fromNumber(ctx_.page);
        if (name == QLatin1String("pages"))
            return fromNumber(ctx_.pages);
        if (name == QLatin1String("words"))
            return fromNumber(ctx_.words);
        if (name == QLatin1String("chars"))
            return fromNumber(ctx_.chars);
        if (name == QLatin1String("paragraphs"))
            return fromNumber(ctx_.paragraphs);
        if (name == QLatin1String("tables"))
            return fromNumber(ctx_.tables);
        if (name == QLatin1String("images"))
            return fromNumber(ctx_.images);
        return fromString(doc_.vars.value(name));
    }
};

FieldContext baseContext(const Document &doc, int page, int pages)
{
    FieldContext ctx;
    ctx.page = page > 0 ? page : 1;
    ctx.pages = pages > 0 ? pages : 1;
    ctx.words = doc.wordCount();
    for (const Block &b : doc.body) {
        ctx.chars += b.plainText().size();
        if (b.type == QLatin1String("paragraph"))
            ++ctx.paragraphs;
        else if (b.type == QLatin1String("table"))
            ++ctx.tables;
        else if (b.type == QLatin1String("image"))
            ++ctx.images;
    }
    return ctx;
}

} // namespace

FieldExprResult FieldExpr::evaluate(const QString &expr, const Document &doc, const FieldContext &ctx)
{
    return Parser(expr, doc, ctx).run();
}

QSet<QString> fieldExprIdentifiers(const QString &expr)
{
    Document empty;
    FieldContext ctx;
    QSet<QString> ids;
    Parser(expr, empty, ctx, &ids).run();
    return ids;
}

bool hideIfExprUsesPageOrPages(const QString &expr)
{
    if (expr.isEmpty())
        return false;
    const QSet<QString> ids = fieldExprIdentifiers(expr);
    return ids.contains(QLatin1String("page")) || ids.contains(QLatin1String("pages"));
}

bool showIfIsBareName(const QString &expr)
{
    if (expr.isEmpty())
        return false;
    for (const QChar c : expr) {
        if (c.isSpace() || c == QLatin1Char('!') || c == QLatin1Char('<') || c == QLatin1Char('>')
            || c == QLatin1Char('(') || c == QLatin1Char(')') || c == QLatin1Char('&') || c == QLatin1Char('|')
            || c == QLatin1Char('=') || c == QLatin1Char('"') || c == QLatin1Char('\''))
            return false;
    }
    return true;
}

bool fieldExprIsTruthy(const QVariant &value)
{
    const int t = value.typeId();
    if (t == QMetaType::Bool)
        return value.toBool();
    if (t == QMetaType::Int || t == QMetaType::LongLong || t == QMetaType::Double
        || t == QMetaType::Float)
        return value.toDouble() != 0.0;
    const QString s = value.toString();
    return !s.isEmpty() && s != QLatin1String("0") && s != QLatin1String("false");
}

QString fieldExprTypeName(const QVariant &value)
{
    const int t = value.typeId();
    if (t == QMetaType::Bool)
        return QStringLiteral("bool");
    if (t == QMetaType::Int || t == QMetaType::LongLong || t == QMetaType::Double
        || t == QMetaType::Float)
        return QStringLiteral("number");
    return QStringLiteral("string");
}

QJsonValue fieldExprJsonValue(const QVariant &value)
{
    const int t = value.typeId();
    if (t == QMetaType::Bool)
        return QJsonValue(value.toBool());
    if (t == QMetaType::Int || t == QMetaType::LongLong || t == QMetaType::Double
        || t == QMetaType::Float)
        return QJsonValue(value.toDouble());
    return QJsonValue(value.toString());
}

bool blockHiddenByExpr(const Document &doc, const Block &block, int page, int pages)
{
    if (block.hideIf.isEmpty())
        return false;
    // Totals, not the visible paragraph count: the decision must not depend on itself.
    const FieldExprResult ev = FieldExpr::evaluate(block.hideIf, doc, baseContext(doc, page, pages));
    if (!ev.ok)
        return false;
    return fieldExprIsTruthy(ev.value);
}

FieldContext fieldContextFor(const Document &doc, int page, int pages)
{
    FieldContext ctx = baseContext(doc, page, pages);
    int visibleParagraphs = 0;
    for (const Block &b : doc.body) {
        if (b.type == QLatin1String("paragraph") && !blockHiddenByExpr(doc, b, page, pages))
            ++visibleParagraphs;
    }
    ctx.paragraphs = visibleParagraphs;
    return ctx;
}

QString evalIfField(const Document &doc, const QString &fieldArg, int page, int pages, bool *exprOk,
                    QString *exprError)
{
    if (exprOk)
        *exprOk = false;
    auto failExpr = [&](const QString &message) {
        if (exprError)
            *exprError = message;
        return QStringLiteral("#ERR");
    };
    QJsonParseError pe;
    const QJsonDocument jd = QJsonDocument::fromJson(fieldArg.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !jd.isObject())
        return failExpr(QStringLiteral("invalid expression"));
    const QJsonObject o = jd.object();
    if (!o.contains(QStringLiteral("cond")) || !o.value(QStringLiteral("cond")).isString())
        return failExpr(QStringLiteral("invalid expression"));
    const FieldExprResult ev = FieldExpr::evaluate(o.value(QStringLiteral("cond")).toString(), doc,
                                                   fieldContextFor(doc, page, pages));
    if (!ev.ok)
        return failExpr(ev.error.isEmpty() ? QStringLiteral("invalid expression") : ev.error);
    if (exprOk)
        *exprOk = true;
    return fieldExprIsTruthy(ev.value) ? o.value(QStringLiteral("then")).toString()
                                        : o.value(QStringLiteral("else")).toString();
}

static bool isStructureField(const TextRun &r)
{
    if (r.field == QLatin1String("if") || r.field == QLatin1String("seq"))
        return true;
    return r.field == QLatin1String("xref") && r.xrefKind == QLatin1String("category_number");
}

void refreshIfFields(Document &doc, bool includeStats)
{
    int statPages = 1;
    if (includeStats) {
        DocumentLayout lay;
        lay.relayout(doc, 1.0);
        statPages = qMax(1, lay.pages);
    }
    // Seq and category_number first: a stat/chars count reads the stored run text.
    auto walk = [&](QVector<TextRun> &runs, bool stats) -> bool {
        bool changed = false;
        for (TextRun &r : runs) {
            const bool stat = r.field == QLatin1String("stat");
            if (stats != stat)
                continue;
            if (!stat && !isStructureField(r))
                continue;
            const int pagesArg = stat && r.fieldArg == QLatin1String("pages") ? statPages : 1;
            const QString next = runDisplayText(doc, r, 1, pagesArg, &r);
            if (next == r.text)
                continue;
            r.text = next;
            changed = true;
        }
        return changed;
    };
    auto eachRun = [&](bool stats) -> bool {
        bool changed = false;
        for (Block &b : doc.body) {
            if (walk(b.runs, stats))
                changed = true;
            if (walk(b.captionRuns, stats))
                changed = true;
            for (auto &row : b.table) {
                for (auto &cell : row) {
                    if (walk(cell.runs, stats))
                        changed = true;
                    for (auto &nrow : cell.nested) {
                        for (auto &nc : nrow) {
                            if (walk(nc.runs, stats))
                                changed = true;
                        }
                    }
                }
            }
        }
        return changed;
    };
    eachRun(false);
    if (!includeStats)
        return;
    // chars/words include the stat field's own cached digits. One pass can change
    // that length and therefore the next total; repeat until the numbers settle.
    for (int pass = 0; pass < 6; ++pass) {
        if (!eachRun(true))
            break;
    }
}

} // namespace aioffice
