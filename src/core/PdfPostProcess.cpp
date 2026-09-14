#include "core/PdfPostProcess.h"

#include <QFile>
#include <QMap>
#include <QtGlobal>
#include <cstring>

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

} // namespace

bool pdfPostProcessAddExtras(const QString &path, const QVector<PdfCommentAnnot> &comments,
                              int physicalPageCount, bool addStructureTree, QString *error)
{
    if (comments.isEmpty() && !addStructureTree)
        return true;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot reopen PDF for post-processing: ") + path;
        return false;
    }
    QByteArray data = f.readAll();
    f.close();

    const int trailerAt = data.lastIndexOf("trailer");
    const int startXrefAt = data.lastIndexOf("startxref");
    if (trailerAt < 0 || startXrefAt < 0) {
        if (error)
            *error = QStringLiteral("PDF has no trailer/startxref (unexpected writer output)");
        return false;
    }
    const QByteArray trailerBody = data.mid(trailerAt, data.size() - trailerAt);
    const int rootNum = extractInt(trailerBody, "/Root");
    const int size = extractInt(trailerBody, "/Size");
    if (rootNum <= 0 || size <= 0) {
        if (error)
            *error = QStringLiteral("PDF trailer missing /Root or /Size");
        return false;
    }

    bool ok = false;
    const QByteArray catalogBody = findObjBody(data, rootNum, &ok);
    if (!ok) {
        if (error)
            *error = QStringLiteral("PDF Catalog object not found");
        return false;
    }
    const int pagesNum = extractInt(catalogBody, "/Pages");
    const QByteArray pagesBody = findObjBody(data, pagesNum, &ok);
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

    int nextObj = qMax(size, 1);
    QMap<int, int> newOffset; // objNum -> byte offset in the final file, QMap keeps keys sorted
    QByteArray appended;
    auto appendObj = [&](int num, const QByteArray &body) {
        newOffset[num] = data.size() + appended.size();
        appended += QByteArray::number(num) + " 0 obj\n" + body + "\nendobj\n";
    };

    // Group comments by physical page, then redefine each page's own (already-allocated, empty)
    // /Annots array object to reference the new annotation objects.
    QMap<int, QVector<int>> annotsByPage;
    for (const auto &c : comments) {
        if (c.physicalPage < 0 || c.physicalPage >= pageNums.size() || c.physicalPage >= physicalPageCount)
            continue;
        const int id = nextObj++;
        const double llx = c.xPt;
        const double ury = c.yPt; // caller passes PDF (bottom-up) space already
        const double urx = llx + 20;
        const double lly = ury - 20;
        const QByteArray body = "<< /Type /Annot /Subtype /Text /Rect [" + pdfNum(llx) + " " + pdfNum(lly) + " "
            + pdfNum(urx) + " " + pdfNum(ury) + "] /Contents (" + pdfUtf16String(c.text) + ") /T ("
            + pdfUtf16String(c.author) + ") /Name /Comment /Open false >>";
        appendObj(id, body);
        annotsByPage[c.physicalPage].push_back(id);
    }
    for (auto it = annotsByPage.constBegin(); it != annotsByPage.constEnd(); ++it) {
        const int pageObjNum = pageNums[it.key()];
        const QByteArray pageBody = findObjBody(data, pageObjNum, &ok);
        if (!ok)
            continue;
        const int annotsObjNum = extractInt(pageBody, "/Annots");
        if (annotsObjNum <= 0)
            continue;
        QByteArray arr = "[ ";
        for (int id : it.value())
            arr += QByteArray::number(id) + " 0 R ";
        arr += "]";
        appendObj(annotsObjNum, arr);
    }

    if (addStructureTree) {
        QVector<int> partNums;
        partNums.reserve(pageNums.size());
        for (int pageObjNum : pageNums) {
            const int pid = nextObj++;
            appendObj(pid, "<< /Type /StructElem /S /Part /Pg " + QByteArray::number(pageObjNum) + " 0 R >>");
            partNums.push_back(pid);
        }
        const int docElemNum = nextObj++;
        QByteArray kids = "[ ";
        for (int pid : partNums)
            kids += QByteArray::number(pid) + " 0 R ";
        kids += "]";
        appendObj(docElemNum, "<< /Type /StructElem /S /Document /K " + kids + " >>");
        const int structRootNum = nextObj++;
        appendObj(structRootNum, "<< /Type /StructTreeRoot /K [ " + QByteArray::number(docElemNum) + " 0 R ] >>");

        QByteArray newCatalog = catalogBody.trimmed();
        if (newCatalog.endsWith(">>"))
            newCatalog.chop(2);
        newCatalog += " /MarkInfo << /Marked true >> /StructTreeRoot " + QByteArray::number(structRootNum)
            + " 0 R >>\n";
        appendObj(rootNum, newCatalog);
    }

    if (newOffset.isEmpty())
        return true; // nothing landed on a valid page / nothing to add

    QByteArray xrefSection = "xref\n";
    for (auto it = newOffset.constBegin(); it != newOffset.constEnd(); ++it) {
        xrefSection += QByteArray::number(it.key()) + " 1\n";
        xrefSection += tenDigit(it.value()) + " 00000 n \n";
    }
    const int xrefOffset = data.size() + appended.size();
    appended += xrefSection;

    const int oldStartXref = parseStartXrefValue(data, startXrefAt);
    const int newSize = qMax(size, nextObj);
    appended += "trailer\n<< /Size " + QByteArray::number(newSize) + " /Root " + QByteArray::number(rootNum)
        + " 0 R /Prev " + QByteArray::number(oldStartXref) + " >>\nstartxref\n"
        + QByteArray::number(xrefOffset) + "\n%%EOF\n";

    data += appended;

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Cannot rewrite PDF: ") + path;
        return false;
    }
    out.write(data);
    return true;
}

} // namespace aioffice
