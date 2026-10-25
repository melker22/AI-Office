#include "lok/LokEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace aioffice {
namespace {

bool executableFile(const QString &path)
{
    const QFileInfo fi(path);
    return fi.isFile() && fi.isExecutable();
}

bool placeConverted(const QString &produced, const QString &finalPath, QString *error)
{
    const QFileInfo finalFi(finalPath);
    if (QFileInfo(produced).absoluteFilePath() == finalFi.absoluteFilePath())
        return true;
    const QString stage = finalFi.absolutePath() + QLatin1Char('/') + QLatin1Char('.') + finalFi.fileName()
        + QStringLiteral(".aioffice-new");
    QFile::remove(stage);
    if (!QFile::rename(produced, stage) && !QFile::copy(produced, stage)) {
        if (error)
            *error = QStringLiteral("Could not move converted file to ") + finalPath;
        return false;
    }
    if (QFile::rename(stage, finalPath))
        return true;
    // rename(2) replaces an existing file on Unix. If it did not, replace
    // only after the converted bytes are already in this directory.
    QFile::remove(finalPath);
    if (QFile::rename(stage, finalPath))
        return true;
    if (QFile::copy(stage, finalPath)) {
        QFile::remove(stage);
        return true;
    }
    QFile::remove(stage);
    if (error)
        *error = QStringLiteral("Could not move converted file to ") + finalPath;
    return false;
}

} // namespace

QString LokEngine::findSoffice()
{
    const QByteArray env = qgetenv("AIOFFICE_SOFFICE");
    if (!env.isEmpty())
        return executableFile(QString::fromUtf8(env)) ? QString::fromUtf8(env) : QString();

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
        if (executableFile(c))
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
    const QFileInfo outFi(outputPath);
    if (!QDir().mkpath(outFi.absolutePath())) {
        if (error)
            *error = QStringLiteral("Cannot create ") + outFi.absolutePath();
        return false;
    }
    // soffice names the output from the input stem inside --outdir. A private
    // directory keeps a sibling that shares that stem from being overwritten.
    QTemporaryDir work;
    if (!work.isValid()) {
        if (error)
            *error = QStringLiteral("Could not create a temporary directory");
        return false;
    }
    const QStringList args = {
        QStringLiteral("--headless"),
        QStringLiteral("--norestore"),
        QStringLiteral("--nolockcheck"),
        QStringLiteral("--convert-to"),
        filter,
        QStringLiteral("--outdir"),
        work.path(),
        QFileInfo(inputPath).absoluteFilePath(),
    };
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(soffice, args);
    if (!p.waitForStarted(5000)) {
        if (error)
            *error = QStringLiteral("soffice failed to start");
        return false;
    }
    if (!p.waitForFinished(120000)) {
        p.kill();
        p.waitForFinished(3000);
        if (error)
            *error = QStringLiteral("soffice timed out");
        return false;
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (error)
            *error = QStringLiteral("soffice failed: ") + QString::fromUtf8(p.readAll());
        return false;
    }
    const QString produced = QDir(work.path()).filePath(
        QFileInfo(inputPath).completeBaseName() + QLatin1Char('.') + outFi.suffix());
    if (!QFileInfo::exists(produced)) {
        if (error)
            *error = QStringLiteral("Conversion produced no file");
        return false;
    }
    return placeConverted(produced, outFi.absoluteFilePath(), error);
}

bool LokEngine::exportPdf(const QString &inputPath, const QString &pdfPath, QString *error)
{
    return convertTo(inputPath, pdfPath, QStringLiteral("pdf:writer_pdf_Export"), error);
}

} // namespace aioffice
