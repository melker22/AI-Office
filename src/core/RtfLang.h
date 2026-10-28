#pragma once

#include <QString>

namespace aioffice {

// Shared RTF LCID table (wave 25 D1). Unknown tag returns 0. Unknown LCID returns empty.
int rtfLcidForTag(const QString &tag);
QString rtfTagForLcid(int lcid);

} // namespace aioffice
