#pragma once

#include "core/Document.h"

#include <QString>

namespace aioffice {

// Native RTF import (wave 20 D4/D5). Rejects hostile input instead of
// falling back to LibreOffice. `.doc` stays on the soffice path.
bool loadRtfFile(const QString &path, Document &doc, QString *error);

} // namespace aioffice
