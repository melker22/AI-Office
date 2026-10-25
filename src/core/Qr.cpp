#include "core/Qr.h"

#include <QByteArray>
#include <QImage>
#include <QVector>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

namespace aioffice {
namespace {

// Level M only. Index is the QR version (1–10).
constexpr int kEccPerBlock[11] = {0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26};
constexpr int kNumBlocks[11] = {0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5};
constexpr int kMaxVersion = 10;

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

QVector<uint8_t> rsDivisor(int degree)
{
    QVector<uint8_t> result(degree, 0);
    result[degree - 1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; ++i) {
        for (int j = 0; j < degree; ++j) {
            result[j] = gfMul(result[j], root);
            if (j + 1 < degree)
                result[j] ^= result[j + 1];
        }
        root = gfMul(root, 2);
    }
    return result;
}

QVector<uint8_t> rsRemainder(const QVector<uint8_t> &data, const QVector<uint8_t> &divisor)
{
    QVector<uint8_t> result(divisor.size(), 0);
    for (uint8_t b : data) {
        const uint8_t factor = uint8_t(b ^ result[0]);
        for (int i = 0; i < result.size() - 1; ++i)
            result[i] = result[i + 1];
        result[result.size() - 1] = 0;
        for (int i = 0; i < result.size(); ++i)
            result[i] ^= gfMul(divisor[i], factor);
    }
    return result;
}

int rawModules(int version)
{
    int result = (16 * version + 128) * version + 64;
    if (version >= 2) {
        const int numAlign = version / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (version >= 7)
            result -= 36;
    }
    return result;
}

int numDataCodewords(int version)
{
    return rawModules(version) / 8 - kEccPerBlock[version] * kNumBlocks[version];
}

int countBitsFor(int version)
{
    return version <= 9 ? 8 : 16;
}

// Smallest version whose byte-mode capacity holds `nbytes`, or 0.
int chooseVersion(int nbytes)
{
    for (int version = 1; version <= kMaxVersion; ++version) {
        const int need = 4 + countBitsFor(version) + nbytes * 8;
        if (need <= numDataCodewords(version) * 8)
            return version;
    }
    return 0;
}

QVector<uint8_t> dataCodewords(const QByteArray &payload, int version)
{
    QVector<int> bits;
    bits.reserve(numDataCodewords(version) * 8);
    auto put = [&](int value, int length) {
        for (int i = length - 1; i >= 0; --i)
            bits.push_back((value >> i) & 1);
    };
    put(0b0100, 4);
    put(payload.size(), countBitsFor(version));
    for (int i = 0; i < payload.size(); ++i)
        put(uint8_t(payload[i]), 8);
    const int capacity = numDataCodewords(version) * 8;
    put(0, qMin(4, capacity - bits.size()));
    while (bits.size() % 8 != 0)
        bits.push_back(0);
    int pad = 0xEC;
    while (bits.size() < capacity) {
        put(pad, 8);
        pad ^= 0xEC ^ 0x11;
    }
    QVector<uint8_t> out(bits.size() / 8, 0);
    for (int i = 0; i < bits.size(); ++i) {
        if (bits[i])
            out[i / 8] = uint8_t(out[i / 8] | (1u << (7 - (i % 8))));
    }
    return out;
}

QVector<uint8_t> interleave(const QVector<uint8_t> &data, int version)
{
    const int numBlocks = kNumBlocks[version];
    const int eccLen = kEccPerBlock[version];
    const int raw = rawModules(version) / 8;
    const int numShort = numBlocks - raw % numBlocks;
    const int shortLen = raw / numBlocks;
    const QVector<uint8_t> divisor = rsDivisor(eccLen);
    QVector<QVector<uint8_t>> blocks;
    blocks.reserve(numBlocks);
    int k = 0;
    for (int i = 0; i < numBlocks; ++i) {
        const int dataLen = shortLen - eccLen + (i < numShort ? 0 : 1);
        QVector<uint8_t> block;
        block.reserve(shortLen + 1);
        for (int n = 0; n < dataLen; ++n)
            block.push_back(data[k + n]);
        k += dataLen;
        const QVector<uint8_t> ecc = rsRemainder(block, divisor);
        if (i < numShort)
            block.push_back(0);
        for (uint8_t e : ecc)
            block.push_back(e);
        blocks.push_back(block);
    }
    QVector<uint8_t> result;
    result.reserve(raw);
    const int skipAt = shortLen - eccLen;
    for (int i = 0; i < blocks[0].size(); ++i) {
        for (int j = 0; j < blocks.size(); ++j) {
            if (i == skipAt && j < numShort)
                continue;
            result.push_back(blocks[j][i]);
        }
    }
    return result;
}

unsigned formatBits(int mask)
{
    // Level M is the 2-bit value 00. The 10 BCH bits follow, then the 0x5412 mask.
    unsigned rem = unsigned(mask);
    for (int i = 0; i < 10; ++i)
        rem = (rem << 1) ^ ((rem >> 9) * 0x537u);
    return (unsigned(mask) << 10 | rem) ^ 0x5412u;
}

unsigned versionBits(int version)
{
    unsigned rem = unsigned(version);
    for (int i = 0; i < 12; ++i)
        rem = (rem << 1) ^ ((rem >> 11) * 0x1F25u);
    return (unsigned(version) << 12) | rem;
}

QVector<int> alignmentPositions(int version, int size)
{
    if (version == 1)
        return {};
    const int numAlign = version / 7 + 2;
    const int step = (version * 8 + numAlign * 3 + 5) / (numAlign * 4 - 4) * 2;
    QVector<int> result;
    int pos = size - 7;
    for (int i = 0; i < numAlign - 1; ++i) {
        result.prepend(pos);
        pos -= step;
    }
    result.prepend(6);
    return result;
}

struct Grid {
    int n = 0;
    QVector<uint8_t> dark;
    QVector<uint8_t> function;

    explicit Grid(int size)
        : n(size)
        , dark(size * size, 0)
        , function(size * size, 0)
    {
    }

    void setFunction(int x, int y, bool isDark)
    {
        if (x < 0 || y < 0 || x >= n || y >= n)
            return;
        dark[y * n + x] = isDark ? 1 : 0;
        function[y * n + x] = 1;
    }

    bool module(int x, int y) const { return dark[y * n + x] != 0; }
};

void drawFinder(Grid &g, int x, int y)
{
    for (int dy = -4; dy <= 4; ++dy) {
        for (int dx = -4; dx <= 4; ++dx) {
            const int dist = qMax(qAbs(dx), qAbs(dy));
            g.setFunction(x + dx, y + dy, dist != 2 && dist != 4);
        }
    }
}

void drawAlignment(Grid &g, int x, int y)
{
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx)
            g.setFunction(x + dx, y + dy, qMax(qAbs(dx), qAbs(dy)) != 1);
    }
}

void drawFormat(Grid &g, int mask)
{
    const unsigned bits = formatBits(mask);
    const int size = g.n;
    for (int i = 0; i <= 5; ++i)
        g.setFunction(8, i, (bits >> i) & 1u);
    g.setFunction(8, 7, (bits >> 6) & 1u);
    g.setFunction(8, 8, (bits >> 7) & 1u);
    g.setFunction(7, 8, (bits >> 8) & 1u);
    for (int i = 9; i < 15; ++i)
        g.setFunction(14 - i, 8, (bits >> i) & 1u);
    for (int i = 0; i < 8; ++i)
        g.setFunction(size - 1 - i, 8, (bits >> i) & 1u);
    for (int i = 8; i < 15; ++i)
        g.setFunction(8, size - 15 + i, (bits >> i) & 1u);
    g.setFunction(8, size - 8, true);
}

void drawVersion(Grid &g, int version)
{
    if (version < 7)
        return;
    const unsigned bits = versionBits(version);
    const int size = g.n;
    for (int i = 0; i < 18; ++i) {
        const bool bit = (bits >> i) & 1u;
        const int a = size - 11 + i % 3;
        const int b = i / 3;
        g.setFunction(a, b, bit);
        g.setFunction(b, a, bit);
    }
}

void drawFunctionPatterns(Grid &g, int version)
{
    const int size = g.n;
    for (int i = 0; i < size; ++i) {
        g.setFunction(6, i, i % 2 == 0);
        g.setFunction(i, 6, i % 2 == 0);
    }
    drawFinder(g, 3, 3);
    drawFinder(g, size - 4, 3);
    drawFinder(g, 3, size - 4);
    const QVector<int> pos = alignmentPositions(version, size);
    const int num = pos.size();
    for (int i = 0; i < num; ++i) {
        for (int j = 0; j < num; ++j) {
            if ((i == 0 && j == 0) || (i == 0 && j == num - 1) || (i == num - 1 && j == 0))
                continue;
            drawAlignment(g, pos[i], pos[j]);
        }
    }
    drawFormat(g, 0);
    drawVersion(g, version);
}

void drawCodewords(Grid &g, const QVector<uint8_t> &data)
{
    const int size = g.n;
    int bit = 0;
    const int total = data.size() * 8;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6)
            right = 5;
        const bool upward = ((right + 1) & 2) == 0;
        for (int vert = 0; vert < size; ++vert) {
            for (int j = 0; j < 2; ++j) {
                const int x = right - j;
                const int y = upward ? size - 1 - vert : vert;
                if (!g.function[y * size + x] && bit < total) {
                    g.dark[y * size + x] = (data[bit >> 3] >> (7 - (bit & 7))) & 1;
                    ++bit;
                }
            }
        }
    }
}

bool maskInvert(int mask, int x, int y)
{
    switch (mask) {
    case 0:
        return (x + y) % 2 == 0;
    case 1:
        return y % 2 == 0;
    case 2:
        return x % 3 == 0;
    case 3:
        return (x + y) % 3 == 0;
    case 4:
        return (x / 3 + y / 2) % 2 == 0;
    case 5:
        return (x * y) % 2 + (x * y) % 3 == 0;
    case 6:
        return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
    case 7:
        return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
    default:
        return false;
    }
}

void applyMask(Grid &g, int mask)
{
    for (int y = 0; y < g.n; ++y) {
        for (int x = 0; x < g.n; ++x) {
            if (g.function[y * g.n + x])
                continue;
            if (maskInvert(mask, x, y))
                g.dark[y * g.n + x] ^= 1;
        }
    }
}

void addHistory(int currentRunLength, std::array<int, 7> &runHistory, int size)
{
    if (runHistory[0] == 0)
        currentRunLength += size;
    std::copy_backward(runHistory.begin(), runHistory.end() - 1, runHistory.end());
    runHistory[0] = currentRunLength;
}

int countPatterns(const std::array<int, 7> &runHistory)
{
    const int n = runHistory[1];
    const bool core = n > 0 && runHistory[2] == n && runHistory[3] == n * 3 && runHistory[4] == n
        && runHistory[5] == n;
    return (core && runHistory[0] >= n * 4 && runHistory[6] >= n ? 1 : 0)
        + (core && runHistory[6] >= n * 4 && runHistory[0] >= n ? 1 : 0);
}

int terminateAndCount(bool currentRunColor, int currentRunLength, std::array<int, 7> &runHistory, int size)
{
    if (currentRunColor) {
        addHistory(currentRunLength, runHistory, size);
        currentRunLength = 0;
    }
    currentRunLength += size;
    addHistory(currentRunLength, runHistory, size);
    return countPatterns(runHistory);
}

int penaltyScore(const Grid &g)
{
    int result = 0;
    for (int y = 0; y < g.n; ++y) {
        bool runColor = false;
        int run = 0;
        std::array<int, 7> history{};
        for (int x = 0; x < g.n; ++x) {
            if (g.module(x, y) == runColor) {
                ++run;
                if (run == 5)
                    result += 3;
                else if (run > 5)
                    ++result;
            } else {
                addHistory(run, history, g.n);
                if (!runColor)
                    result += countPatterns(history) * 40;
                runColor = g.module(x, y);
                run = 1;
            }
        }
        result += terminateAndCount(runColor, run, history, g.n) * 40;
    }
    for (int x = 0; x < g.n; ++x) {
        bool runColor = false;
        int run = 0;
        std::array<int, 7> history{};
        for (int y = 0; y < g.n; ++y) {
            if (g.module(x, y) == runColor) {
                ++run;
                if (run == 5)
                    result += 3;
                else if (run > 5)
                    ++result;
            } else {
                addHistory(run, history, g.n);
                if (!runColor)
                    result += countPatterns(history) * 40;
                runColor = g.module(x, y);
                run = 1;
            }
        }
        result += terminateAndCount(runColor, run, history, g.n) * 40;
    }
    for (int y = 0; y < g.n - 1; ++y) {
        for (int x = 0; x < g.n - 1; ++x) {
            const bool color = g.module(x, y);
            if (color == g.module(x + 1, y) && color == g.module(x, y + 1) && color == g.module(x + 1, y + 1))
                result += 3;
        }
    }
    int dark = 0;
    for (uint8_t bit : g.dark)
        dark += bit;
    const int total = g.n * g.n;
    const int k = int((std::llabs(dark * 20LL - total * 10LL) + total - 1) / total) - 1;
    result += k * 10;
    return result;
}

QImage paint(const Grid &g, int modulePx)
{
    const int px = qMax(2, modulePx);
    constexpr int quiet = 4;
    const int dim = (g.n + quiet * 2) * px;
    QImage img(dim, dim, QImage::Format_RGB32);
    img.fill(qRgb(255, 255, 255));
    const QRgb ink = qRgb(42, 36, 32);
    for (int y = 0; y < g.n; ++y) {
        for (int x = 0; x < g.n; ++x) {
            if (!g.module(x, y))
                continue;
            for (int dy = 0; dy < px; ++dy) {
                for (int dx = 0; dx < px; ++dx)
                    img.setPixel((x + quiet) * px + dx, (y + quiet) * px + dy, ink);
            }
        }
    }
    return img;
}

} // namespace

QString qrCapacityError(const QString &text)
{
    if (chooseVersion(text.toUtf8().size()) == 0)
        return QStringLiteral("QR payload exceeds version 10 level M capacity");
    return {};
}

QImage encodeQr(const QString &text, int modulePx)
{
    const QByteArray payload = text.toUtf8();
    const int version = chooseVersion(payload.size());
    if (version == 0)
        return {};
    const QVector<uint8_t> codewords = interleave(dataCodewords(payload, version), version);
    Grid grid(version * 4 + 17);
    drawFunctionPatterns(grid, version);
    drawCodewords(grid, codewords);

    int bestMask = 0;
    int bestPenalty = 0x7fffffff;
    QVector<uint8_t> bestDark;
    for (int mask = 0; mask < 8; ++mask) {
        applyMask(grid, mask);
        drawFormat(grid, mask);
        const int score = penaltyScore(grid);
        if (score < bestPenalty) {
            bestPenalty = score;
            bestMask = mask;
            bestDark = grid.dark;
        }
        applyMask(grid, mask);
    }
    grid.dark = bestDark;
    drawFormat(grid, bestMask);
    return paint(grid, modulePx);
}

} // namespace aioffice
