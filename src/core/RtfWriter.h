#pragma once

#include "core/Document.h"

#include <QString>
#include <QStringList>

namespace aioffice {

bool saveRtf(const Document &doc, const QString &path, QString *error, QStringList *warnings = nullptr);

} // namespace aioffice
