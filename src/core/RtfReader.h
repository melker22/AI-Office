#pragma once

#include "core/Document.h"

#include <QString>
#include <QStringList>

namespace aioffice {

// Native RTF import (wave 20 D4/D5, wave 28 D5–D9 read side). Rejects hostile
// input instead of falling back to LibreOffice. `.doc` stays on the soffice path.
// `warnings`, when set, receives non-fatal import notes (an image inside a
// table cell, revision markup dropped in a cell, an author index outside
// \revtbl, an invalid revision date, ignored aiorevfmt JSON, a clamped shape,
// an unknown shape type, a shape group, a missing comment parent). document_open
// does not surface that list.
bool loadRtfFile(const QString &path, Document &doc, QString *error, QStringList *warnings = nullptr);

} // namespace aioffice
