#pragma once

#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aioffice {

// A comment to render as a real PDF /Annot (Subtype /Text, a sticky note), not a rendered
// extra page. Position is the desired top-left of the icon, top-down, in PDF points (1/72in),
// relative to the page it lands on.
struct PdfCommentAnnot {
    int physicalPage = 0; // 0-based index into the already-written PDF's page sequence
    double xPt = 0;
    double yPt = 0;
    QString author;
    QString text;
};

// Post-processes an already-written QPdfWriter PDF at `path` via a standard PDF incremental
// update (appends new objects + a new xref section + trailer; the original bytes are never
// touched): adds real clickable annotation objects for `comments`, replacing the old "extra page
// listing comments" approach — QPdfWriter always emits an (empty) /Annots array per page, so this
// just redefines that array's object with references to the new annotation objects.
bool pdfPostProcessAddExtras(const QString &path, const QVector<PdfCommentAnnot> &comments,
                              int physicalPageCount, QString *error);

// One clip emitted by paintPage, in the same order as pdfTagMarkersForPage (Preview.cpp).
// Painting wraps each marker in exactly one setClipRect; the post-processor correlates those
// clip paths, in this order, with the content stream. Artifact markers are page furniture
// (background, header, footer, page number, watermark, page border, and the other chrome
// painted in the same passes). Content markers are the taggable elements.
enum class PdfTagRole {
    Artifact,
    Paragraph,
    Heading,
    ListItem,
    TableCell,
    Figure,
    Note,
    TocItem,
};

struct PdfTagMarker {
    PdfTagRole role = PdfTagRole::Artifact;
    QString blockId;
    int docOrder = 0;
    int level = 1;
    int row = -1;
    int col = -1;
    int fragment = 0; // nth laid fragment of blockId on this page
    int tocEntry = -1;
    int textFrom = -1; // display range painted for a TOC entry; -1 = whole fragment
    int textTo = -1;
    bool headerCell = false;
    bool listLabel = false; // first text object inside the clip is /Lbl
    QString alt;
    // Empty: the clip is one marked-content sequence. Otherwise one entry per body text
    // object in paint order (list label not included). An empty string means the document
    // language; any other value is a /Span /Lang.
    QStringList pieceLangs;
    QRectF clip;
    int artifactPass = -1; // 0 background, 1 chrome, 2 overlay; content stays -1
};

// Tagged PDF structure from the marker list above.
//
// QPdfWriter/QPainter cannot emit BDC/EMC while painting. Each marker is one
// `setClipRect`, which this writer turns into an `m ... h W* n` clip. When the clip count
// matches the marker list, artifacts become `/Artifact BMC ... EMC` (not in the tree) and
// each taggable element becomes a real structure element with MCIDs. `/Document` children
// follow `readingOrder` when that list is non-empty, otherwise document order.
// A page whose clips do not match falls back to one `/Part` and is not given a wrong tag.
//
// `docLang` is written as the catalog `/Lang` (BCP 47).
bool pdfPostProcessTagStructure(const QString &path, const QVector<QVector<PdfTagMarker>> &perPageMarkers,
                                 const QString &docLang, const QStringList &readingOrder, QString *error);

// One shaped glyph's Unicode, keyed by the face captured at paint time.
// `faceKey` is the font family with spaces and hyphens removed, lowercased, plus
// "bold" and/or "italic" when that face is bold or italic. `cid` is the subset
// index QPdfWriter writes in `<HHHH> Tj` (0 is .notdef and is never recorded).
struct PdfSubsetUnicode {
    QString faceKey;
    int cid = 0;
    QString unicode;
};

// Fills ToUnicode holes for glyphs the font cmap does not encode (Arabic
// presentation forms and ligatures). Existing bfchar/bfrange entries are kept.
bool pdfPostProcessFixToUnicode(const QString &path, const QVector<PdfSubsetUnicode> &maps, QString *error);

// PDF/UA DisplayDocTitle is applied in post-processing (wave 19 T1). T2 sets the flag before calling
// pdfPostProcessAddExtras when `ua` is requested on export.
void pdfPostProcessSetDisplayDocTitle(bool enable);
bool pdfPostProcessPendingDisplayDocTitle();

// Document-info values written into both the XMP packet and the PDF /Info dictionary.
// Dates are two spellings of the same instant: XMP is ISO-8601, PDF is D:YYYYMMDDHHmmSSOHH'mm'.
struct PdfDocumentInfoMeta {
    QString title;
    QString author;
    QString subject;
    QString keywords;
    QString pdfCreationDate;
    QString pdfModDate;
    QString xmpCreationDate;
    QString xmpModDate;
};

void pdfPostProcessSetDocumentInfo(const PdfDocumentInfoMeta &info);
bool pdfPostProcessPendingDocumentInfo(PdfDocumentInfoMeta *out);

} // namespace aioffice
