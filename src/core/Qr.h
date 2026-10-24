#pragma once

#include <QImage>
#include <QString>

namespace aioffice {

QImage encodeQr(const QString &text, int modulePx = 4);

} // namespace aioffice
