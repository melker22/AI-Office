#pragma once

#include <QString>
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
// touched), adding:
//  - real clickable annotation objects for `comments`, replacing the old "extra page listing
//    comments" approach — QPdfWriter always emits an (empty) /Annots array per page, so this
//    just redefines that array's object with references to the new annotation objects.
//  - `addStructureTree`: /MarkInfo <</Marked true>> + a /StructTreeRoot with one /Part structure
//    element per page. This is page-granularity tagging, not full PDF/UA per-paragraph tagging:
//    QPdfWriter/QPainter give no way to inject marked-content (BDC/EMC + MCID) operators into the
//    content streams it generates, so structure elements here carry no /K content reference —
//    real per-element tagging would need either Qt engine support that does not exist as of Qt
//    6.11, or a from-scratch content-stream generator replacing QPdfWriter. Documented honestly
//    in docs/WRITER_FEATURE_CATALOG.md; this is the real, valid, spec-conformant subset that is
//    achievable via post-processing alone.
// `physicalPageCount` is the total number of pages already written (for bounds-checking).
bool pdfPostProcessAddExtras(const QString &path, const QVector<PdfCommentAnnot> &comments,
                              int physicalPageCount, bool addStructureTree, QString *error);

} // namespace aioffice
