#include "core/Zip.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QtGlobal>
#include <zlib.h>

#include <cstdint>
#include <cstring>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace aioffice {
namespace {
constexpr quint32 kMaxZipEntryUncompressed = 256u * 1024u * 1024u;
constexpr qint64 kMaxZipBytes = 1ll << 30; // 1 GiB

// Tests lower these to prove the real-byte budget without a 1 GiB allocation.
// A missing, unparsable, or larger value is ignored, so the environment cannot
// raise the built-in ceiling.
quint64 limitFromEnv(const char *name, quint64 ceiling)
{
    const QByteArray v = qgetenv(name);
    if (v.isEmpty())
        return ceiling;
    bool ok = false;
    const quint64 n = v.toULongLong(&ok);
    if (!ok || n < 1 || n > ceiling)
        return ceiling;
    return n;
}

quint32 zipEntryCap()
{
    return static_cast<quint32>(limitFromEnv("AIOFFICE_ZIP_ENTRY_CAP", kMaxZipEntryUncompressed));
}

qint64 zipTotalCap()
{
    return static_cast<qint64>(limitFromEnv("AIOFFICE_ZIP_TOTAL_CAP", static_cast<quint64>(kMaxZipBytes)));
}
} // namespace

bool ensureAtomicWriteTarget(const QString &path, QString *error)
{
    const QFileInfo fi(path);
    // isFile() follows symlinks, so a link to /dev/full or a fifo fails here.
    if (path.isEmpty() || (fi.exists() && !fi.isFile())) {
        if (error)
            *error = QStringLiteral("Cannot write ") + path;
        return false;
    }
    return true;
}

namespace {

// commit() already renamed the temp onto `path`. Flush the parent directory
// so that directory entry survives a crash. EINVAL means this filesystem
// cannot fsync a directory; any other failure is a real error.
bool fsyncParentDirectory(const QString &path)
{
#if defined(Q_OS_UNIX)
    const QByteArray dirPath = QFile::encodeName(QFileInfo(path).absolutePath());
    const int fd = ::open(dirPath.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    int rc = -1;
#if defined(F_FULLFSYNC)
    rc = ::fcntl(fd, F_FULLFSYNC);
#else
    rc = ::fsync(fd);
#endif
    const int savedErrno = errno;
    ::close(fd);
    if (rc != 0 && savedErrno != EINVAL)
        return false;
    return true;
#else
    Q_UNUSED(path);
    return true;
#endif
}

} // namespace

bool writeFileAtomic(const QString &path, const QByteArray &bytes, QString *error)
{
    if (!ensureAtomicWriteTarget(path, error))
        return false;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Cannot write ") + path;
        return false;
    }
    if (f.write(bytes) != qint64(bytes.size())) {
        f.cancelWriting();
        if (error)
            *error = QStringLiteral("Cannot write ") + path;
        return false;
    }
    if (!f.commit()) {
        if (error)
            *error = QStringLiteral("Cannot write ") + path;
        return false;
    }
    if (!fsyncParentDirectory(path)) {
        if (error)
            *error = QStringLiteral("Cannot write ") + path;
        return false;
    }
    return true;
}

namespace {

quint32 crcOf(const QByteArray &data)
{
    return static_cast<quint32>(::crc32(0L, reinterpret_cast<const Bytef *>(data.constData()),
                                        static_cast<uInt>(data.size())));
}

QByteArray deflateRaw(const QByteArray &in)
{
    if (in.isEmpty())
        return {};
    z_stream strm{};
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return {};
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.constData()));
    strm.avail_in = static_cast<uInt>(in.size());
    QByteArray out;
    out.resize(static_cast<int>(compressBound(static_cast<uLong>(in.size()))));
    strm.next_out = reinterpret_cast<Bytef *>(out.data());
    strm.avail_out = static_cast<uInt>(out.size());
    const int ret = deflate(&strm, Z_FINISH);
    const int produced = static_cast<int>(strm.total_out);
    deflateEnd(&strm);
    if (ret != Z_STREAM_END)
        return {};
    out.resize(produced);
    return out;
}

// `tooLarge` is set when the bytes zlib actually produces would pass the
// per-entry cap or `budget` (what this entry may still add to the archive).
// The buffer grows with that output. It does not start at the declared size:
// that field is attacker-controlled, and 0 means "unknown", not "empty".
QByteArray inflateRaw(const QByteArray &in, quint32 uncompressed, quint64 budget, bool *tooLarge)
{
    if (tooLarge)
        *tooLarge = false;
    const quint32 entryCap = zipEntryCap();
    if (uncompressed > entryCap) {
        if (tooLarge)
            *tooLarge = true;
        return {};
    }
    if (in.isEmpty())
        return {};
    quint64 cap64 = qMin(static_cast<quint64>(entryCap), budget);
    if (uncompressed > 0)
        cap64 = qMin(cap64, static_cast<quint64>(uncompressed));
    if (cap64 < 1) {
        if (tooLarge)
            *tooLarge = true;
        return {};
    }
    const int cap = static_cast<int>(cap64);
    z_stream strm{};
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
        return {};
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.constData()));
    strm.avail_in = static_cast<uInt>(in.size());
    const qint64 guess = qint64(in.size()) * 4 + 64;
    int start = static_cast<int>(qMin(guess, qint64(cap)));
    if (start < 1)
        start = 1;
    QByteArray out;
    out.resize(start);
    for (;;) {
        strm.next_out = reinterpret_cast<Bytef *>(out.data() + strm.total_out);
        strm.avail_out = static_cast<uInt>(out.size() - static_cast<int>(strm.total_out));
        const int ret = inflate(&strm, Z_FINISH);
        if (ret == Z_STREAM_END) {
            if (strm.total_out > static_cast<uLong>(cap)) {
                inflateEnd(&strm);
                if (tooLarge)
                    *tooLarge = true;
                return {};
            }
            out.resize(static_cast<int>(strm.total_out));
            inflateEnd(&strm);
            return out;
        }
        if (ret == Z_BUF_ERROR || ret == Z_OK) {
            if (static_cast<qint64>(strm.total_out) >= cap || out.size() >= cap) {
                inflateEnd(&strm);
                if (tooLarge)
                    *tooLarge = true;
                return {};
            }
            const int grown = out.size() > cap / 2 ? cap : out.size() * 2;
            out.resize(grown);
            continue;
        }
        inflateEnd(&strm);
        return {};
    }
}

void put16(QByteArray &b, quint16 v)
{
    b.append(char(v & 0xff));
    b.append(char((v >> 8) & 0xff));
}

void put32(QByteArray &b, quint32 v)
{
    b.append(char(v & 0xff));
    b.append(char((v >> 8) & 0xff));
    b.append(char((v >> 16) & 0xff));
    b.append(char((v >> 24) & 0xff));
}

quint16 u16(const QByteArray &b, int off)
{
    if (off + 1 >= b.size())
        return 0;
    return quint16(quint8(b[off])) | (quint16(quint8(b[off + 1])) << 8);
}

quint32 u32(const QByteArray &b, int off)
{
    if (off + 3 >= b.size())
        return 0;
    return quint32(quint8(b[off])) | (quint32(quint8(b[off + 1])) << 8)
        | (quint32(quint8(b[off + 2])) << 16) | (quint32(quint8(b[off + 3])) << 24);
}

} // namespace

QByteArray Zip::pack(const QVector<ZipEntry> &entries)
{
    QByteArray local;
    QByteArray central;
    struct Rec {
        QByteArray name;
        quint32 crc = 0;
        quint32 comp = 0;
        quint32 uncomp = 0;
        quint16 method = 0;
        quint32 offset = 0;
    };
    QVector<Rec> recs;
    recs.reserve(entries.size());

    for (const auto &e : entries) {
        Rec r;
        r.name = e.name.toUtf8();
        r.uncomp = static_cast<quint32>(e.data.size());
        r.crc = crcOf(e.data);
        r.offset = static_cast<quint32>(local.size());
        QByteArray payload;
        if (e.store) {
            r.method = 0;
            payload = e.data;
        } else {
            payload = deflateRaw(e.data);
            if (payload.isEmpty() && !e.data.isEmpty()) {
                r.method = 0;
                payload = e.data;
            } else {
                r.method = 8;
            }
        }
        r.comp = static_cast<quint32>(payload.size());

        local += "PK\x03\x04";
        put16(local, 20);
        put16(local, 0);
        put16(local, r.method);
        put16(local, 0);
        put16(local, 0);
        put32(local, r.crc);
        put32(local, r.comp);
        put32(local, r.uncomp);
        put16(local, static_cast<quint16>(r.name.size()));
        put16(local, 0);
        local += r.name;
        local += payload;
        recs.push_back(r);
    }

    const quint32 centralOff = static_cast<quint32>(local.size());
    for (const auto &r : recs) {
        central += "PK\x01\x02";
        put16(central, 0x0314);
        put16(central, 20);
        put16(central, 0);
        put16(central, r.method);
        put16(central, 0);
        put16(central, 0);
        put32(central, r.crc);
        put32(central, r.comp);
        put32(central, r.uncomp);
        put16(central, static_cast<quint16>(r.name.size()));
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, 0);
        put32(central, r.offset);
        central += r.name;
    }

    QByteArray eocd;
    eocd += "PK\x05\x06";
    put16(eocd, 0);
    put16(eocd, 0);
    put16(eocd, static_cast<quint16>(recs.size()));
    put16(eocd, static_cast<quint16>(recs.size()));
    put32(eocd, static_cast<quint32>(central.size()));
    put32(eocd, centralOff);
    put16(eocd, 0);

    return local + central + eocd;
}

bool Zip::unpack(const QByteArray &zip, QVector<ZipEntry> &out, QString *error)
{
    out.clear();
    if (qint64(zip.size()) > kMaxZipBytes) {
        if (error)
            *error = QStringLiteral("archive entry too large");
        return false;
    }
    if (zip.size() < 22) {
        if (error)
            *error = QStringLiteral("ZIP too small");
        return false;
    }
    int eocd = -1;
    for (int i = zip.size() - 22; i >= 0 && i >= zip.size() - 22 - 65535; --i) {
        if (i + 3 < zip.size() && zip[i] == 'P' && zip[i + 1] == 'K' && zip[i + 2] == 5 && zip[i + 3] == 6) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        if (error)
            *error = QStringLiteral("ZIP EOCD not found");
        return false;
    }
    const quint16 nfiles = u16(zip, eocd + 10);
    quint32 central = u32(zip, eocd + 16);
    // Running total of bytes actually produced. A central-directory size of 0
    // must not skip this: several such entries otherwise sum past the archive cap.
    quint64 totalProduced = 0;
    const quint64 totalCap = static_cast<quint64>(zipTotalCap());
    const quint32 entryCap = zipEntryCap();
    const auto rejectTooLarge = [&]() {
        out.clear();
        if (error)
            *error = QStringLiteral("archive entry too large");
        return false;
    };
    for (quint16 i = 0; i < nfiles; ++i) {
        if (central + 46 > static_cast<quint32>(zip.size()) || zip.mid(static_cast<int>(central), 4) != "PK\x01\x02") {
            if (error)
                *error = QStringLiteral("Bad central directory");
            return false;
        }
        const quint16 method = u16(zip, static_cast<int>(central) + 10);
        const quint32 comp = u32(zip, static_cast<int>(central) + 20);
        const quint32 uncomp = u32(zip, static_cast<int>(central) + 24);
        const quint16 nameLen = u16(zip, static_cast<int>(central) + 28);
        const quint16 extraLen = u16(zip, static_cast<int>(central) + 30);
        const quint16 commentLen = u16(zip, static_cast<int>(central) + 32);
        const quint32 localOff = u32(zip, static_cast<int>(central) + 42);
        const QByteArray name = zip.mid(static_cast<int>(central) + 46, nameLen);
        const quint16 localName = u16(zip, static_cast<int>(localOff) + 26);
        const quint16 localExtra = u16(zip, static_cast<int>(localOff) + 28);
        const int dataOff = static_cast<int>(localOff) + 30 + localName + localExtra;
        const QByteArray packed = zip.mid(dataOff, static_cast<int>(comp));
        // A declared size already over the cap is rejected before inflation.
        // A declared 0 is not a size, so only the bytes produced below count.
        if (uncomp > entryCap || (uncomp > 0 && totalProduced + uncomp > totalCap))
            return rejectTooLarge();
        ZipEntry e;
        e.name = QString::fromUtf8(name);
        if (method == 0) {
            const quint64 produced = static_cast<quint64>(packed.size());
            if (produced > entryCap || totalProduced + produced > totalCap)
                return rejectTooLarge();
            e.data = packed;
            e.store = true;
            totalProduced += produced;
        } else if (method == 8) {
            bool tooLarge = false;
            const quint64 remaining = totalCap - totalProduced;
            e.data = inflateRaw(packed, uncomp, remaining, &tooLarge);
            if (tooLarge)
                return rejectTooLarge();
            if (e.data.isEmpty() && uncomp > 0) {
                if (error)
                    *error = QStringLiteral("Inflate failed for ") + e.name;
                return false;
            }
            const quint64 produced = static_cast<quint64>(e.data.size());
            if (produced > entryCap || totalProduced + produced > totalCap)
                return rejectTooLarge();
            totalProduced += produced;
        } else {
            if (error)
                *error = QStringLiteral("Unsupported ZIP method");
            return false;
        }
        out.push_back(e);
        central += 46u + nameLen + extraLen + commentLen;
    }
    return true;
}

QByteArray Zip::file(const QVector<ZipEntry> &entries, const QString &name)
{
    for (const auto &e : entries) {
        if (e.name == name)
            return e.data;
    }
    return {};
}

} // namespace aioffice
