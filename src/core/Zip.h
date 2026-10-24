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

} // namespace aioffice
