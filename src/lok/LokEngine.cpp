#include "lok/LokEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace aioffice {

QString LokEngine::findSoffice()
{
    const QByteArray env = qgetenv("AIOFFICE_SOFFICE");
    if (!env.isEmpty() && QFileInfo::exists(QString::fromUtf8(env)))
        return QString::fromUtf8(env);

    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("soffice"));
    if (!inPath.isEmpty())
        return inPath;
    const QString lo = QStandardPaths::findExecutable(QStringLiteral("libreoffice"));
    if (!lo.isEmpty())
        return lo;

    const QStringList candidates = {
#ifdef Q_OS_WIN
        QStringLiteral("C:/Program Files/LibreOffice/program/soffice.exe"),
        QStringLiteral("C:/Program Files (x86)/LibreOffice/program/soffice.exe"),
#else
        QStringLiteral("/usr/bin/soffice"),
        QStringLiteral("/usr/bin/libreoffice"),
        QStringLiteral("/usr/lib/libreoffice/program/soffice"),
        QStringLiteral("/usr/lib64/libreoffice/program/soffice"),
        QStringLiteral("/opt/libreoffice/program/soffice"),
#endif
    };
    for (const auto &c : candidates) {
        if (QFileInfo::exists(c))
            return c;
    }
    return {};
}

QString LokEngine::findInstallPath()
{
    const QString so = findSoffice();
    if (so.isEmpty())
        return {};
    QFileInfo fi(so);
    QDir dir = fi.dir();
    if (dir.dirName() == QLatin1String("program"))
        return dir.absolutePath();
    if (QFileInfo::exists(dir.filePath(QStringLiteral("program"))))
        return dir.filePath(QStringLiteral("program"));
    return dir.absolutePath();
}

bool LokEngine::available()
{
    return !findSoffice().isEmpty();
}

bool LokEngine::convertTo(const QString &inputPath, const QString &outputPath, const QString &filter,
                          QString *error)
{
    const QString soffice = findSoffice();
    if (soffice.isEmpty()) {
        if (error)
            *error = QStringLiteral("LibreOffice (soffice) not found. Install it to convert DOC/PDF.");
        return false;
    }
    QFileInfo outFi(outputPath);
    QDir().mkpath(outFi.absolutePath());
    QStringList args = {
        QStringLiteral("--headless"),
        QStringLiteral("--norestore"),
        QStringLiteral("--nolockcheck"),
        QStringLiteral("--convert-to"),
        filter,
        QStringLiteral("--outdir"),
        outFi.absolutePath(),
        QFileInfo(inputPath).absoluteFilePath(),
    };
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(soffice, args);
    if (!p.waitForFinished(120000)) {
        p.kill();
        if (error)
            *error = QStringLiteral("soffice timed out");
        return false;
    }
    if (p.exitCode() != 0) {
        if (error)
            *error = QStringLiteral("soffice failed: ") + QString::fromUtf8(p.readAll());
        return false;
    }
    // soffice names output from the input stem + new extension
    const QString produced = outFi.absolutePath() + QLatin1Char('/')
        + QFileInfo(inputPath).completeBaseName() + QLatin1Char('.')
        + QFileInfo(outputPath).suffix();
    if (QFileInfo::exists(produced) && QFileInfo(produced).absoluteFilePath() != outFi.absoluteFilePath()) {
        QFile::remove(outputPath);
        if (!QFile::rename(produced, outputPath) && !QFile::copy(produced, outputPath)) {
            if (error)
                *error = QStringLiteral("Could not move converted file to ") + outputPath;
            return false;
        }
    }
    if (!QFileInfo::exists(outputPath)) {
        if (error)
            *error = QStringLiteral("Conversion produced no file");
        return false;
    }
    return true;
}

bool LokEngine::exportPdf(const QString &inputPath, const QString &pdfPath, QString *error)
{
    return convertTo(inputPath, pdfPath, QStringLiteral("pdf:writer_pdf_Export"), error);
}

} // namespace aioffice
