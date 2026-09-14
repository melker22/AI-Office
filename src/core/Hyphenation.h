#pragma once

#include <QString>

namespace aioffice {

// Knuth-Liang pattern-based hyphenation (real TeX-style dictionaries, see
// hyphen/NOTICE.md), not a suffix heuristic. Inserts U+00AD (soft hyphen) at
// linguistically valid break points. `lang` picks the pattern set: any tag
// starting with "en" uses English patterns, everything else (including
// empty/unset) uses Portuguese patterns.
QString hyphenateWord(const QString &word, const QString &lang);

} // namespace aioffice
