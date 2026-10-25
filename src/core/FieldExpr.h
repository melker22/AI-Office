#pragma once

#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QVariant>

namespace aioffice {

struct Document;
struct Block;
struct TextRun;

// Numeric field references (page, pages, words, chars, paragraphs, tables, images)
// are filled by the caller. `paragraphs` is the count of paragraphs that are not
// hidden by hideIf. hideIf itself is decided against the unfiltered totals so the
// decision does not recurse.
struct FieldContext {
    int page = 1;
    int pages = 1;
    int words = 0;
    int chars = 0;
    int paragraphs = 0;
    int tables = 0;
    int images = 0;
};

struct FieldExprResult {
    bool ok = false;
    QVariant value; // number (double) | string | bool
    QString error;
};

struct FieldExpr {
    // Recursive-descent expression. No scripting. Syntax errors set ok=false.
    // Parentheses and `!` share a depth limit of 64. Past that, error is
    // "expression too deeply nested" (the field shows #ERR).
    static FieldExprResult evaluate(const QString &expr, const Document &doc, const FieldContext &ctx);
};

// A showIf value that is only a variable name (no operator, quote, or space).
bool showIfIsBareName(const QString &expr);

bool fieldExprIsTruthy(const QVariant &value);
QString fieldExprTypeName(const QVariant &value);
QJsonValue fieldExprJsonValue(const QVariant &value);

FieldContext fieldContextFor(const Document &doc, int page, int pages);

// True when hideIf evaluates truthy. Invalid expressions do not hide.
// hideIf is always evaluated with page=1 and pages=1 (before pagination).
bool blockHiddenByExpr(const Document &doc, const Block &block, int page, int pages);

// Identifier names referenced by an expression (including page, pages, words, …).
// Syntax errors yield a partial set; pair with FieldExpr::evaluate for validation.
QSet<QString> fieldExprIdentifiers(const QString &expr);

bool hideIfExprUsesPageOrPages(const QString &expr);

// Empty when hide_if may be stored. Otherwise the same message document_validate
// reports: page/pages dependency, or the expression parser error.
QString hideIfExprError(const QString &expr);

// Live text of an `if` field. On a bad arg or condition, returns "#ERR" and sets *exprOk false.
QString evalIfField(const Document &doc, const QString &fieldArg, int page, int pages, bool *exprOk = nullptr,
                    QString *exprError = nullptr);

// One fixed point for field text. Order: structure fields and `if` (seeded with the
// real layout page total, settled so a word-count cycle keeps the lexicographic
// minimum and a later call returns the same text), then layout, and if that page
// total changed one more structure pass and one layout that is not fed back.
// `stat` is rewritten only when includeStats is true, using that final page total.
// Callers pass true after an edit (dirty get/save, hide, or vars) and false on a
// clean read, so an imported stat cache survives document_get. A chars/words recount
// includes the field's own cached digits, so the stat pass repeats until those
// stored numbers settle; if a stat changed the text, layout runs once more and
// fields are not rewritten again. `page` in cached field text stays 1.
void refreshIfFields(Document &doc, bool includeStats = false);

} // namespace aioffice
