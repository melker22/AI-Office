#pragma once

#include <QJsonValue>
#include <QString>
#include <QVariant>

namespace aioffice {

class Document;
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
    static FieldExprResult evaluate(const QString &expr, const Document &doc, const FieldContext &ctx);
};

// A showIf value that is only a variable name (no operator, quote, or space).
bool showIfIsBareName(const QString &expr);

bool fieldExprIsTruthy(const QVariant &value);
QString fieldExprTypeName(const QVariant &value);
QJsonValue fieldExprJsonValue(const QVariant &value);

FieldContext fieldContextFor(const Document &doc, int page, int pages);

// True when hideIf evaluates truthy. Invalid expressions do not hide.
bool blockHiddenByExpr(const Document &doc, const Block &block, int page, int pages);

// Live text of an `if` field. On a bad arg or condition, returns "#ERR" and sets *exprOk false.
QString evalIfField(const Document &doc, const QString &fieldArg, int page, int pages, bool *exprOk = nullptr);

// Rewrite stored text of fields whose result depends on document structure:
// `if`, `seq`, and xref `category_number` (body runs, caption runs, cells).
// `stat` is rewritten only when includeStats is true (variable or hide changes).
// A chars recount reads the field's own cached text, so it is not refreshed on every read.
void refreshIfFields(Document &doc, bool includeStats = false);

} // namespace aioffice
