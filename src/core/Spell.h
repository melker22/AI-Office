#pragma once

#include "core/Document.h"

#include <QJsonArray>
#include <QString>

namespace aioffice {

bool spellAvailable();
QJsonArray spellCheckDocument(const Document &doc);

} // namespace aioffice
