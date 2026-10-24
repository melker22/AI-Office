#pragma once

#include "core/Document.h"

#include <QString>

namespace aioffice {

bool saveOdt(const Document &doc, const QString &path, QString *error);
bool loadOdt(const QByteArray &zipData, Document &doc, QString *error);
bool loadOdtFile(const QString &path, Document &doc, QString *error);

} // namespace aioffice
