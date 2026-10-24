#pragma once

#include "core/Document.h"

#include <QString>

namespace aioffice {

bool saveDocx(const Document &doc, const QString &path, QString *error, bool embedJson = true);
bool loadDocx(const QByteArray &zipData, Document &doc, QString *error);
bool loadDocxFile(const QString &path, Document &doc, QString *error);

} // namespace aioffice
