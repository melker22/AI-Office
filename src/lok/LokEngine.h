#pragma once

#include <QString>
#include <QStringList>

namespace aioffice {

class LokEngine {
public:
    static QString findSoffice();
    static QString findInstallPath();
    static bool available();
    static bool convertTo(const QString &inputPath, const QString &outputPath,
                          const QString &filter, QString *error);
    static bool exportPdf(const QString &inputPath, const QString &pdfPath, QString *error);
};

} // namespace aioffice
