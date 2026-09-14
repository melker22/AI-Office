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
// touched): adds real clickable annotation objects for `comments`, replacing the old "extra page
// listing comments" approach — QPdfWriter always emits an (empty) /Annots array per page, so this
// just redefines that array's object with references to the new annotation objects.
bool pdfPostProcessAddExtras(const QString &path, const QVector<PdfCommentAnnot> &comments,
                              int physicalPageCount, QString *error);

// One text-bearing block (paragraph/heading/list-item) expected on a page, in paint order.
// structTag is a PDF structure type: "P" for body text, "H1".."H6" for headings.
struct PdfTaggedBlock {
    QString structTag;
};

// Post-processes an already-written PDF to add real, paragraph/heading-granularity tagged-PDF
// structure: /MarkInfo <</Marked true>>, a /StructTreeRoot whose leaf elements carry real
// /K [mcid...] marked-content references (not just a bare per-page /Part placeholder), and
// /StructParents + a /ParentTree so readers can resolve MCID -> structure element.
//
// QPdfWriter/QPainter expose no API to inject BDC/EMC+MCID marked-content operators while
// painting, so this recovers block boundaries from the *already-written* content stream: every
// text-bearing block is painted through exactly one `p->setClipRect(lb.rect...)` call
// (Preview.cpp's paintPage), which QPdfWriter's PDF engine renders as a distinctive
// `x y m ... l ... l ... l h W* n` clip-path sequence found nowhere else in our output (tables,
// images, shapes all use different operators — verified empirically against real generated
// output before relying on it). Each `BT...ET` text object between one such clip-path and the
// next belongs to exactly one block, in the same order Preview.cpp painted them. This is decoded,
// each BT/ET is wrapped in `/<Tag> <</MCID n>> BDC ... EMC`, the content stream is recompressed,
// and the page's Contents object is redefined via the same incremental-update mechanism.
//
// Per page, this only proceeds if the clip-pattern count found in the real content stream exactly
// matches perPageBlocks[page].size() — if anything doesn't line up (an unexpected block type, a
// future rendering change, ...), that page silently falls back to a bare page-level /Part element
// instead of risking a wrong/misaligned tag. Never produces a wrong tag; at worst, a coarser one.
//
// Scope: only paragraph/heading/list-item body text is covered (the common case). Tables, images,
// shapes, footnotes/endnotes, and TOC entries are not yet tagged at this granularity — tracked as
// a follow-up, not silently dropped (see docs/WRITER_FEATURE_CATALOG.md).
bool pdfPostProcessTagStructure(const QString &path, const QVector<QVector<PdfTaggedBlock>> &perPageBlocks,
                                 QString *error);

} // namespace aioffice
