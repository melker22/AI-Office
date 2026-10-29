#pragma once

#include "core/Document.h"

#include <QString>
#include <QStringList>

namespace aioffice {

// Native RTF import (wave 20 D4/D5). Rejects hostile input instead of
// falling back to LibreOffice. `.doc` stays on the soffice path.
// `warnings`, when set, receives non-fatal import notes (an image inside a
// table cell is replaced by a text placeholder). document_open does not
// surface that list.
bool loadRtfFile(const QString &path, Document &doc, QString *error, QStringList *warnings = nullptr);

} // namespace aioffice
