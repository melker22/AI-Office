#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aioffice {

struct ZipEntry {
    QString name;
    QByteArray data;
    bool store = false;
};

class Zip {
public:
    static QByteArray pack(const QVector<ZipEntry> &entries);
    static bool unpack(const QByteArray &zip, QVector<ZipEntry> &out, QString *error = nullptr);
    static QByteArray file(const QVector<ZipEntry> &entries, const QString &name);
};

// Rejects devices, fifos, and directories (including a symlink to one) so a
// save cannot replace the link or report success on /dev/full.
bool ensureAtomicWriteTarget(const QString &path, QString *error);
bool writeFileAtomic(const QString &path, const QByteArray &bytes, QString *error);

} // namespace aioffice
