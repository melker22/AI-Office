#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aioffice {

struct Document;

// EPUB 3 export. `info`, when set, receives format, chapters, images, and identifier.
// `warnings` is added only when an orphan note or a page-field substitution was published.
bool saveEpub(const Document &doc, const QString &path, QString *error, QJsonObject *info = nullptr);

// Self-contained HTML5 export (wave 27 D5) through the same block renderer the
// EPUB chapters use (wave 27 D4): <!DOCTYPE html>, <html lang>, <meta charset>,
// the metadata <title>, the stylesheet in a <style>, block/character styles as
// classes, evaluated fields, hide_if/section_hidden omitted, lang/dir, captions,
// tables with colspan/rowspan, nested lists, figures embedded as data: URIs and
// the notes in a <section> at the end with forward and back links. A note whose
// runs carry U+2029 renders one <p> per paragraph (wave 27 D2). `selection`, when
// non-empty, limits the export to those body indexes. `warnings`, when set, gets
// the same wording saveEpub publishes. Returns false and sets *error on failure.
bool renderHtmlDocument(const Document &doc, const QVector<int> &selection, QString *html,
                        QStringList *warnings, QString *error);

} // namespace aioffice
