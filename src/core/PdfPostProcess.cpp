#include "core/PdfPostProcess.h"

#include <QFile>
#include <QMap>
#include <QPair>
#include <QtGlobal>
#include <algorithm>
#include <cstring>
#include <zlib.h>

namespace aioffice {
namespace {

// Locates "N 0 obj" ... "endobj" for a specific object number. Anchored on a leading '\n' since
// every object QPdfWriter emits starts at column 0 — this is a lightweight scanner for our own
// controlled, uncompressed-structure output (no object streams), not a general PDF parser, so it
// deliberately doesn't need to handle arbitrary third-party PDFs.
QByteArray findObjBody(const QByteArray &data, int num, bool *found = nullptr)
{
    const QByteArray needle = "\n" + QByteArray::number(num) + " 0 obj";
    int start = data.indexOf(needle);
    if (start < 0) {
        if (found)
            *found = false;
        return {};
    }
    start += needle.size();
    const int end = data.indexOf("endobj", start);
    if (end < 0) {
        if (found)
            *found = false;
        return {};
    }
    if (found)
        *found = true;
    return data.mid(start, end - start);
}

int extractInt(const QByteArray &body, const QByteArray &key)
{
    int i = body.indexOf(key);
    if (i < 0)
        return -1;
    i += key.size();
    while (i < body.size() && (body[i] == ' ' || body[i] == '\n' || body[i] == '\r' || body[i] == '\t'))
        ++i;
    int j = i;
    while (j < body.size() && body[j] >= '0' && body[j] <= '9')
        ++j;
    if (j == i)
        return -1;
    return body.mid(i, j - i).toInt();
}

// Extracts every "N 0 R" reference inside the first "[ ... ]" that follows `key`.
QVector<int> extractRefArray(const QByteArray &body, const QByteArray &key)
{
    QVector<int> out;
    int i = body.indexOf(key);
    if (i < 0)
        return out;
    i = body.indexOf('[', i);
    if (i < 0)
        return out;
    const int close = body.indexOf(']', i);
    if (close < 0)
        return out;
    const QByteArray inner = body.mid(i + 1, close - i - 1);
    int p = 0;
    while (p < inner.size()) {
        while (p < inner.size() && !(inner[p] >= '0' && inner[p] <= '9'))
            ++p;
        const int s = p;
        while (p < inner.size() && inner[p] >= '0' && inner[p] <= '9')
            ++p;
        if (p > s)
            out.push_back(inner.mid(s, p - s).toInt());
        else
            break;
        while (p < inner.size() && inner[p] != 'R') // skip "0 R"
            ++p;
        if (p < inner.size())
            ++p;
    }
    return out;
}

QByteArray pdfUtf16String(const QString &s)
{
    QByteArray out;
    out.append(char(0xFE));
    out.append(char(0xFF));
    for (const QChar &c : s) {
        const ushort u = c.unicode();
        for (char b : {char((u >> 8) & 0xFF), char(u & 0xFF)}) {
            if (b == '(' || b == ')' || b == '\\')
                out.append('\\');
            out.append(b);
        }
    }
    return out;
}

QByteArray pdfNum(double v) { return QByteArray::number(v, 'f', 2); }
QByteArray tenDigit(int v) { return QByteArray::number(v).rightJustified(10, '0'); }

int parseStartXrefValue(const QByteArray &data, int startXrefAt)
{
    int p = startXrefAt + int(std::strlen("startxref"));
    while (p < data.size() && (data[p] == '\n' || data[p] == '\r' || data[p] == ' '))
        ++p;
    int q = p;
    while (q < data.size() && data[q] >= '0' && data[q] <= '9')
        ++q;
    return data.mid(p, q - p).toInt();
}

// Small helper bundling the incremental-update bookkeeping shared by every post-processing pass:
// new/redefined objects get appended (never touching existing bytes), then finish() writes a new
// xref section + trailer (with /Prev chaining back to whatever was already there) and the file.
struct IncrementalUpdate {
    QByteArray data;
    QByteArray appended;
    QMap<int, int> newOffset; // objNum -> byte offset, QMap keeps keys sorted (xref wants that)
    int nextObj = 1;
    int rootNum = 0;
    int trailerSize = 0;
    int oldStartXref = 0;
    bool ok = false;
    QString error;

    static IncrementalUpdate open(const QString &path)
    {
        IncrementalUpdate u;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            u.error = QStringLiteral("Cannot reopen PDF for post-processing: ") + path;
            return u;
        }
        u.data = f.readAll();
        f.close();
        const int trailerAt = u.data.lastIndexOf("trailer");
        const int startXrefAt = u.data.lastIndexOf("startxref");
        if (trailerAt < 0 || startXrefAt < 0) {
            u.error = QStringLiteral("PDF has no trailer/startxref (unexpected writer output)");
            return u;
        }
        const QByteArray trailerBody = u.data.mid(trailerAt, u.data.size() - trailerAt);
        u.rootNum = extractInt(trailerBody, "/Root");
        u.trailerSize = extractInt(trailerBody, "/Size");
        if (u.rootNum <= 0 || u.trailerSize <= 0) {
            u.error = QStringLiteral("PDF trailer missing /Root or /Size");
            return u;
        }
        u.oldStartXref = parseStartXrefValue(u.data, startXrefAt);
        u.nextObj = qMax(u.trailerSize, 1);
        u.ok = true;
        return u;
    }

    int appendObj(const QByteArray &body)
    {
        const int num = nextObj++;
        newOffset[num] = data.size() + appended.size();
        appended += QByteArray::number(num) + " 0 obj\n" + body + "\nendobj\n";
        return num;
    }

    int reserveObj() { return nextObj++; }

    void redefineObj(int num, const QByteArray &body)
    {
        newOffset[num] = data.size() + appended.size();
        appended += QByteArray::number(num) + " 0 obj\n" + body + "\nendobj\n";
        nextObj = qMax(nextObj, num + 1);
    }

    // Appends the new xref section + trailer (chaining back via /Prev) and writes only the
    // increment — original bytes stay untouched. No-op if nothing was appended.
    bool finish(const QString &path, QString *errorOut)
    {
        if (newOffset.isEmpty())
            return true;
        QByteArray xrefSection = "xref\n";
        for (auto it = newOffset.constBegin(); it != newOffset.constEnd(); ++it) {
            xrefSection += QByteArray::number(it.key()) + " 1\n";
            xrefSection += tenDigit(it.value()) + " 00000 n \n";
        }
        const int xrefOffset = data.size() + appended.size();
        appended += xrefSection;
        const int newSize = qMax(trailerSize, nextObj);
        appended += "trailer\n<< /Size " + QByteArray::number(newSize) + " /Root " + QByteArray::number(rootNum)
            + " 0 R /Prev " + QByteArray::number(oldStartXref) + " >>\nstartxref\n"
            + QByteArray::number(xrefOffset) + "\n%%EOF\n";
        QFile out(path);
        if (!out.open(QIODevice::Append)) {
            if (errorOut)
                *errorOut = QStringLiteral("Cannot rewrite PDF: ") + path;
            return false;
        }
        const qint64 n = out.write(appended);
        if (n != appended.size() || !out.flush()) {
            if (errorOut)
                *errorOut = QStringLiteral("Short write while appending PDF update: ") + path;
            return false;
        }
        data += appended;
        return true;
    }
};

// -------- zlib helpers (PDF FlateDecode is RFC1950 zlib-wrapped, same as compress()/uncompress()) --------

QByteArray flateCompress(const QByteArray &raw)
{
    uLongf boundLen = compressBound(uLong(raw.size()));
    QByteArray out(int(boundLen), Qt::Uninitialized);
    uLongf destLen = boundLen;
    if (compress2(reinterpret_cast<Bytef *>(out.data()), &destLen, reinterpret_cast<const Bytef *>(raw.constData()),
                  uLong(raw.size()), Z_BEST_COMPRESSION)
        != Z_OK)
        return {};
    out.resize(int(destLen));
    return out;
}

// Inflates the FlateDecode stream of object `num` (a "stream\n<bytes>\nendstream" body). Locating
// the payload end via zlib's own inflate() consumption (not a textual "endstream" search) is
// deliberate: the compressed bytes are binary and could coincidentally contain that text.
bool inflateObjStream(const QByteArray &data, int num, QByteArray *out)
{
    const QByteArray needle = "\n" + QByteArray::number(num) + " 0 obj";
    const int objStart = data.indexOf(needle);
    if (objStart < 0)
        return false;
    int streamKw = data.indexOf("stream", objStart);
    if (streamKw < 0)
        return false;
    int dataStart = streamKw + 6;
    if (dataStart < data.size() && data[dataStart] == '\r')
        ++dataStart;
    if (dataStart < data.size() && data[dataStart] == '\n')
        ++dataStart;

    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    if (inflateInit(&strm) != Z_OK)
        return false;
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData() + dataStart));
    strm.avail_in = uInt(data.size() - dataStart);

    QByteArray buf(65536, Qt::Uninitialized);
    int total = 0;
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        if (total == buf.size())
            buf.resize(buf.size() * 2);
        strm.next_out = reinterpret_cast<Bytef *>(buf.data() + total);
        strm.avail_out = uInt(buf.size() - total);
        ret = inflate(&strm, Z_NO_FLUSH);
        total = buf.size() - int(strm.avail_out);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&strm);
            return false;
        }
        if (ret != Z_STREAM_END && strm.avail_in == 0)
            break; // truncated / not actually flate — bail out rather than loop forever
    }
    inflateEnd(&strm);
    if (ret != Z_STREAM_END)
        return false;
    buf.resize(total);
    *out = buf;
    return true;
}

// -------- content-stream tokenizer, just enough to find our own known operator idioms --------

struct Tok {
    QByteArray text;
    int start = 0;
    int end = 0;
};

QVector<Tok> tokenize(const QByteArray &s)
{
    QVector<Tok> out;
    int i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t'))
            ++i;
        if (i >= s.size())
            break;
        const int start = i;
        if (s[i] == '(') {
            int depth = 1;
            ++i;
            while (i < s.size() && depth > 0) {
                if (s[i] == '\\')
                    i += 2;
                else {
                    if (s[i] == '(')
                        ++depth;
                    else if (s[i] == ')')
                        --depth;
                    ++i;
                }
            }
        } else if (s[i] == '<') {
            if (i + 1 < s.size() && s[i + 1] == '<') {
                i += 2;
                int depth = 1;
                while (i < s.size() && depth > 0) {
                    if (s[i] == '<' && i + 1 < s.size() && s[i + 1] == '<') {
                        ++depth;
                        i += 2;
                    } else if (s[i] == '>' && i + 1 < s.size() && s[i + 1] == '>') {
                        --depth;
                        i += 2;
                    } else {
                        ++i;
                    }
                }
            } else {
                ++i;
                while (i < s.size() && s[i] != '>')
                    ++i;
                if (i < s.size())
                    ++i;
            }
        } else if (s[i] == '[' || s[i] == ']') {
            ++i;
        } else {
            while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '(' && s[i] != '<'
                   && s[i] != '[' && s[i] != ']')
                ++i;
        }
        out.push_back({s.mid(start, i - start), start, i});
    }
    return out;
}

bool isNumberTok(const QByteArray &t)
{
    if (t.isEmpty())
        return false;
    bool ok = false;
    t.toDouble(&ok);
    return ok;
}

// End offsets (right after the final "n") of every `x y m (x y l)+ h W*? n` clip-rect sequence —
// exactly what Preview.cpp's `p->setClipRect(lb.rect...)` produces once per text-bearing block,
// and nothing else in our current renderer emits (tables/images/shapes use different operators —
// verified against real generated output before relying on this).
QVector<int> findClipBoundaries(const QVector<Tok> &toks)
{
    QVector<int> ends;
    int i = 0;
    while (i < toks.size()) {
        if (isNumberTok(toks[i].text) && i + 2 < toks.size() && isNumberTok(toks[i + 1].text)
            && toks[i + 2].text == "m") {
            int j = i + 3;
            int lcount = 0;
            while (j + 2 < toks.size() && isNumberTok(toks[j].text) && isNumberTok(toks[j + 1].text)
                   && toks[j + 2].text == "l") {
                ++lcount;
                j += 3;
            }
            if (lcount >= 3 && j < toks.size() && toks[j].text == "h") {
                int k = j + 1;
                if (k < toks.size() && (toks[k].text == "W*" || toks[k].text == "W")) {
                    ++k;
                    if (k < toks.size() && toks[k].text == "n") {
                        ends.push_back(toks[k].end);
                        i = k + 1;
                        continue;
                    }
                }
            }
        }
        ++i;
    }
    return ends;
}

// Byte (start of "BT", end of "ET") for every text object, in order. BT/ET never nest in PDF.
QVector<QPair<int, int>> findTextSpans(const QVector<Tok> &toks)
{
    QVector<QPair<int, int>> out;
    for (int i = 0; i < toks.size(); ++i) {
        if (toks[i].text == "BT") {
            for (int j = i + 1; j < toks.size(); ++j) {
                if (toks[j].text == "ET") {
                    out.push_back({toks[i].start, toks[j].end});
                    i = j;
                    break;
                }
            }
        }
    }
    return out;
}

} // namespace

bool pdfPostProcessAddExtras(const QString &path, const QVector<PdfCommentAnnot> &comments,
                              int physicalPageCount, QString *error)
{
    if (comments.isEmpty())
        return true;

    IncrementalUpdate u = IncrementalUpdate::open(path);
    if (!u.ok) {
        if (error)
            *error = u.error;
        return false;
    }

    bool ok = false;
    const QByteArray catalogBody = findObjBody(u.data, u.rootNum, &ok);
    if (!ok) {
        if (error)
            *error = QStringLiteral("PDF Catalog object not found");
        return false;
    }
    const int pagesNum = extractInt(catalogBody, "/Pages");
    const QByteArray pagesBody = findObjBody(u.data, pagesNum, &ok);
    if (!ok) {
        if (error)
            *error = QStringLiteral("PDF Pages object not found");
        return false;
    }
    const QVector<int> pageNums = extractRefArray(pagesBody, "/Kids");
    if (pageNums.isEmpty()) {
        if (error)
            *error = QStringLiteral("PDF has no pages");
        return false;
    }

    // Group comments by physical page, then redefine each page's own (already-allocated, empty)
    // /Annots array object to reference the new annotation objects.
    QMap<int, QVector<int>> annotsByPage;
    for (const auto &c : comments) {
        if (c.physicalPage < 0 || c.physicalPage >= pageNums.size() || c.physicalPage >= physicalPageCount)
            continue;
        const double llx = c.xPt;
        const double ury = c.yPt; // caller passes PDF (bottom-up) space already
        const double urx = llx + 20;
        const double lly = ury - 20;
        const QByteArray body = "<< /Type /Annot /Subtype /Text /Rect [" + pdfNum(llx) + " " + pdfNum(lly) + " "
            + pdfNum(urx) + " " + pdfNum(ury) + "] /Contents (" + pdfUtf16String(c.text) + ") /T ("
            + pdfUtf16String(c.author) + ") /Name /Comment /Open false >>";
        const int id = u.appendObj(body);
        annotsByPage[c.physicalPage].push_back(id);
    }
    for (auto it = annotsByPage.constBegin(); it != annotsByPage.constEnd(); ++it) {
        const int pageObjNum = pageNums[it.key()];
        const QByteArray pageBody = findObjBody(u.data, pageObjNum, &ok);
        if (!ok)
            continue;
        const int annotsObjNum = extractInt(pageBody, "/Annots");
        if (annotsObjNum <= 0)
            continue;
        QByteArray arr = "[ ";
        for (int id : it.value())
            arr += QByteArray::number(id) + " 0 R ";
        arr += "]";
        u.redefineObj(annotsObjNum, arr);
    }

    return u.finish(path, error);
}

bool pdfPostProcessTagStructure(const QString &path, const QVector<QVector<PdfTaggedBlock>> &perPageBlocks,
                                 QString *error)
{
    IncrementalUpdate u = IncrementalUpdate::open(path);
    if (!u.ok) {
        if (error)
            *error = u.error;
        return false;
    }

    bool ok = false;
    const QByteArray catalogBody = findObjBody(u.data, u.rootNum, &ok);
    if (!ok) {
        if (error)
            *error = QStringLiteral("PDF Catalog object not found");
        return false;
    }
    const int pagesNum = extractInt(catalogBody, "/Pages");
    const QByteArray pagesBody = findObjBody(u.data, pagesNum, &ok);
    if (!ok) {
        if (error)
            *error = QStringLiteral("PDF Pages object not found");
        return false;
    }
    const QVector<int> pageNums = extractRefArray(pagesBody, "/Kids");
    if (pageNums.isEmpty()) {
        if (error)
            *error = QStringLiteral("PDF has no pages");
        return false;
    }

    QVector<int> partOrLeafNums; // top-level children of the /Document struct elem, one per page
    // per-page: array of struct-elem object numbers indexed by mcid, for /ParentTree
    QVector<QPair<int, QVector<int>>> parentTreeEntries; // (page's StructParents index, mcid->elem array)

    const int structRootNum = u.reserveObj();
    const int docElemNum = u.reserveObj();

    for (int pi = 0; pi < pageNums.size(); ++pi) {
        const int pageObjNum = pageNums[pi];
        const QByteArray pageBody = findObjBody(u.data, pageObjNum, &ok);
        if (!ok)
            continue;
        const int contentsNum = extractInt(pageBody, "/Contents");
        const QVector<PdfTaggedBlock> expected = pi < perPageBlocks.size() ? perPageBlocks[pi] : QVector<PdfTaggedBlock>();

        int partElem = -1;
        QVector<int> mcidToElem;

        if (contentsNum > 0 && !expected.isEmpty()) {
            QByteArray raw;
            if (inflateObjStream(u.data, contentsNum, &raw)) {
                const QVector<Tok> toks = tokenize(raw);
                const QVector<int> clipEnds = findClipBoundaries(toks);
                const QVector<QPair<int, int>> spans = findTextSpans(toks);

                // Assign each BT/ET span to the block whose clip-boundary most recently precedes it.
                QVector<QVector<QPair<int, int>>> byBlock(clipEnds.size());
                bool assignmentOk = clipEnds.size() == expected.size();
                if (assignmentOk) {
                    for (const auto &sp : spans) {
                        auto it = std::upper_bound(clipEnds.begin(), clipEnds.end(), sp.first);
                        const int idx = int(it - clipEnds.begin()) - 1;
                        if (idx < 0 || idx >= byBlock.size()) {
                            assignmentOk = false;
                            break;
                        }
                        byBlock[idx].push_back(sp);
                    }
                }
                // Every expected block must have gotten at least its clip boundary counted; spans
                // with zero BT/ET (an empty block) are fine — MCIDs just won't cover it.
                if (assignmentOk) {
                    QVector<QPair<int, QByteArray>> inserts; // (byte pos, text) sorted later
                    int mcid = 0;
                    struct BlockMcids {
                        QVector<int> mcids;
                    };
                    QVector<BlockMcids> blockMcids(expected.size());
                    for (int bi = 0; bi < expected.size(); ++bi) {
                        for (const auto &sp : byBlock[bi]) {
                            const QByteArray tag = expected[bi].structTag.toUtf8();
                            inserts.push_back({sp.first, "/" + tag + " <</MCID " + QByteArray::number(mcid) + ">> BDC\n"});
                            inserts.push_back({sp.second, "\nEMC"});
                            blockMcids[bi].mcids.push_back(mcid);
                            ++mcid;
                        }
                    }
                    std::sort(inserts.begin(), inserts.end(),
                              [](const QPair<int, QByteArray> &a, const QPair<int, QByteArray> &b) { return a.first < b.first; });
                    QByteArray tagged;
                    tagged.reserve(raw.size() + inserts.size() * 24);
                    int cursor = 0;
                    for (const auto &ins : inserts) {
                        tagged += raw.mid(cursor, ins.first - cursor);
                        tagged += ins.second;
                        cursor = ins.first;
                    }
                    tagged += raw.mid(cursor);

                    const QByteArray compressed = flateCompress(tagged);
                    if (!compressed.isEmpty()) {
                        u.redefineObj(contentsNum,
                                      "<< /Length " + QByteArray::number(compressed.size()) + " /Filter /FlateDecode >>\nstream\n"
                                          + compressed + "\nendstream");

                        // one StructElem per non-empty block, /K = its mcids, /Pg = this page, /P = parent
                        QVector<int> kids;
                        mcidToElem.resize(mcid, -1);
                        const int partNum = u.reserveObj();
                        for (int bi = 0; bi < expected.size(); ++bi) {
                            if (blockMcids[bi].mcids.isEmpty())
                                continue;
                            QByteArray kArr = "[ ";
                            for (int m : blockMcids[bi].mcids)
                                kArr += QByteArray::number(m) + " ";
                            kArr += "]";
                            const int elemNum = u.reserveObj();
                            u.redefineObj(elemNum,
                                          "<< /Type /StructElem /S /" + expected[bi].structTag.toUtf8() + " /P "
                                              + QByteArray::number(partNum) + " 0 R /Pg "
                                              + QByteArray::number(pageObjNum) + " 0 R /K " + kArr + " >>");
                            kids.push_back(elemNum);
                            for (int m : blockMcids[bi].mcids)
                                mcidToElem[m] = elemNum;
                        }
                        if (!kids.isEmpty()) {
                            QByteArray kidsArr = "[ ";
                            for (int k : kids)
                                kidsArr += QByteArray::number(k) + " 0 R ";
                            kidsArr += "]";
                            u.redefineObj(partNum, "<< /Type /StructElem /S /Part /P " + QByteArray::number(docElemNum)
                                                       + " 0 R /Pg " + QByteArray::number(pageObjNum) + " 0 R /K "
                                                       + kidsArr + " >>");
                            partElem = partNum;
                        } else {
                            u.redefineObj(partNum, "<< /Type /StructElem /S /Part /P " + QByteArray::number(docElemNum)
                                                       + " 0 R /Pg " + QByteArray::number(pageObjNum) + " 0 R >>");
                            partElem = partNum;
                        }
                    }
                }
            }
        }

        if (partElem < 0) {
            // Fallback: page-level tag only, same as wave 13, when content-stream correlation
            // wasn't possible (unsupported block types on the page, or an unexpected mismatch).
            partElem = u.appendObj("<< /Type /StructElem /S /Part /P " + QByteArray::number(docElemNum) + " 0 R /Pg "
                                   + QByteArray::number(pageObjNum) + " 0 R >>");
        }
        partOrLeafNums.push_back(partElem);

        const int structParentsIdx = pi;
        u.redefineObj(pageObjNum, [&]() {
            QByteArray nb = pageBody.trimmed();
            if (nb.endsWith(">>"))
                nb.chop(2);
            nb += " /StructParents " + QByteArray::number(structParentsIdx) + " >>\n";
            return nb;
        }());

        if (!mcidToElem.isEmpty())
            parentTreeEntries.push_back({structParentsIdx, mcidToElem});
    }

    {
        QByteArray kids = "[ ";
        for (int n : partOrLeafNums)
            kids += QByteArray::number(n) + " 0 R ";
        kids += "]";
        u.redefineObj(docElemNum, "<< /Type /StructElem /S /Document /P " + QByteArray::number(structRootNum)
                                      + " 0 R /K " + kids + " >>");
    }

    int parentTreeNum = -1;
    if (!parentTreeEntries.isEmpty()) {
        QByteArray nums = "[ ";
        for (const auto &e : parentTreeEntries) {
            nums += QByteArray::number(e.first) + " [ ";
            for (int elem : e.second)
                nums += (elem >= 0 ? QByteArray::number(elem) + " 0 R " : QByteArray("null "));
            nums += "] ";
        }
        nums += "]";
        parentTreeNum = u.appendObj("<< /Nums " + nums + " >>");
    }

    QByteArray structRoot = "<< /Type /StructTreeRoot /K [ " + QByteArray::number(docElemNum) + " 0 R ]";
    if (parentTreeNum >= 0)
        structRoot += " /ParentTree " + QByteArray::number(parentTreeNum) + " 0 R";
    structRoot += " >>";
    u.redefineObj(structRootNum, structRoot);

    QByteArray newCatalog = catalogBody.trimmed();
    if (newCatalog.endsWith(">>"))
        newCatalog.chop(2);
    newCatalog += " /MarkInfo << /Marked true >> /StructTreeRoot " + QByteArray::number(structRootNum) + " 0 R >>\n";
    u.redefineObj(u.rootNum, newCatalog);

    return u.finish(path, error);
}

} // namespace aioffice
