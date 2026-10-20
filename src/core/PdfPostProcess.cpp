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

// PDF/A-1 6.1.3: every trailer, including each incremental update, carries the file /ID.
// Copy the array from the first trailer that has one (QPdfWriter's original trailer).
QByteArray originalFileId(const QByteArray &data)
{
    int search = 0;
    while (search < data.size()) {
        const int trailerAt = data.indexOf("trailer", search);
        if (trailerAt < 0)
            break;
        const int idAt = data.indexOf("/ID", trailerAt);
        const int nextTrailer = data.indexOf("trailer", trailerAt + 7);
        if (idAt >= 0 && (nextTrailer < 0 || idAt < nextTrailer)) {
            const int open = data.indexOf('[', idAt);
            if (open >= 0 && (nextTrailer < 0 || open < nextTrailer)) {
                int depth = 0;
                for (int i = open; i < data.size() && (nextTrailer < 0 || i < nextTrailer); ++i) {
                    if (data[i] == '[')
                        ++depth;
                    else if (data[i] == ']') {
                        --depth;
                        if (depth == 0)
                            return data.mid(open, i - open + 1);
                    }
                }
            }
        }
        search = trailerAt + 7;
    }
    return {};
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
    int infoNum = 0; // /Info object from the first trailer; 0 when the writer omitted it
    QByteArray fileId; // /ID array from the original trailer, copied into every update
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
        u.fileId = originalFileId(u.data);
        const int firstStart = u.data.indexOf("startxref");
        if (firstStart > 0) {
            const int firstTrailer = u.data.lastIndexOf("trailer", firstStart);
            if (firstTrailer >= 0) {
                const int info = extractInt(u.data.mid(firstTrailer, firstStart - firstTrailer), "/Info");
                if (info > 0)
                    u.infoNum = info;
            }
        }
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
            + " 0 R";
        if (infoNum > 0)
            appended += " /Info " + QByteArray::number(infoNum) + " 0 R";
        appended += " /Prev " + QByteArray::number(oldStartXref);
        if (!fileId.isEmpty())
            appended += " /ID " + fileId;
        appended += " >>\nstartxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";
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

// One `x y m (x y l)+ h W*? n` clip, the sequence QPdfWriter emits for setClipRect and for
// nothing else we paint. pathStart is the first number; pathEnd is just after the closing n.
struct ClipSpan {
    int pathStart = 0;
    int pathEnd = 0;
};

QVector<ClipSpan> findClipSpans(const QVector<Tok> &toks)
{
    QVector<ClipSpan> out;
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
                        out.push_back({toks[i].start, toks[k].end});
                        i = k + 1;
                        continue;
                    }
                }
            }
        }
        ++i;
    }
    return out;
}

// The q that opens this clip's save(), if it sits immediately before the path.
int clipOwnerStart(const QByteArray &raw, int pathStart)
{
    int k = pathStart - 1;
    while (k >= 0 && (raw[k] == ' ' || raw[k] == '\n' || raw[k] == '\r' || raw[k] == '\t'))
        --k;
    if (k >= 0 && raw[k] == 'q' && (k == 0 || !std::isalpha(static_cast<unsigned char>(raw[k - 1]))))
        return k;
    return pathStart;
}

QByteArray pdfAsciiString(const QString &s)
{
    QByteArray out;
    for (const QChar &c : s) {
        const char ch = c.unicode() < 128 ? char(c.unicode()) : '?';
        if (ch == '(' || ch == ')' || ch == '\\')
            out.append('\\');
        out.append(ch);
    }
    return out;
}

QByteArray bdcNameFor(const PdfTagMarker &m)
{
    switch (m.role) {
    case PdfTagRole::Heading:
        return "/H" + QByteArray::number(qBound(1, m.level, 6));
    case PdfTagRole::TableCell:
        return m.headerCell ? QByteArray("/TH") : QByteArray("/TD");
    case PdfTagRole::Figure:
        return "/Figure";
    case PdfTagRole::Note:
        return "/Note";
    case PdfTagRole::TocItem:
        return "/TOCI";
    case PdfTagRole::ListItem:
        return "/LBody";
    case PdfTagRole::Artifact:
        return "/Artifact";
    case PdfTagRole::Paragraph:
        break;
    }
    return "/P";
}

struct MarkedBit {
    int pageObj = 0;
    int pageIndex = 0;
    int mcid = 0;
    QString lang;
    int ownerElem = -1;
};

struct ClipOut {
    PdfTagMarker marker;
    int pageObj = 0;
    int pageIndex = 0;
    bool matched = false;
    bool hasLabel = false;
    MarkedBit label;
    QVector<MarkedBit> body;
};

QByteArray mcrDict(int pageObj, int mcid)
{
    return "<< /Type /MCR /Pg " + QByteArray::number(pageObj) + " 0 R /MCID " + QByteArray::number(mcid) + " >>";
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

bool pdfPostProcessTagStructure(const QString &path, const QVector<QVector<PdfTagMarker>> &perPageMarkers,
                                 const QString &docLang, const QStringList &readingOrder, QString *error)
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

    const QString catalogLang = docLang.trimmed().isEmpty() ? QStringLiteral("pt-PT") : docLang.trimmed();
    const int structRootNum = u.reserveObj();
    const int docElemNum = u.reserveObj();

    QVector<QVector<ClipOut>> pageClips(pageNums.size());
    QVector<int> fallbackPart(pageNums.size(), -1);
    QVector<QByteArray> pageBodies(pageNums.size());

    for (int pi = 0; pi < pageNums.size(); ++pi) {
        const int pageObjNum = pageNums[pi];
        const QByteArray pageBody = findObjBody(u.data, pageObjNum, &ok);
        if (!ok)
            continue;
        pageBodies[pi] = pageBody;
        const int contentsNum = extractInt(pageBody, "/Contents");
        const QVector<PdfTagMarker> expected = pi < perPageMarkers.size() ? perPageMarkers[pi] : QVector<PdfTagMarker>();

        bool matched = false;
        if (contentsNum > 0 && !expected.isEmpty()) {
            QByteArray raw;
            if (inflateObjStream(u.data, contentsNum, &raw)) {
                const QVector<Tok> toks = tokenize(raw);
                const QVector<ClipSpan> clips = findClipSpans(toks);
                if (clips.size() == expected.size()) {
                    struct Region {
                        int ownerStart = 0;
                        int ownerEnd = 0;
                        int pathEnd = 0;
                    };
                    QVector<Region> regions(clips.size());
                    for (int i = 0; i < clips.size(); ++i) {
                        regions[i].ownerStart = clipOwnerStart(raw, clips[i].pathStart);
                        regions[i].pathEnd = clips[i].pathEnd;
                    }
                    for (int i = 0; i < regions.size(); ++i)
                        regions[i].ownerEnd = (i + 1 < regions.size()) ? regions[i + 1].ownerStart : raw.size();

                    const QVector<QPair<int, int>> allText = findTextSpans(toks);
                    QByteArray tagged;
                    int cursor = 0;
                    auto emitAt = [&](int pos, const QByteArray &s) {
                        if (pos < cursor)
                            pos = cursor;
                        tagged += raw.mid(cursor, pos - cursor);
                        cursor = pos;
                        tagged += s;
                    };

                    QVector<ClipOut> outs;
                    outs.reserve(expected.size());
                    int mcid = 0;
                    bool regionsOk = true;
                    for (int i = 0; i < expected.size(); ++i) {
                        if (regions[i].ownerEnd < regions[i].ownerStart) {
                            regionsOk = false;
                            break;
                        }
                        ClipOut clip;
                        clip.marker = expected[i];
                        clip.pageObj = pageObjNum;
                        clip.pageIndex = pi;
                        const Region &rg = regions[i];
                        QVector<QPair<int, int>> spans;
                        for (const auto &sp : allText) {
                            if (sp.first >= rg.pathEnd && sp.first < rg.ownerEnd)
                                spans.push_back(sp);
                        }

                        auto pushBody = [&](const QString &lang) {
                            MarkedBit bit;
                            bit.pageObj = pageObjNum;
                            bit.pageIndex = pi;
                            bit.mcid = mcid++;
                            bit.lang = lang;
                            clip.body.push_back(bit);
                            return bit.mcid;
                        };

                        if (clip.marker.role == PdfTagRole::Artifact) {
                            emitAt(rg.ownerStart, "/Artifact BMC\n");
                            emitAt(rg.ownerEnd, "\nEMC\n");
                            clip.matched = true;
                            outs.push_back(clip);
                            continue;
                        }

                        const bool wantLabel = clip.marker.listLabel && clip.marker.role == PdfTagRole::ListItem
                            && !spans.isEmpty();
                        int bodyFrom = rg.ownerStart;
                        QVector<QPair<int, int>> bodySpans = spans;
                        if (wantLabel) {
                            const int id = mcid++;
                            emitAt(rg.ownerStart, "/Lbl <</MCID " + QByteArray::number(id) + ">> BDC\n");
                            emitAt(spans[0].second, "\nEMC\n");
                            clip.hasLabel = true;
                            clip.label.pageObj = pageObjNum;
                            clip.label.pageIndex = pi;
                            clip.label.mcid = id;
                            bodyFrom = spans[0].second;
                            bodySpans = spans.mid(1);
                        }

                        const bool split = !clip.marker.pieceLangs.isEmpty()
                            && clip.marker.pieceLangs.size() == bodySpans.size();
                        if (split) {
                            for (int j = 0; j < bodySpans.size(); ++j) {
                                const int from = (j == 0) ? bodyFrom : bodySpans[j].first;
                                const int to = (j + 1 < bodySpans.size()) ? bodySpans[j + 1].first : rg.ownerEnd;
                                const QString lang = clip.marker.pieceLangs[j];
                                const int id = pushBody(lang);
                                QByteArray props = "<</MCID " + QByteArray::number(id);
                                QByteArray name = lang.isEmpty() ? bdcNameFor(clip.marker) : QByteArray("/Span");
                                if (!lang.isEmpty())
                                    props += " /Lang (" + pdfAsciiString(lang) + ")";
                                props += ">>";
                                emitAt(from, name + " " + props + " BDC\n");
                                emitAt(to, "\nEMC\n");
                            }
                        } else {
                            const int id = pushBody(QString());
                            const QByteArray name = clip.marker.role == PdfTagRole::ListItem ? QByteArray("/LBody")
                                                                                              : bdcNameFor(clip.marker);
                            emitAt(bodyFrom, name + " <</MCID " + QByteArray::number(id) + ">> BDC\n");
                            emitAt(rg.ownerEnd, "\nEMC\n");
                        }
                        clip.matched = true;
                        outs.push_back(clip);
                    }
                    if (regionsOk) {
                        tagged += raw.mid(cursor);
                        const QByteArray compressed = flateCompress(tagged);
                        if (!compressed.isEmpty()) {
                            u.redefineObj(contentsNum,
                                          "<< /Length " + QByteArray::number(compressed.size())
                                              + " /Filter /FlateDecode >>\nstream\n" + compressed + "\nendstream");
                            pageClips[pi] = outs;
                            matched = true;
                        }
                    }
                }
            }
        }

        if (!matched)
            fallbackPart[pi] = 0; // filled with an object number after the document element exists

        QByteArray nb = pageBody.trimmed();
        if (nb.endsWith(">>"))
            nb.chop(2);
        nb += " /StructParents " + QByteArray::number(pi) + " >>\n";
        u.redefineObj(pageObjNum, nb);
    }

    QVector<ClipOut> flat;
    for (const auto &clips : pageClips) {
        for (const auto &c : clips) {
            if (c.matched && c.marker.role != PdfTagRole::Artifact)
                flat.push_back(c);
        }
    }

    auto clipsOf = [&](const QString &id) {
        QVector<int> idx;
        for (int i = 0; i < flat.size(); ++i) {
            if (flat[i].marker.blockId == id)
                idx.push_back(i);
        }
        return idx;
    };
    auto roleOf = [&](const QString &id) {
        for (const auto &c : flat) {
            if (c.marker.blockId == id)
                return c.marker.role;
        }
        return PdfTagRole::Paragraph;
    };

    QStringList sequence;
    QStringList seen;
    auto consider = [&](const QString &id) {
        if (id.isEmpty() || seen.contains(id))
            return;
        if (clipsOf(id).isEmpty())
            return;
        seen.push_back(id);
        sequence.push_back(id);
    };
    if (!readingOrder.isEmpty()) {
        for (const auto &id : readingOrder)
            consider(id);
    }
    QVector<int> byDoc;
    for (int i = 0; i < flat.size(); ++i)
        byDoc.push_back(i);
    std::sort(byDoc.begin(), byDoc.end(), [&](int a, int b) {
        if (flat[a].marker.docOrder != flat[b].marker.docOrder)
            return flat[a].marker.docOrder < flat[b].marker.docOrder;
        if (flat[a].pageIndex != flat[b].pageIndex)
            return flat[a].pageIndex < flat[b].pageIndex;
        return a < b;
    });
    for (int i : byDoc)
        consider(flat[i].marker.blockId);

    auto defineElem = [&](int num, const QByteArray &body) { u.redefineObj(num, body); };

    auto writeBits = [&](int parentNum, const QVector<QPair<int, MarkedBit *>> &bits) {
        QByteArray k = "[ ";
        int firstPage = 0;
        for (int i = 0; i < bits.size(); ++i) {
            MarkedBit *bit = bits[i].second;
            if (i == 0)
                firstPage = bit->pageObj;
            if (bit->lang.isEmpty()) {
                k += mcrDict(bit->pageObj, bit->mcid) + " ";
                bit->ownerElem = parentNum;
            } else {
                const int spanNum = u.reserveObj();
                defineElem(spanNum, "<< /Type /StructElem /S /Span /Lang (" + pdfAsciiString(bit->lang) + ") /P "
                                         + QByteArray::number(parentNum) + " 0 R /Pg " + QByteArray::number(bit->pageObj)
                                         + " 0 R /K [ " + mcrDict(bit->pageObj, bit->mcid) + " ] >>");
                bit->ownerElem = spanNum;
                k += QByteArray::number(spanNum) + " 0 R ";
            }
        }
        k += "]";
        return qMakePair(k, firstPage);
    };

    auto bodyBits = [&](const QVector<int> &idx) {
        QVector<QPair<int, MarkedBit *>> bits;
        for (int i : idx) {
            for (auto &b : flat[i].body)
                bits.push_back({i, &b});
        }
        return bits;
    };

    // Indirect objects stay so the parent tree can point at them. /Document /K also
    // carries the same dictionaries inline: qpdf --json=1 does not follow "N 0 R", and the
    // PDF QA reads each child's /S from that array.
    QVector<QByteArray> docKidBodies;

    auto emitSimple = [&](const QString &id) {
        const QVector<int> idx = clipsOf(id);
        const PdfTagMarker &m = flat[idx[0]].marker;
        const int elem = u.reserveObj();
        const auto written = writeBits(elem, bodyBits(idx));
        QByteArray body = "<< /Type /StructElem /S " + bdcNameFor(m) + " /P " + QByteArray::number(docElemNum)
            + " 0 R";
        if (written.second > 0)
            body += " /Pg " + QByteArray::number(written.second) + " 0 R";
        if (m.role == PdfTagRole::Figure && !m.alt.isEmpty())
            body += " /Alt (" + pdfUtf16String(m.alt) + ")";
        if (m.role == PdfTagRole::Note && !m.blockId.isEmpty())
            body += " /ID (" + pdfAsciiString(m.blockId) + ")";
        body += " /K " + written.first + " >>";
        defineElem(elem, body);
        docKidBodies.push_back(body);
    };

    auto emitList = [&](const QStringList &ids) {
        const int listNum = u.reserveObj();
        QByteArray listK = "[ ";
        int firstPage = 0;
        for (const auto &id : ids) {
            const QVector<int> idx = clipsOf(id);
            const int li = u.reserveObj();
            const int lbody = u.reserveObj();
            int lbl = -1;
            QVector<QPair<int, MarkedBit *>> labels;
            for (int i : idx) {
                if (flat[i].hasLabel)
                    labels.push_back({i, &flat[i].label});
            }
            if (!labels.isEmpty()) {
                lbl = u.reserveObj();
                const auto lk = writeBits(lbl, labels);
                defineElem(lbl, "<< /Type /StructElem /S /Lbl /P " + QByteArray::number(li) + " 0 R /Pg "
                                    + QByteArray::number(lk.second) + " 0 R /K " + lk.first + " >>");
                if (firstPage == 0)
                    firstPage = lk.second;
            }
            const auto bk = writeBits(lbody, bodyBits(idx));
            if (firstPage == 0)
                firstPage = bk.second;
            defineElem(lbody, "<< /Type /StructElem /S /LBody /P " + QByteArray::number(li) + " 0 R /Pg "
                                  + QByteArray::number(bk.second) + " 0 R /K " + bk.first + " >>");
            QByteArray liK = "[ ";
            if (lbl >= 0)
                liK += QByteArray::number(lbl) + " 0 R ";
            liK += QByteArray::number(lbody) + " 0 R ]";
            defineElem(li, "<< /Type /StructElem /S /LI /P " + QByteArray::number(listNum) + " 0 R /K " + liK + " >>");
            listK += QByteArray::number(li) + " 0 R ";
        }
        listK += "]";
        QByteArray listBody = "<< /Type /StructElem /S /L /P " + QByteArray::number(docElemNum) + " 0 R";
        if (firstPage > 0)
            listBody += " /Pg " + QByteArray::number(firstPage) + " 0 R";
        listBody += " /K " + listK + " >>";
        defineElem(listNum, listBody);
        docKidBodies.push_back(listBody);
    };

    auto emitTable = [&](const QString &id) {
        const QVector<int> idx = clipsOf(id);
        const int tableNum = u.reserveObj();
        QMap<int, QMap<int, QVector<int>>> grid;
        for (int i : idx)
            grid[flat[i].marker.row][flat[i].marker.col].push_back(i);
        QMap<int, QByteArray> headerIdByCol;
        for (auto rowIt = grid.constBegin(); rowIt != grid.constEnd(); ++rowIt) {
            for (auto colIt = rowIt.value().constBegin(); colIt != rowIt.value().constEnd(); ++colIt) {
                const PdfTagMarker &cell = flat[colIt.value()[0]].marker;
                if (!cell.headerCell)
                    continue;
                headerIdByCol.insert(colIt.key(),
                    pdfAsciiString(cell.blockId + QStringLiteral("-r%1c%2").arg(cell.row).arg(cell.col)));
            }
        }
        QByteArray tableK = "[ ";
        int firstPage = 0;
        for (auto rowIt = grid.constBegin(); rowIt != grid.constEnd(); ++rowIt) {
            const int tr = u.reserveObj();
            QByteArray trK = "[ ";
            for (auto colIt = rowIt.value().constBegin(); colIt != rowIt.value().constEnd(); ++colIt) {
                const QVector<int> cells = colIt.value();
                const int cellNum = u.reserveObj();
                const auto ck = writeBits(cellNum, bodyBits(cells));
                if (firstPage == 0)
                    firstPage = ck.second;
                const PdfTagMarker &cell = flat[cells[0]].marker;
                const QByteArray tag = cell.headerCell ? QByteArray("/TH") : QByteArray("/TD");
                QByteArray attrs;
                if (cell.headerCell) {
                    attrs = " /ID (" + headerIdByCol.value(colIt.key()) + ") /A << /O /Table /Scope /Column >>";
                } else if (headerIdByCol.contains(colIt.key())) {
                    attrs = " /A << /O /Table /Headers [ (" + headerIdByCol.value(colIt.key()) + ") ] >>";
                }
                defineElem(cellNum, "<< /Type /StructElem /S " + tag + " /P " + QByteArray::number(tr) + " 0 R /Pg "
                                        + QByteArray::number(ck.second) + " 0 R" + attrs + " /K " + ck.first + " >>");
                trK += QByteArray::number(cellNum) + " 0 R ";
            }
            trK += "]";
            defineElem(tr, "<< /Type /StructElem /S /TR /P " + QByteArray::number(tableNum) + " 0 R /K " + trK + " >>");
            tableK += QByteArray::number(tr) + " 0 R ";
        }
        tableK += "]";
        QByteArray tableBody = "<< /Type /StructElem /S /Table /P " + QByteArray::number(docElemNum) + " 0 R";
        if (firstPage > 0)
            tableBody += " /Pg " + QByteArray::number(firstPage) + " 0 R";
        tableBody += " /K " + tableK + " >>";
        defineElem(tableNum, tableBody);
        docKidBodies.push_back(tableBody);
    };

    auto emitToc = [&](const QString &id) {
        const QVector<int> idx = clipsOf(id);
        QVector<int> ordered = idx;
        std::sort(ordered.begin(), ordered.end(), [&](int a, int b) {
            if (flat[a].marker.tocEntry != flat[b].marker.tocEntry)
                return flat[a].marker.tocEntry < flat[b].marker.tocEntry;
            return flat[a].pageIndex < flat[b].pageIndex;
        });
        const int tocNum = u.reserveObj();
        QByteArray tocK = "[ ";
        int firstPage = 0;
        int entry = -999999;
        int toci = -1;
        QVector<int> group;
        auto flush = [&]() {
            if (group.isEmpty())
                return;
            toci = u.reserveObj();
            const auto ck = writeBits(toci, bodyBits(group));
            if (firstPage == 0)
                firstPage = ck.second;
            defineElem(toci, "<< /Type /StructElem /S /TOCI /P " + QByteArray::number(tocNum) + " 0 R /Pg "
                                 + QByteArray::number(ck.second) + " 0 R /K " + ck.first + " >>");
            tocK += QByteArray::number(toci) + " 0 R ";
            group.clear();
        };
        for (int i : ordered) {
            if (flat[i].marker.tocEntry != entry) {
                flush();
                entry = flat[i].marker.tocEntry;
            }
            group.push_back(i);
        }
        flush();
        tocK += "]";
        QByteArray tocBody = "<< /Type /StructElem /S /TOC /P " + QByteArray::number(docElemNum) + " 0 R";
        if (firstPage > 0)
            tocBody += " /Pg " + QByteArray::number(firstPage) + " 0 R";
        tocBody += " /K " + tocK + " >>";
        defineElem(tocNum, tocBody);
        docKidBodies.push_back(tocBody);
    };

    for (int i = 0; i < sequence.size();) {
        const QString id = sequence[i];
        const PdfTagRole role = roleOf(id);
        if (role == PdfTagRole::ListItem) {
            QStringList ids;
            while (i < sequence.size() && roleOf(sequence[i]) == PdfTagRole::ListItem)
                ids.push_back(sequence[i++]);
            emitList(ids);
        } else if (role == PdfTagRole::TableCell) {
            emitTable(id);
            ++i;
        } else if (role == PdfTagRole::TocItem) {
            emitToc(id);
            ++i;
        } else {
            emitSimple(id);
            ++i;
        }
    }

    for (int pi = 0; pi < fallbackPart.size(); ++pi) {
        if (fallbackPart[pi] != 0)
            continue;
        const QByteArray partBody = "<< /Type /StructElem /S /Part /P " + QByteArray::number(docElemNum)
            + " 0 R /Pg " + QByteArray::number(pageNums[pi]) + " 0 R >>";
        const int part = u.appendObj(partBody);
        fallbackPart[pi] = part;
        docKidBodies.push_back(partBody);
    }

    {
        QByteArray kids = "[ ";
        for (const QByteArray &body : docKidBodies)
            kids += body + " ";
        kids += "]";
        u.redefineObj(docElemNum, "<< /Type /StructElem /S /Document /P " + QByteArray::number(structRootNum)
                                      + " 0 R /K " + kids + " >>");
    }

    QVector<QPair<int, QVector<int>>> parentTreeEntries;
    for (int pi = 0; pi < pageNums.size(); ++pi) {
        int maxMcid = -1;
        for (const auto &c : pageClips[pi]) {
            if (c.hasLabel)
                maxMcid = qMax(maxMcid, c.label.mcid);
            for (const auto &b : c.body)
                maxMcid = qMax(maxMcid, b.mcid);
        }
        // ownerElem was written into `flat`, which is a copy. Rebuild from flat.
        if (maxMcid < 0)
            continue;
        QVector<int> map(maxMcid + 1, -1);
        for (const auto &c : flat) {
            if (c.pageIndex != pi)
                continue;
            if (c.hasLabel && c.label.ownerElem >= 0 && c.label.mcid >= 0 && c.label.mcid < map.size())
                map[c.label.mcid] = c.label.ownerElem;
            for (const auto &b : c.body) {
                if (b.mcid >= 0 && b.mcid < map.size() && b.ownerElem >= 0)
                    map[b.mcid] = b.ownerElem;
            }
        }
        parentTreeEntries.push_back({pi, map});
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
    newCatalog += " /MarkInfo << /Marked true >> /Lang (" + pdfAsciiString(catalogLang) + ")";
    if (pdfPostProcessPendingDisplayDocTitle())
        newCatalog += " /ViewerPreferences << /DisplayDocTitle true >>";
    newCatalog += " /StructTreeRoot " + QByteArray::number(structRootNum) + " 0 R >>\n";
    u.redefineObj(u.rootNum, newCatalog);

    PdfDocumentInfoMeta meta;
    if (pdfPostProcessPendingDocumentInfo(&meta)) {
        const auto lit = [](const QString &s) { return QByteArray("(") + pdfUtf16String(s) + ")"; };
        QByteArray info = "<<\n/Title " + lit(meta.title) + "\n/Creator " + lit(QStringLiteral("AI Office"))
            + "\n/Producer " + lit(QStringLiteral("AI Office")) + "\n";
        if (!meta.author.isEmpty())
            info += "/Author " + lit(meta.author) + "\n";
        if (!meta.subject.isEmpty())
            info += "/Subject " + lit(meta.subject) + "\n";
        if (!meta.keywords.isEmpty())
            info += "/Keywords " + lit(meta.keywords) + "\n";
        info += "/CreationDate (" + meta.pdfCreationDate.toLatin1() + ")\n/ModDate (" + meta.pdfModDate.toLatin1()
            + ")\n>>";
        if (u.infoNum > 0)
            u.redefineObj(u.infoNum, info);
        else
            u.infoNum = u.appendObj(info);
    }

    return u.finish(path, error);
}

} // namespace aioffice
