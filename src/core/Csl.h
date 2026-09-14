#pragma once

#include "core/Document.h"

#include <QString>
#include <QStringList>

namespace aioffice {
namespace csl {

// A real, if scoped, citation formatter — not a generic CSL-XML interpreter (no license-compatible
// embeddable C/C++ CSL processor exists; researched before writing this, see
// docs/WRITER_FEATURE_CATALOG.md). Covers 4 built-in styles (apa, abnt, mla, chicago) across the 4
// most common reference types (book, article-journal, webpage, chapter). Arbitrary user-supplied
// .csl style files remain out of scope — documented honestly, not silently dropped.

QStringList supportedStyles();

// Parenthetical/narrative in-text citation, e.g. "(Silva, 2020)" (apa), "(SILVA, 2020)" (abnt).
QString inText(const BiblioEntry &e, const QString &style);

// Full bibliography-entry text as runs (title portion italicized where the style calls for it),
// e.g. Author, A. (2020). *Title*. Publisher.
QVector<TextRun> bibliographyEntry(const BiblioEntry &e, const QString &style);

} // namespace csl
} // namespace aioffice
