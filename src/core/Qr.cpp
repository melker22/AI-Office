#include "core/Qr.h"

#include <QByteArray>
#include <QImage>
#include <QVector>
#include <cstdint>

namespace aioffice {
namespace {

uint8_t gfMul(uint8_t a, uint8_t b)
{
    uint8_t p = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1)
            p ^= a;
        const bool hi = a & 0x80;
        a = uint8_t(a << 1);
        if (hi)
            a ^= 0x1d;
        b >>= 1;
    }
    return p;
}

QByteArray rsEcc(const QByteArray &data, int eccLen)
{
    QByteArray gen(eccLen, 0);
    gen[eccLen - 1] = 1;
    uint8_t alpha = 1;
    for (int i = 0; i < eccLen; ++i) {
        for (int j = 0; j < eccLen; ++j) {
            gen[j] = gfMul(gen[j], alpha);
            if (j + 1 < eccLen)
                gen[j] ^= gen[j + 1];
        }
        alpha = gfMul(alpha, 2);
    }
    QByteArray ecc(eccLen, 0);
    for (uint8_t byte : data) {
        const uint8_t factor = uint8_t(byte) ^ uint8_t(ecc[0]);
        for (int i = 0; i < eccLen - 1; ++i)
            ecc[i] = ecc[i + 1] ^ gfMul(gen[i], factor);
        ecc[eccLen - 1] = gfMul(gen[eccLen - 1], factor);
    }
    return ecc;
}

void setMod(QVector<QVector<int>> &m, int x, int y, int v, QVector<QVector<int>> &lock)
{
    const int n = m.size();
    if (x < 0 || y < 0 || x >= n || y >= n)
        return;
    m[y][x] = v;
    lock[y][x] = 1;
}

void finder(QVector<QVector<int>> &m, QVector<QVector<int>> &lock, int x, int y)
{
    for (int dy = -1; dy <= 7; ++dy) {
        for (int dx = -1; dx <= 7; ++dx) {
            const int xx = x + dx;
            const int yy = y + dy;
            const bool ring = dx == -1 || dx == 7 || dy == -1 || dy == 7;
            const bool outer = dx == 0 || dx == 6 || dy == 0 || dy == 6;
            const bool core = dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4;
            int v = 0;
            if (dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6)
                v = (outer || core) ? 1 : 0;
            if (ring)
                v = 0;
            setMod(m, xx, yy, v, lock);
        }
    }
}

} // namespace

QImage encodeQr(const QString &text, int modulePx)
{
    QByteArray raw = text.toUtf8();
    if (raw.size() > 17)
        raw = raw.left(17);
    const int size = 21;
    QByteArray payload;
    int acc = 0;
    int accN = 0;
    auto putBits = [&](int bits, int n) {
        for (int i = n - 1; i >= 0; --i) {
            acc = (acc << 1) | ((bits >> i) & 1);
            ++accN;
            if (accN == 8) {
                payload.append(char(acc));
                acc = 0;
                accN = 0;
            }
        }
    };
    putBits(0b0100, 4);
    putBits(int(raw.size()), 8);
    for (uint8_t b : raw)
        putBits(b, 8);
    putBits(0, 4);
    if (accN > 0) {
        acc <<= (8 - accN);
        payload.append(char(acc));
        acc = 0;
        accN = 0;
    }
    const char pads[] = {char(0xEC), char(0x11)};
    int p = 0;
    while (payload.size() < 19)
        payload.append(pads[(p++) & 1]);
    payload = payload.left(19);
    payload += rsEcc(payload, 7);

    QVector<QVector<int>> m(size, QVector<int>(size, 0));
    QVector<QVector<int>> lock(size, QVector<int>(size, 0));
    finder(m, lock, 0, 0);
    finder(m, lock, size - 7, 0);
    finder(m, lock, 0, size - 7);
    for (int i = 8; i < size - 8; ++i) {
        setMod(m, 6, i, i % 2 == 0, lock);
        setMod(m, i, 6, i % 2 == 0, lock);
    }
    setMod(m, 8, size - 8, 1, lock);

    int bit = 0;
    const int totalBits = payload.size() * 8;
    auto bitAt = [&](int i) { return (uint8_t(payload[i / 8]) >> (7 - (i % 8))) & 1; };
    int col = size - 1;
    int dir = -1;
    while (col > 0) {
        if (col == 6)
            --col;
        for (int pass = 0; pass < size; ++pass) {
            const int y = dir < 0 ? (size - 1 - pass) : pass;
            for (int dx = 0; dx < 2; ++dx) {
                const int x = col - dx;
                if (lock[y][x])
                    continue;
                int v = bit < totalBits ? bitAt(bit++) : 0;
                if (((x + y) & 1) == 0)
                    v ^= 1;
                m[y][x] = v;
            }
        }
        col -= 2;
        dir = -dir;
    }

    const int fmt = 0x5412;
    auto placeFmt = [&](int i, int v) {
        if (i < 6)
            setMod(m, i, 8, v, lock);
        else if (i < 8)
            setMod(m, i + 1, 8, v, lock);
        else
            setMod(m, size - 15 + i, 8, v, lock);
        if (i < 8)
            setMod(m, 8, size - 1 - i, v, lock);
        else
            setMod(m, 8, 14 - i, v, lock);
    };
    for (int i = 0; i < 15; ++i)
        placeFmt(i, (fmt >> i) & 1);

    const int px = qMax(2, modulePx);
    const int quiet = 4;
    const int dim = (size + quiet * 2) * px;
    QImage img(dim, dim, QImage::Format_RGB32);
    img.fill(qRgb(255, 255, 255));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (!m[y][x])
                continue;
            for (int dy = 0; dy < px; ++dy)
                for (int dx = 0; dx < px; ++dx)
                    img.setPixel((x + quiet) * px + dx, (y + quiet) * px + dy, qRgb(42, 36, 32));
        }
    }
    return img;
}

} // namespace aioffice
