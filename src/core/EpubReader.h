#pragma once

#include <QString>
#include <QStringList>

namespace aioffice {

struct Document;

// EPUB 3 import (wave 27 D6/D7). Reads META-INF/container.xml, the OPF and the
// spine, and maps each content document to blocks: headings, paragraphs, list
// items, tables, images, figure captions, inline formatting/links, language,
// direction and notes. Note paragraphs inside one note are separated by U+2029
// (D1). Package limits are the wave 23 ones (Zip owns them) plus 32 MiB per
// image. A document outside the spine, the nav, scripts, iframes, objects and
// forms do not become content.
//
// `warnings` (when set) receives one message per ignored href or image, missing
// note target, and unresolved internal link. `error` receives a message of the
// form "<file>: <error> at line N" for malformed XML inside the package.
bool loadEpubFile(const QString &path, Document &doc, QString *error, QStringList *warnings = nullptr);

} // namespace aioffice