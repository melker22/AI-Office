#pragma once

#include <QImage>
#include <QString>

namespace aioffice {

// ISO/IEC 18004 byte mode, versions 1–10, error correction M.
// A payload past that capacity returns a null image (never a truncated symbol).
QImage encodeQr(const QString &text, int modulePx = 4);

// Empty when `text` fits. Otherwise the insert_qr error string.
QString qrCapacityError(const QString &text);

} // namespace aioffice
