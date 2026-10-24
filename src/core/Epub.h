#pragma once

#include <QJsonObject>
#include <QString>

namespace aioffice {

struct Document;

// EPUB 3 export. `info`, when set, receives format, chapters, images, and identifier.
bool saveEpub(const Document &doc, const QString &path, QString *error, QJsonObject *info = nullptr);

} // namespace aioffice
