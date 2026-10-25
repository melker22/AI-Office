# Module Sweep Audit — Grok 4.7 First Pass, Tech Lead Confirmation

Tech-lead confirmation pass over a module-by-module audit produced by Grok 4.7. For every
finding the code was re-read at the cited file/line and the described scenario was traced
through the current source. Verdicts:

- **CONFIRMED** — the scenario breaks as described. Where cheap and safe, it is proven with an
  executable playbook under `qa/audit/*.json` (run with `./build/linux/aioffice-cli play <file>`
  after `scripts/qa-all.sh`'s build step); otherwise the verdict rests on an unambiguous read of
  the code (a missing bound/flag check, a discarded return value, a hard-coded wrong constant) —
  reproduction would need a dangerous payload (zip bomb, OOM-sized allocation, a `/dev/zero`-style
  infinite read) that is deliberately **not** run in this sandbox, or a deep multi-format
  round-trip that is traced by hand instead.
- **PLAUSIBLE** — not used in this pass; every finding below was either resolved by direct,
  unambiguous code reading or disproved outright. See the one REJECTED item for the only case
  where the code contradicted the scenario.
- **REJECTED** — the scenario does not reproduce against current code; reason given inline.

None of the 8 `qa/audit/*.json` playbooks are wired into CTest — they demonstrate bugs that are
still open and would fail a correctness assertion today, so they stay out of the green suite by
design.

**Totals: 101 findings reviewed → 100 CONFIRMED, 0 PLAUSIBLE, 1 REJECTED.**

Cross-reference with `docs/audit/coderabbit-history.md` (Frente A, "Still VALID"): one item
below (A-document-6, `FieldExpr.cpp:595`) is related to CR-10-114 (`refreshIfFields` computes
`statPages` before the structure-field pass) but is a **distinct** defect — CR-10-114 is an
ordering/staleness bug for `stat{pages}`; A-document-6 is that `if` fields never receive the
real page count at all, regardless of ordering, because the `pagesArg` ternary only fires for
`stat`-type runs. Not a duplicate. No other item below overlaps a CR-*-* entry from that file.

---

## Top findings, CONFIRMED (+ PLAUSIBLE — none), ordered by severity

### Critical (12)

| ID | File:line | Title | Repro |
|----|-----------|-------|-------|
| A-session-1 | `src/core/Session.cpp:1262` | undo/redo swap the document but keep the new file path and force `dirty_=true` | `qa/audit/A-session-1-undo-path.json` (executed) |
| A-session-8 | `src/core/Session.cpp:5141` | `document_include` copies blocks but drops `revisions`/`bookmarks`, deleting text that only existed in a delete-revision | `qa/audit/A-session-8-document-include-drops-revisions.json` (executed) |
| A-document-1 | `src/core/Document.cpp:531` | `Block::toJson` writes `caption`/`caption_runs` only for `type==image`; tables/other captioned blocks lose the caption immediately, even in `document_get` | `qa/audit/A-document-1-table-caption-lost.json` (executed) |
| A-docx-1 | `src/core/Zip.cpp:50` | `inflateRaw` allocates the declared uncompressed size (or doubles unbounded) with no cap | code read only — zip-bomb repro deliberately not run (OOM risk) |
| A-docx-2 | `src/core/Docx.cpp:1980` | `loadDocx` never checks `xml.hasError()`; a truncated `document.xml` opens as `ok:true` with silently-dropped tail content | code read (`hasError` has zero occurrences in `Docx.cpp`) |
| A-docx-3 | `src/core/Docx.cpp:1349` | `saveDocx` discards `QFile::write`'s return value and always returns `true` | `qa/audit/A-{docx-3,odf-9,rtf-3}-write-failure-ignored-{docx,odt,rtf}.json` (executed via `/dev/full` symlink) |
| A-odf-9 | `src/core/Odf.cpp:967` | `saveOdt` has the identical discarded-write-result bug | its own playbook, same pattern as A-docx-3 (executed) |
| A-epub-1 | `src/core/Epub.cpp:567` | EPUB image read has no cap when `QFile::size()` reports 0 (device/FIFO); `readAll()` can hang/OOM forever | code read only — `/dev/zero`-style repro deliberately not run (hang risk) |
| A-mcp-1 | `src/cli/Cli.cpp:146` | `dispatch()` treats an IPC transport failure as license to run the tool on a brand-new blank headless `Session` instead of surfacing the error | code read (`dispatch`'s `err.isEmpty() \|\| r.ok` short-circuit) |
| A-mcp-2 | `src/mcp/McpServer.cpp:185` | `tools/call` falls back to the private headless `Session` on any IPC timeout, silently editing/reading the wrong (blank) document | code read; `AIOFFICE_HEADLESS` has zero occurrences in `McpServer.cpp` |
| A-mcp-3 | `src/lok/LokEngine.cpp:98` | `convertTo` writes LibreOffice's output into `--outdir` named by the **input**'s basename, clobbering any same-named sibling file before the final rename | code read (soffice naming + unconditional overwrite) |
| A-app-1 | `src/app/qml/TitleBar.qml:90` / `Main.qml` | No `onClosing` guard anywhere in QML; closing the window discards dirty documents with no prompt | code read (`onClosing` has zero occurrences in `src/app/qml`) |

### Major (81) and Minor (7)

All remaining 88 findings (every module below except the one REJECTED item) are **CONFIRMED**
at **major** or **minor** severity as originally graded by the audit — no severity adjustments
were needed. They are listed with full evidence in the per-module tables below; the ones with an
executable repro are: A-session-2, A-session-3, A-session-6, A-session-7 (`qa/audit/A-session-*.json`)
and A-rtf-3 (same `/dev/full` playbook as A-docx-3 / A-odf-9).

---

## Per-module detail

### `src/core/Session.cpp` — 8 findings, 8 CONFIRMED

Session's undo stack stores only the `Document`, not the path/dirty bit that belongs with it, so
undo-after-open silently repoints a save at the wrong file. `batch` allows `document_save` inside
it, so a failed batch rolls memory back but a save that already hit disk stays, with `dirty`
left lying. Revision ids are `size+1`, which collides after any accept/reject. `split_block` and
`find_replace` never touch `doc_.revisions`, so `revision_reject` silently corrupts or no-ops
instead of failing. `set_selection` builds its result before `markChanged()`, so the call reports
`dirty:false` while actually dirtying the session. `insert_table` skips the 200×64 cap that
`insert_blocks`' shorthand enforces. `document_include` drops `revisions`/`bookmarks` entirely.

| ID | File:line | Severity | Category | Title | Verdict | Evidence |
|----|-----------|----------|----------|-------|---------|----------|
| A-session-1 | Session.cpp:1262 | critical | undo-path | undo/redo keep the new file path and force dirty | CONFIRMED | Executed `qa/audit/A-session-1-undo-path.json`: after `document_open a` → `document_open b` → `undo`, `document_get` shows body text "AAA" (from `a`) but `path:"/tmp/aio-audit/b.aio.json"` and `dirty:true`; the trailing `document_save` (no path) then overwrites `b.aio.json` with "AAA" on disk, permanently losing "BBB" from storage. |
| A-session-2 | Session.cpp:5308 | major | batch-rollback | failed batch leaves a `document_save` on disk, reports clean | CONFIRMED | Executed `qa/audit/A-session-2-batch-save-rollback.json`: batch does `insert "Y"` → `document_save` (writes "v1Y" to disk) → a call that fails. Batch returns `ok:false` and rolls `doc_`/`dirty_` back; `document_get` after shows text "v1", `dirty:false`. Reading `/tmp/aio-audit/batch.aio.json` from disk shows `"v1"+"Y"` — the save was never undone. `document_save`/`document_new`/etc. are not in batch's disallowed-tool list (only `batch`/`undo`/`redo`/`checkpoint`/`restore` are), and `baseline_` is not part of the batch snapshot either. |
| A-session-3 | Session.cpp:1784 (+768,1771,1820,1865) | major | revision-id | revision ids are `size+1`, collide after accept/reject | CONFIRMED | Executed `qa/audit/A-session-3-revision-id-collision.json`: insert "A","B" → `rev1`,`rev2`. `revision_accept rev1` leaves one revision. Insert "C" → `revision_list` shows **two** revisions both with `id:"rev2"` (one covering "B", one covering "C"). |
| A-session-4 | Session.cpp:1899 (`splitBlock`) / 1927 scenario | major | revision-offset | `splitBlock`/`joinBlocks`/backspace-join never touch `doc_.revisions`; `revision_reject` then clamps and corrupts | CONFIRMED | Code read: `splitBlock` (Session.cpp:1899-1938) builds `rest` and reassigns `b.runs`/`rest.runs` via `splitRuns` but never calls any `shiftRevisionOffsets`/retarget on `doc_.revisions`. `spliceRuns` (Document.cpp:1828-1842) clamps `end` to `qBound(0,end,plain.size())`, so `revisionReject` on a now-4-char block with `found.end==10` silently deletes only the remaining 4 chars and returns `ok:true` — matches the scenario exactly (not executed: needs a multi-call sequence across split/join/backspace variants to show all three holes; the core mechanism is unambiguous from the two functions read). |
| A-session-5 | Session.cpp:1219 (`findReplace`) | major | revision-offset | `find_replace` rewrites run text, never calls `shiftRevisionOffsets` | CONFIRMED | Code read: `findReplace` (Session.cpp:1175-1254) mutates `r.text` directly in both regex and plain branches; the only `shiftRevisionOffsets` call sites in the file are inside `insertAtCaret`/`deleteBackward`/`revisionReject`/`revisionRejectAll` (lines 1764,1816,1863,3069,3101) — none inside `findReplace`. Combined with the `spliceRuns` clamp from A-session-4, a reject after `find_replace` shrinks the block silently reports `ok:true`. |
| A-session-6 | Session.cpp:1736 (`setSelectionTool`) | major | dirty-flag | `set_selection` marks dirty but its own result says `dirty:false` | CONFIRMED | Executed `qa/audit/A-session-6-set-selection-dirty.json`: `set_selection`'s own result has `dirty:false` (built by `ok()` before `markChanged()`); the following `document_get` shows `dirty:true`. `ok()` (Session.cpp:96-104) reads `dirty_` into the JSON before `markChanged()` (line 121-126) flips it, and the `dirty_ = dirty_;` self-assignment in `setSelectionTool` is a no-op. |
| A-session-7 | Session.cpp:1049 (`insertTable`) | major | hostile-input | `insert_table` has no rows/cols cap, unlike `insert_blocks`' table shorthand | CONFIRMED | Executed `qa/audit/A-session-7-insert-table-no-cap.json`: `insert_blocks {type:table, rows:300, cols:64}` is correctly rejected (`"table rows/cols out of range"`, cap is 200×64 — `validateTableShorthand`, Document.cpp:413-436) but `insert_table {rows:300, cols:64}` succeeds and builds the full 300×64 (19,200-cell) grid. `nestTable` (Session.cpp:3309) has the identical uncapped loop (not independently executed; same code pattern). |
| A-session-8 | Session.cpp:5141 (`documentInclude`) | critical | include-data-loss | `document_include` drops `revisions`/`bookmarks` | CONFIRMED | Executed `qa/audit/A-session-8-document-include-drops-revisions.json`: hand-built `qa/audit/fixtures/include-source.aio.json` with a delete-revision (`deleted_runs:[{text:"B"}]`) and a bookmark on block `p9`. After `document_include`, `document_get` shows the included paragraph's `runs` as plain `"AC"` and **both `revisions` and `bookmarks` keys are absent** from the result — the "B" that only existed as a recoverable delete-revision is gone for good. |

### `src/core/Preview.cpp` + `Hyphenation.cpp` — 7 findings, 7 CONFIRMED

Every one of these is the same root shape: `blockTextLayout`/`paintPage` build or measure one
string (with a synthetic chapter/footnote prefix, or hyphenation, or a shaped `QTextLine`) and
then format/paint/hit-test a *different* string or coordinate space, so the two drift apart.

| ID | File:line | Severity | Category | Title | Verdict | Evidence |
|----|-----------|----------|----------|-------|---------|----------|
| A-preview-1 | Preview.cpp:1367 | major | layout | chapter/footnote numbers prepended only while measuring/painting `raw`, but the per-run `QTextLayout::FormatRange`s (`pos` loop, Preview.cpp:1453-1497) start at `pos=0` with no offset for the synthetic prefix length | CONFIRMED | Code read: `prependSynthetic` (called at 1366,1369 when `in.paint`) is folded into `raw`→`text` *before* the layout is built, but the format-range loop's `pos`/`skip` accounting (1455-1466) only subtracts drop-cap `skip`, never the synthetic prefix — ranges are shifted left by exactly the prefix length. |
| A-preview-2 | Preview.cpp:1392 | major | hyphenation | the laid-out string is hyphenated once with the **block** language (`doc.page.hyphenate` branch, line 1388-1389: `hyphenateDisplay(raw, ..., b.lang)`), but the per-run format-range loop hyphenates again with each **run's own** language (line 1468: `r.lang.isEmpty()?b.lang:r.lang`) | CONFIRMED | Both call sites read directly; different lang arguments to the same hyphenation function on text that may differ per run guarantees a length/position mismatch between the painted string and its format ranges whenever a run's `lang` differs from the block's. |
| A-preview-3 | Preview.cpp:1459 | major | fields | `note_restart`'s per-page ordinal substitution happens only in the format-range loop (`disp = QString::number(noteOrds.value(r.noteId))`, line 1459-1460), never in the first loop that builds the actual `raw`/`text` painted by `QTextLayout` (1346-1358, which calls `liveDisplayText`/`runDisplayText` with no ordinal awareness) | CONFIRMED | The superscript marker's *painted* text is built in the first loop and never gets the per-page ordinal; only the *format range length* accounts for it, producing the length-mismatch bug described (bold/color drifting by one char when marker widths differ). |
| A-preview-4 | Preview.cpp:2635 | major | drawing | TOC/`paintSlice==1` text is drawn via `pdfDrawText(p, pos, btl.display.mid(a,bend-a))` using the block's first-run font/pen, not `QTextLine::draw` with the shaped per-run format ranges | CONFIRMED | Preview.cpp:2621-2636: when `m.textFrom>=0`, the code sets `p->setFont(f)`/`p->setPen(col)` (block-level) and calls `pdfDrawText` on a raw substring — any `QTextLayout::setFormats` ranges (bold/italic/underline/color/link) on that text are never consulted by this path. |
| A-preview-5 | Preview.cpp:2582 | major | selection | selection highlight is one proportional rectangle (`x0 = left + markerWidth + selStart/n * width`), not per-line `cursorToX` | CONFIRMED | Preview.cpp:2582-2587: the rect math is a straight linear interpolation over the whole fragment width based on `selStart/selEnd` as a fraction of `t.size()`, independent of how the text actually wraps across `QTextLine`s. |
| A-preview-6 | Preview.cpp:2352 | major | hidden | table cells are painted by concatenating `cellp->runs` text directly with a fixed font/color (Liberation Serif 10pt, `#1A1F24`), bypassing `blockTextLayout` | CONFIRMED | Preview.cpp:2351-2370 (`paintTableCellMarker`'s plain-cell branch): `for (const auto &r : cellp->runs) t += r.text;` then one fixed `QFont`/pen draws `t` — `hidden`, `show_if`, `redact`, and per-run color/size/font are never consulted, unlike the paragraph path which goes through `blockTextLayout`→`runDisplayText`. |
| A-preview-7 | Preview.cpp:2656 | major | hit-test | `gutterMirror` shifts the *painter* for even pages (`p->translate(-gutterMm,0)`, Preview.cpp:2656-2657) but `DocumentLayout::hitTest` (962) and `caretGeometry`/`hitTestTool` (Session.cpp:1357,1392) never apply the same shift | CONFIRMED | `gutterMirror`/`gutterMm` appear exactly twice in `Preview.cpp`, both inside `paintPage`'s painter translate; zero references anywhere in `hitTest` or the caret/hit-test Session tools. |

### `src/core/Document.cpp` + `FieldExpr.cpp` — 7 findings, 7 CONFIRMED

`Block::toJson` only persists `caption`/`caption_runs` for images even though `fromJson` reads
them unconditionally for any block type — an asymmetric save/load bug that loses table/shape
captions on the very first `document_get`, not just after a save/reload. Header/footer "active"
checks are inconsistent between the primary pair and the even/first variants. `plainText()` never
reads captions, so stats/diff never see caption edits. `forEachFieldRun` skips `cell.nested`.
`refreshIfFields`'s `if`-field evaluation only runs once per dirty refresh while stat fields
iterate to a fixed point, so an `if` whose own branch text changes total word count oscillates
across successive `document_get` calls. The same function never passes the real `statPages` to
`if`-field evaluation at all (distinct from, and additional to, CR-10-114's ordering issue for
`stat{pages}`). `validateDocumentBody` silently skips non-object body items instead of rejecting
the file, letting `Document::fromJson` assign a colliding id.

| ID | File:line | Severity | Category | Title | Verdict | Evidence |
|----|-----------|----------|----------|-------|---------|----------|
| A-document-1 | Document.cpp:531 | critical | roundtrip | table caption lost immediately, not just on save | CONFIRMED | Executed `qa/audit/A-document-1-table-caption-lost.json`: `insert_table` → `insert_caption {id:"t1", text:"Vendas"}` returns `ok:true`, but the **very next** `document_get` already shows the table block with no `caption`/`caption_runs` keys at all — confirms `Block::toJson` (Document.cpp:500-543) emits those fields only inside the `type==image` branch, and `document_get` serializes through the same `toJson()`. After save+reopen the loss is identical. |
| A-document-2 | Document.cpp:1119 | major | roundtrip | header/footer with only `chapter`/`imagePath` set is never written | CONFIRMED | Document.cpp:1118-1129: main `header`/`footer` use `if (!header.text.isEmpty() || header.pageNumbers)`; `headerEven`/`footerEven`/`headerFirst`/`footerFirst` use `.active()` (Document.h:237: `pageNumbers \|\| chapter \|\| !text.isEmpty() \|\| !imagePath.isEmpty()`). The primary pair's condition is missing `chapter`/`imagePath` entirely. |
| A-document-3 | Document.cpp:816 | major | stats | `plainText()` ignores caption, so word count/diff never see it | CONFIRMED | `Block::plainText()` (Document.cpp:816-839): the non-table path is `for (r: runs) t+=r.text;` — no reference to `caption`/`captionRuns` anywhere in the function. `visibleWordCount`/`documentDiff` consume blocks via this same function. |
| A-document-4 | Document.cpp:1575 (actually `forEachFieldRun`, ~1563) | major | fields | `seq` inside a nested table cell is skipped by numbering | CONFIRMED | `forEachFieldRun` (Document.cpp:1563-1586) walks `block.runs`, `block.captionRuns`, and `row.cell.runs` for `type==table`, but never descends into `cell.nested` — confirmed by the complete absence of a `nested` traversal in the function. |
| A-document-5 | FieldExpr.cpp:626 | major | fields | an `if` field whose own text changes word count oscillates every dirty `document_get` | CONFIRMED | `refreshIfFields` (FieldExpr.cpp:577-630): `eachRun(false)` (the `if`/`seq` pass) runs exactly once per call, before the `for (pass<6) eachRun(true)` fixed-point loop that only touches `stat`-type runs. An `if` field's own branch text feeds into the stat word count that a later call's single `eachRun(false)` reads, so each dirty refresh can flip the branch the *opposite* way from the last. |
| A-document-6 | FieldExpr.cpp:595 | major | fields | `if` fields never receive the real page count, only `1` | CONFIRMED (related to, not a duplicate of, CR-10-114) | `const int pagesArg = stat && r.fieldArg==="pages" ? statPages : 1;` (FieldExpr.cpp:595) only yields `statPages` when `stat` is true; `if`/`seq`/`category_number` runs go through the `stats=false` branch where `stat` is always false, so `pagesArg` is unconditionally `1` regardless of the real layout — not an ordering bug like CR-10-114, a complete absence of plumbing. |
| A-document-7 | Document.cpp:445 | major | hostile-input | non-object body items are skipped, not rejected, letting `fromJson` assign colliding ids | CONFIRMED | `validateDocumentBody` (Document.cpp:439-452): `if (!v.isObject()) continue;` — accepts the file. `Document::fromJson` later does `toObject()` on the `null`/non-object value (empty object) and assigns a fresh id via `assignId`, which can collide with a real block's explicit id read right after. |

### `src/core/Docx.cpp` + `Zip.cpp` + `Xml.cpp` — 12 findings, 12 CONFIRMED

Two file-format-level crashes (unbounded zip inflate, no `hasError()` check) and a silently
truncated/failed save sit alongside a long tail of round-trip losses: run color/size/font never
read back, `w:b/i/u/strike`/`w:bidi val="0"`-only-off handling, text dropped after an inline
drawing, page size collapsed to A4/Letter, nested tables wiping the outer table, exported tables
missing `gridSpan`/nested/caption, `docProps/core.xml` never read, a tracked-delete field
resurrected as live text, and numbered lists hard-coded to `numId` 1/2 while `numbering.xml` is
ignored entirely.

| ID | File:line | Severity | Category | Title | Verdict | Evidence |
|----|-----------|----------|----------|-------|---------|----------|
| A-docx-1 | Zip.cpp:50 | critical | hostile-input | `inflateRaw` has no cap on the declared or doubling buffer size | CONFIRMED | `inflateRaw` (Zip.cpp:38-64): `out.resize(uncompressed ? uncompressed : in.size()*4+64)` then `out.resize(out.size()*2)` in the `Z_BUF_ERROR`/`Z_OK` retry loop — no upper bound anywhere, no `try/catch` around the resize. Not executed (a 2^31-sized or doubling-to-exhaustion payload risks OOM-killing the sandbox). |
| A-docx-2 | Docx.cpp:1980 | critical | error-path | `loadDocx` never checks `xml.hasError()` | CONFIRMED | `grep -c hasError src/core/Docx.cpp` → 0. The parse loop's exit condition is only `xml.atEnd()`; `return true` at the end is unconditional, and `if (doc.body.isEmpty()) doc = Document::blank();` only fires when *nothing* parsed, not when parsing stopped early on an error. |
| A-docx-3 | Docx.cpp:1349 | critical | error-path | `saveDocx` discards `f.write()`'s result | CONFIRMED | Executed `qa/audit/A-{docx-3,odf-9,rtf-3}-write-failure-ignored-{docx,odt,rtf}.json` against `/tmp/aio-audit/fail.docx` symlinked to `/dev/full`: `document_save` returns `{"ok":true,"dirty":false,"path":".../fail.docx"}` even though every byte write to `/dev/full` fails. Code: `f.write(Zip::pack(entries)); return true;` (Docx.cpp:1347-1348) — return value never checked. |
| A-docx-4 | Docx.cpp:1697 | major | roundtrip | import never reads `w:sz`/`w:rFonts`/`w:color`; `b/i/u/strike` ignore `w:val`; `bidi` treats anything but literal `"0"` as RTL | CONFIRMED | `grep -n '"rFonts"\|"sz"\|"color"' Docx.cpp` → 0 hits outside export. The `b`/`i`/`u`/`strike` branch (1690-1705) sets the flag unconditionally on element presence. `bidi` (1653-1656): `v.isEmpty() \|\| v != "0"` → `"false"` is treated as RTL since it isn't the literal string `"0"`. |
| A-docx-5 | Docx.cpp:1959 | major | roundtrip | paragraph text is dropped when an inline drawing follows it | CONFIRMED | On `drawing` end (Docx.cpp:1947-1959): `blipRid.clear(); inP=false; runs.clear();` — `runs` (the accumulated text before the drawing) is cleared without ever calling `flushP()`, and `inP=false` means the subsequent `</w:p>`'s `if (inP) flushP();` is skipped too. |
| A-docx-6 | Docx.cpp:1434 | major | roundtrip | `pgSz` import collapses every size to Letter or A4, never storing real mm | CONFIRMED | Docx.cpp:1425-1437: only compares the short side to 215.9mm within 2mm; sets `doc.page.size` to `"Letter"`/`"A4"` and never touches `widthMm`/`heightMm`. |
| A-docx-7 | Docx.cpp:1622 | major | roundtrip | nested `w:tbl` resets `tableBlock`/`tableRow`, discarding the outer table | CONFIRMED | Single `inTbl`/`tableBlock`/`tableRow` (no stack) confirmed by reading the parse loop (Docx.cpp:1618-1645): `n=="tbl"` always does `tableBlock = Block{}`; `n=="tr"` always does `tableRow.clear()` — a second nested `<w:tbl>` zeroes the first. |
| A-docx-8 | Docx.cpp:1078 | major | roundtrip | table export omits `w:gridSpan`, nested `w:tbl`, and the caption paragraph | CONFIRMED | Docx.cpp:1068-1076: the table branch emits one `<w:tc><w:p>runs</w:p></w:tc>` per cell with no `gridSpan`, no `cell.nested` handling, and only `accountSeqRuns(o,b.captionRuns)` (counter bump, not emission) for the caption. |
| A-docx-9 | Docx.cpp:1370 | major | roundtrip | `docProps/core.xml` (`dc:title`/`subject`/`keywords`/`creator`) is written but never read | CONFIRMED | `grep -n "docProps/core.xml" Docx.cpp` → only the export-side entries (1215,1251,1339); `loadDocx` never opens that part. |
| A-docx-10 | Docx.cpp:1815 | major | revision | a field inside `w:del` is exported as a live `w:fldSimple`/`w:t` and re-imported as a live run | CONFIRMED | Export (`runsToWml`, Docx.cpp:756-761): the `nativeWordField` branch always emits `<w:fldSimple>...<w:t>`, with no `asDeleted`-aware `w:delText` path. Import (Docx.cpp:1744-1816): `fldSimple` handling ends with `runs.push_back(fr); ++pushedRuns;` unconditionally — no check of `inDel`, so a field found inside `<w:del>` is pushed to the live `runs`, not `delAcc`. |
| A-docx-11 | Docx.cpp:1552 | major | roundtrip | lists only exist for hard-coded `numId` 1/2; `numbering.xml` is never parsed | CONFIRMED | Docx.cpp:1551-1553: `if (numId==1\|\|numId==2)` is the only list detection; `grep -n "numbering.xml\|numFmt" Docx.cpp` shows these strings only on the export side. |
| A-docx-12 | Xml.cpp:26 | minor | roundtrip | `xmlEscape` passes C0 control chars through verbatim; `w:color` bypasses escaping entirely | CONFIRMED | `xmlEscape` (Xml.cpp:5-30): `default: o += c;` — no handling of U+000B/U+0001/etc. `runRPr` (Docx.cpp:125-129): `c.remove('#'); rPr += QStringLiteral("<w:color w:val=\"%1\"/>").arg(c);` — `c` (the color string) is never passed through `xmlEscape`, unlike the adjacent font-name line which does use it. |

### `src/core/Odf.cpp` + `Csl.cpp` + `Qr.cpp` + `Spell.cpp` — 10 findings, 10 CONFIRMED

Same shape as the DOCX module: a crash-class defect (unbounded `text:s text:c`), a
write-failure-ignored save, a truncated-XML-opens-as-success bug, and a tail of round-trip
losses (note citation text leaking into the body, page size collapse, font never read back,
rows-spanned never emitted, every list defaulting to decimal, shape/chart/qr/form blocks
silently downgraded to plain paragraphs). `encodeQr` does not produce a decodable symbol, though
one of the three claimed causes turned out to be a false alarm. No defect was found in
`Csl.cpp`/`Csl.h`/`Spell.cpp`/`Spell.h` — confirmed by direct reading, matching the original
audit's own conclusion that the MLA in-text-citation "mismatch" there is a documented design
choice (`BiblioEntry.pages` is a publication range, not a citation locator), not a bug.

| ID | File:line | Severity | Category | Title | Verdict | Evidence |
|----|-----------|----------|----------|-------|---------|----------|
| A-odf-1 | Odf.cpp:1404 | major | roundtrip | note import concatenates `text:note-citation` into the note body | CONFIRMED | Odf.cpp:1393-1418: the `note` branch collects `xml.text()` for every `isCharacters()` event between `<text:note>` and `</text:note>`, with no distinction between `text:note-citation` and `text:note-body` children. |
| A-odf-2 | Odf.cpp:1740 | major | error-path | truncated `content.xml` opens as success once any block parsed | CONFIRMED | Odf.cpp:1739-1743: `if (xml.hasError() && doc.body.isEmpty())` — only fails when body is *also* empty; one successfully-parsed paragraph before the error makes this return `true`. |
| A-odf-3 | Odf.cpp:1430 | major | hostile-input | `text:s text:c` has no cap, can allocate ~1GB per element | CONFIRMED | Odf.cpp:1428-1432: `int count = ...toInt(); if (count<=0) count=1; spanRun.text += QString(count,' ');` — no upper bound. Not executed (allocation-exhaustion risk). |
| A-odf-4 | Odf.cpp:1160 | major | roundtrip | ODT page size collapses Legal/A3/custom sizes to Letter/A4, no mm stored | CONFIRMED | Odf.cpp:1152-1163: `qAbs(ww-215.9)<2 \|\| qAbs(hh-215.9)<2` picks `"Letter"` else `"A4"`; `widthMm`/`heightMm` are never set. |
| A-odf-5 | Odf.cpp:971 | major | roundtrip | run font (`style:font-name`) is written but never read back | CONFIRMED | `applyTextProps` (Odf.cpp:971-996) reads weight/style/underline/strike/position/color/size/background — no `style:font-name` anywhere in the function, despite the export side writing it (Odf.cpp:502). |
| A-odf-6 | Odf.cpp:1717 | major | roundtrip | merged cells don't survive a round trip (`rows-spanned` never emitted, neither span read) | CONFIRMED | Export only writes `table:number-columns-spanned` (Odf.cpp:797-798), never `table:number-rows-spanned`; `grep -n "number-columns-spanned\|number-rows-spanned" Odf.cpp` shows zero import-side occurrences. |
| A-odf-7 | Odf.cpp:1325 | major | roundtrip | every `text:list` becomes `listKind:"decimal"` regardless of style | CONFIRMED | Odf.cpp:1323-1326: `if (n=="list") { ...; listKind="decimal"; }` unconditionally. |
| A-odf-8 | Odf.cpp:824 | major | roundtrip | shape/chart/qr/form blocks are saved and reopened as plain paragraphs | CONFIRMED | `grep -n '"shape"\|"chart"\|"qr"\|"form"' Odf.cpp` → 0 hits; these block types are entirely unhandled by the ODF reader/writer. |
| A-odf-9 | Odf.cpp:967 | critical | dirty-flag | `saveOdt` discards `f.write()`'s result, identical to A-docx-3 | CONFIRMED | Executed `qa/audit/A-{docx-3,odf-9,rtf-3}-write-failure-ignored-{docx,odt,rtf}.json` against `/tmp/aio-audit/fail.odt → /dev/full`: `document_save` returns `ok:true,dirty:false` despite every write failing. Code: `f.write(Zip::pack(entries)); return true;` (Odf.cpp:966-967). |
| A-odf-10 | Qr.cpp:20 | major | correctness | `encodeQr` does not produce a decodable QR symbol | CONFIRMED (one sub-claim corrected) | Two of the three claimed causes hold: (1) `fmt=0x5412` (Qr.cpp:155) is used as the *final* 15-bit format string, but that value is the correct result only for EC level M mask 0, while the payload is built as version-1-L (19 data + 7 ECC bytes, Qr.cpp:109-115) — a level/payload mismatch. (2) the data-placement zigzag (Qr.cpp:128-146) runs *before* `placeFmt` locks the format-info modules (Qr.cpp:154-163), so data bits are written onto those modules and then overwritten, corrupting the data stream. **Correction:** the `gfMul` reduction constant `0x1d` (Qr.cpp:11-24) is in fact the correct GF(256) reduction for QR's primitive polynomial `0x11D` (`0x11D & 0xFF == 0x1D`), matching standard reference implementations (e.g. Nayuki's) — this specific sub-claim in the original finding does not hold, but the other two independently break decodability, so the overall verdict stands. |

### `src/core/PdfPostProcess.cpp` — 4 findings, 4 CONFIRMED

| ID | File:line | Severity | Category | Title | Verdict | Evidence |
|----|-----------|----------|----------|-------|---------|----------|
| A-pdf-1 | PdfPostProcess.cpp:99 | major | roundtrip | `pdfUtf16String` only escapes `( ) \`, not `\r\n\t\b\f` | CONFIRMED | `pdfUtf16String` (PdfPostProcess.cpp:92-106): the escape check is `b=='('\|\|b==')'\|\|b=='\\\\'` only; a UTF-16BE low byte of `0x0D` passes through raw into a PDF literal string, where PDF string-literal CR/CRLF normalization rewrites it. |
| A-pdf-2 | PdfPostProcess.cpp:829 | major | structure | `copies>1` folds every repeated page's structure elements into one | CONFIRMED | `clipsOf` (PdfPostProcess.cpp:827-833) keys purely by `marker.blockId` with no page discrimination, so a block id repeated across physical copies of a page collects all its clips into one structure element's `/K`. |
| A-pdf-3 | PdfPostProcess.cpp:1087 | major | structure | consecutive `ListItem`s of any level are grouped into one flat `/L` | CONFIRMED | The sequence-walking loop (PdfPostProcess.cpp:1083-1098): `while (roleOf(sequence[i])==ListItem) ids.push_back(...)` collects *every* consecutive list item regardless of `level`, then calls `emitList(ids)` once — no split on level change. |
| A-pdf-4 | PdfPostProcess.cpp:1012 | major | roundtrip | merged table cells have no `/ColSpan`/`/RowSpan` in the structure tree | CONFIRMED | `grep -n "ColSpan\|RowSpan"` across `PdfPostProcess.{cpp,h}` → 0 hits; `PdfTagMarker` (PdfPostProcess.h:45) has no span fields to carry the information even if emission were added. |

### `src/core/RtfReader.cpp` + `RtfWriter.cpp` — 14 findings, 14 CONFIRMED

A long tail of round-trip losses in both directions: `pt-BR` collapses to `pt-PT` on export;
`\upr`/`\ud` are both discarded outright (losing *both* copies of the dual-representation text);
`saveRtf` ignores write failures exactly like the DOCX/ODF siblings; explicit `\sb0\sa0` on a
heading is overwritten by a later unconditional `\sb240\sa120`; nested list levels never emit
`\ilvl`; any non-`"bullet"` list kind (checkbox, task, …) is exported as a numbered list; a
`\pict` inside a table cell is silently dropped; `REF`/`SEQ`/`MERGEFIELD`/`DOCVARIABLE`/`FILLIN`
fields the writer itself emits are not understood by the reader and degrade to plain text;
`hide_if`/`sectionHidden` are never consulted by the writer, so hidden text is published; notes
lose all character formatting and endnotes always reopen as footnotes; table/break/note comments
are exported silently with no warning (unlike paragraph/list/image); `\column` has no reader
support and leaves a stray empty paragraph; bookmarks wrap the whole paragraph, ignoring
`Bookmark::offset`; and table column widths are always rebuilt equal, ignoring `colWidthsMm`.

| ID | File:line | Severity | Category | Title | Verdict | Evidence |
|----|-----------|----------|----------|-------|---------|----------|
| A-rtf-1 | RtfWriter.cpp:95 | major | roundtrip | `pt-BR` is exported as LCID 2070 (`pt-PT`) | CONFIRMED | `langLcid` (RtfWriter.cpp:89-106): `if (l.startsWith("pt")) return 2070;` — no `pt-BR`-specific branch, even though the reader (`RtfReader.cpp:239,251`) correctly distinguishes 1046↔`pt-BR` and 2070↔`pt-PT`. Same bug shape collapses `en-GB` into 1033 (`en-US`). |
| A-rtf-2 | RtfReader.cpp:1036 | major | data-loss | `\upr` and `\ud` are both routed to `Sink::Discard` | CONFIRMED | RtfReader.cpp:1030-1039: both `"upr"` and `"ud"` destinations unconditionally set `sink = Sink::Discard` — the ANSI fallback group and the nested `{\*\ud ...}` Unicode group are both thrown away, losing every copy of the text. |
| A-rtf-3 | RtfWriter.cpp:748 | major | error-path | `saveRtf` discards `f.write()`'s result, identical pattern to A-docx-3/A-odf-9 | CONFIRMED | Executed `qa/audit/A-{docx-3,odf-9,rtf-3}-write-failure-ignored-{docx,odt,rtf}.json` against `/tmp/aio-audit/fail.rtf → /dev/full`: `document_save` returns `ok:true` for the `.rtf` export despite every write failing. Code: `f.write(rtf.toUtf8()); return true;` (RtfWriter.cpp:747-748). |
| A-rtf-4 | RtfWriter.cpp:703 | major | roundtrip | heading export overwrites an explicit `\sb0\sa0` with a later unconditional `\sb240\sa120` | CONFIRMED | RtfWriter.cpp:691-703: `inner = paraProps(b)` (which emits `\sb0\sa0` for explicit zero spacing) runs first; the heading branch then unconditionally appends `inner += "\\sb240\\sa120 "` — RTF's last-control-wins semantics make the explicit zero spacing unrecoverable. |
| A-rtf-5 | RtfWriter.cpp:672 | major | roundtrip | nested list level (`\ilvl`) is never emitted | CONFIRMED | The list-item export loop (RtfWriter.cpp:665-677) emits `\\ls%1` only — no `\ilvl` anywhere in the function, while the reader does honor `\ilvl` for `para.ilvl`. |
| A-rtf-6 | RtfWriter.cpp:593 | major | roundtrip | checkbox/task lists export as numbered lists | CONFIRMED | `listTable` (RtfWriter.cpp:592-604): `const bool numbered = it.key() != "bullet";` — any kind other than the literal string `"bullet"` (checkbox, task, decimal, …) gets `nfc=0`; the reader (`RtfReader.cpp:1561`) maps any `nfc!=23` back to `"decimal"`, losing `checked` state. |
| A-rtf-7 | RtfReader.cpp:1907 | major | data-loss | an image inside a table cell is silently dropped | CONFIRMED | `finishPict` (RtfReader.cpp:1887-1908): `if (inTable) return;` — no block created, no error, no warning; `loadRtfFile` still returns success. |
| A-rtf-8 | RtfReader.cpp:1864 | major | roundtrip | `REF`/`SEQ`/`MERGEFIELD`/`DOCVARIABLE`/`FILLIN` fields become fixed text on reimport | CONFIRMED | `finishField` (RtfReader.cpp:1789-1866): the `key==...` chain only recognizes `HYPERLINK`/`PAGE`/`NUMPAGES`/`DATE`/`TIME`/`AUTHOR`/`TITLE`/`FILENAME`/`NUMWORDS`; everything else (including fields the writer itself emits via `MERGEFIELD`/`DOCVARIABLE`/`REF`/`SEQ`/`FILLIN`, RtfWriter.cpp:255-266) falls to the final `for (r:field.result) dest->push_back(r);`, losing the field marker. Writer also never emits `NUMWORDS` for `word_count` — `nativeFieldInstr` has no such branch, so the computed number is written as plain escaped text (RtfWriter.cpp:336-337). |
| A-rtf-9 | RtfWriter.cpp:650 | major | invariant | `hide_if`/`sectionHidden` text is published to RTF with no warning | CONFIRMED | `grep -n "hideIf\|hide_if\|blockHiddenByExpr\|sectionHidden" RtfWriter.cpp` → 0 hits; the export path never consults either flag before writing a block's text. |
| A-rtf-10 | RtfWriter.cpp:316 | major | roundtrip | notes lose character formatting; endnotes always reopen as footnotes | CONFIRMED | `emitRun`'s note branch (RtfWriter.cpp:310-318): `body += escapeText(nr.text);` — no `charProps`, no `\ftnalt` for endnotes. Reader (`RtfReader.cpp:1698`) always creates `type:"footnote"` regardless of the original type. |
| A-rtf-11 | RtfWriter.cpp:681 | major | spec | table/break/note comments are exported with no warning (unlike paragraph/list/image) | CONFIRMED | `commentSuffix` calls found at RtfWriter.cpp:656 (shape/form), 673 (list-item), 688 (image), 704 (paragraph/heading) — the table branch (`emitTable(b)+"\\pard\n"`, ~line 684) and the break branch have no `commentSuffix` call, so block comments on those types are silently dropped without the required warning. |
| A-rtf-12 | RtfWriter.cpp:660 | major | roundtrip | `\column` has no reader support, degrades to a stray empty paragraph | CONFIRMED | Writer emits `"\\column\\par\n"` (RtfWriter.cpp:659-662) for a column break; `grep -n '"column"' RtfReader.cpp` → 0 hits (only `"page"` is handled, RtfReader.cpp:1182), and `suppressEmptyPar_` is only set `true` for the page-break path — the `\par` after an unrecognized `\column` creates an empty paragraph via `onPar`'s `allowEmpty` fallback. |
| A-rtf-13 | RtfWriter.cpp:406 | major | roundtrip | bookmark wraps the whole paragraph, ignoring `Bookmark::offset` | CONFIRMED | `bookmarksWrap` (RtfWriter.cpp:400-408): `out = "{\\*\\bkmkstart %1}%2{\\*\\bkmkend %3}".arg(name,out,name)` wraps the entire `inner` paragraph string; `bm.offset` is never read anywhere in the function. |
| A-rtf-14 | RtfWriter.cpp:625 | major | roundtrip | exported table columns are always equal width, ignoring `colWidthsMm` | CONFIRMED | `emitTable` (RtfWriter.cpp:616-629): `const int colW = cols>0 ? 9000/cols : 9000;` builds equal `cellx` boundaries; `colWidthsMm` is never referenced in the function. |

### `src/core/Epub.cpp` + `Xhtml.cpp` — 10 findings, 10 CONFIRMED

An unbounded image read when the source isn't a regular file (device/FIFO) sits alongside a long
list of fidelity losses: endnotes with no in-flow reference are dropped entirely; footnote/endnote
`<aside>`s never carry the note's `lang`/`dir`; character styles are resolved for preview but
never emitted to EPUB; page-dependent fields (`page_count`, `page_number`, `xref kind:page`) are
always evaluated as page 1 of 1; block ids sanitize to colliding NCNames with no uniqueness
counter; a hidden/`show_if`-false note reference is still written as a visible `noteref`; `dir:
auto` always resolves to `ltr` instead of running strong-character detection; colliding CSS class
names silently keep only the first style's rule; and `list_bullet` is never emitted.

| ID | File:line | Severity | Category | Title | Verdict | Evidence |
|----|-----------|----------|----------|-------|---------|----------|
| A-epub-1 | Epub.cpp:567 | critical | hostile-input | image read has no cap when the source isn't a regular file | CONFIRMED | Epub.cpp:561-572: `if (f.size() > kMaxImageBytes)` passes for any device/FIFO (`QFile::size()` reports 0 for those), then `f.readAll()` blocks/grows until EOF, which `/dev/zero`-class sources never reach. Not executed (hang risk). |
| A-epub-2 | Epub.cpp:1312 | major | data-loss | an endnote with no in-flow reference is omitted entirely | CONFIRMED | `writeNotes` (Epub.cpp:853-865) only emits ids present in `ctx.noteOrder`, which is populated *solely* by `writeRuns` finding a run with `r.noteId` (Epub.cpp:383-384). A block of type `isNote` is excluded from the normal body walk (Epub.cpp:1312), so a note whose only referencing run was deleted never enters `noteOrder` and is never written anywhere in the package. |
| A-epub-3 | Epub.cpp:884 | major | contract | footnote/endnote `<aside>` elements never carry `lang`/`dir` | CONFIRMED | `writeNotes`'s aside/`<p>` construction (Epub.cpp:882-890) builds `Attrs` with only `id`/`epub:type`/`role`/`class` — no call to `blockAttrs`/`addLangDir`, unlike the normal paragraph path. |
| A-epub-4 | Epub.cpp:193 | major | data-loss | character styles are never resolved/emitted to EPUB spans | CONFIRMED | `grep -n "charStyle" Epub.cpp` → 0 hits; `fontFor` (used by Preview) resolves `charStyle` to a font/color, but `Epub.cpp` has no equivalent lookup anywhere. |
| A-epub-5 | Epub.cpp:378 | major | data-loss | page-dependent fields and `xref kind:page` are always rendered for page 1 of 1 | CONFIRMED | All three `runDisplayText` call sites in `Epub.cpp` (lines 111, 378, 787) pass the literal `1, 1` for page/pages. |
| A-epub-6 | Epub.cpp:115 | major | hostile-input | sanitized block ids collide with no uniqueness counter | CONFIRMED | `xmlIdFor` (Epub.cpp:115-127): pure per-character sanitization (letter/digit/`-_.` kept, else `-`) with no dedup/counter — `"a b"` and `"a-b"` both sanitize to `"a-b"`. |
| A-epub-7 | Epub.cpp:379 | major | visibility | a hidden/`show_if`-false noteref is still written as a visible link | CONFIRMED | `writeRuns`'s noteref branch (Epub.cpp:378-396) runs unconditionally whenever `!r.noteId.isEmpty()`, regardless of whether `shown` (the hidden/show_if-aware display text) is empty; an empty `shown` only changes the *label* fallback (`use.label = shown.isEmpty() ? QString::number(...) : shown`), not whether the `<a epub:type="noteref">` is emitted. |
| A-epub-8 | Epub.cpp:331 | major | visibility | `dir:auto` always resolves to `ltr` | CONFIRMED | `blockDir` (Epub.cpp:331-336): `if (b.dir=="rtl"\|\|b.dir=="ltr") return b.dir; return "ltr";` — no strong-character detection for the unset/`auto` case, unlike `resolvedParagraphDirection` used elsewhere. |
| A-epub-9 | Epub.cpp:150 | minor | data-loss | colliding CSS class names keep only the first style's rule | CONFIRMED | `cssClassName` (Epub.cpp:150-164): both `_` and `-` normalize to `-`, so `"Foo_Bar"` and `"Foo-Bar"` both become `"sFoo-Bar"`. |
| A-epub-10 | Epub.cpp:765 | minor | data-loss | custom `list_bullet` is never emitted | CONFIRMED | `writeList` (Epub.cpp:738-767): no reference to `listBullet` anywhere in the function; only `checkbox`/`task` get a special marker. |

### `src/mcp/` + `src/cli/` + `src/lok/` — 10 findings, 10 CONFIRMED

Three critical "silently edit/read the wrong document" bugs (CLI `dispatch`, MCP `tools/call`,
and a LibreOffice-conversion file clobber) sit alongside IPC-protocol and CLI-robustness issues:
a partial-line frame is read with `readAll()` and dropped instead of buffered; `aioffice play`
mutates the live GUI session per-step with no checkpoint/rollback despite `docs/mcp-tools.md`
calling it an atomic batch; `aioffice new` ignores `AIOFFICE_HEADLESS` and can blank a live GUI
document; invalid JSON CLI/MCP arguments silently become `{}` and the tool still runs; starting a
second GUI process unconditionally unlinks and steals the first one's IPC socket; the
`page_preview` MCP schema types `page` as `integer` only even though `page:"all"` is a real,
documented, working value; and `findSoffice`/`convertTo` misreport a `FailedToStart` process as a
120-second timeout.

| ID | File:line | Severity | Category | Title | Verdict | Evidence |
|----|-----------|----------|----------|-------|---------|----------|
| A-mcp-1 | Cli.cpp:146 | critical | error-path | `dispatch()` falls back to a brand-new blank `Session` on IPC transport failure | CONFIRMED | `dispatch` (Cli.cpp:141-151): `if (err.isEmpty() \|\| r.ok) return r;` — on a timeout, `err` is non-empty and `r.ok` is false, so this is false and execution falls through to `Session session; return session.call(tool,args);`, a disposable headless session unrelated to the live GUI document. |
| A-mcp-2 | McpServer.cpp:185 | critical | error-path | `tools/call` falls back to the private headless session on any IPC timeout | CONFIRMED | McpServer.cpp:181-187: `if (!ipcErr.isEmpty() && !out.ok) out = session_->call(name,args);` — identical fallback shape to A-mcp-1. `grep -n AIOFFICE_HEADLESS McpServer.cpp` → 0 hits, confirming the env var is never consulted here unlike `Cli::dispatch`. |
| A-mcp-3 | LokEngine.cpp:98 | critical | data-loss | `soffice --convert-to --outdir` overwrites a same-named sibling file before the rename | CONFIRMED | `convertTo` (LokEngine.cpp:62-104): `--outdir` is the destination directory, and soffice always names its own output after the *input*'s basename inside that directory (`produced = outdir + inputBaseName + outputSuffix`) — any pre-existing file at that exact produced path is clobbered by soffice itself, before this function's own `QFile::remove(outputPath)`+rename even runs. |
| A-mcp-4 | Ipc.cpp:106 | major | ipc | a partial (no-newline-yet) frame is read with `readAll()` and dropped | CONFIRMED | Ipc.cpp:104-111: `while (sock->canReadLine() \|\| sock->bytesAvailable()>0) { line = canReadLine() ? readLine() : readAll(); ...; if (!doc.isObject()) continue; }` — when only a line fragment has arrived, `canReadLine()` is false so `readAll()` consumes the fragment, `QJsonDocument::fromJson` fails on it, and `continue` discards it; the remainder of the line arrives later missing its prefix and also fails to parse. |
| A-mcp-5 | Cli.cpp:234 | major | contract | `aioffice play` is not atomic despite being documented as a batch | CONFIRMED | The `play` loop (Cli.cpp:213-294) calls `ipcCall(t,a,&ipcError)` directly per step against the live GUI session with no checkpoint/restore/batch wrapping; a failing `expect` or `ok:false` step simply stops and reports, leaving every prior step's mutation applied to the live document. |
| A-mcp-6 | Cli.cpp:303 | major | session | `aioffice new` ignores `AIOFFICE_HEADLESS`, can blank a live GUI document | CONFIRMED | The `new` command (Cli.cpp:296-310) gates only on `if (ipcAvailable())`, unlike `dispatch`'s `ipcAvailable() && qEnvironmentVariableIsEmpty("AIOFFICE_HEADLESS")` — a headless caller still `ipcCall`s `document_new` onto the live GUI session. |
| A-mcp-7 | Cli.cpp:138 | major | hostile-input | invalid JSON CLI arguments silently become `{}` and the tool still runs | CONFIRMED | `parseArgsJson` (Cli.cpp:133-139): `QJsonDocument::fromJson(s).object()` — for an array, a bare scalar, or a parse error, `.object()` returns `{}` with no error surfaced; `McpServer.cpp:177-179` has the identical shape for a non-object `arguments` string. |
| A-mcp-8 | Ipc.cpp:100 | major | session | a second GUI process steals the first one's IPC socket unconditionally | CONFIRMED | `IpcServer::start` (Ipc.cpp:96-101): `QLocalServer::removeServer(ipcSocketPath()); if (!server->listen(...)) return false;` — `removeServer` runs unconditionally before `listen`, regardless of whether another live process is already serving that path. |
| A-mcp-9 | ToolDispatch.cpp:245 | major | contract | `page_preview`'s MCP schema types `page` as `integer` only; `page:"all"` is rejected by schema-validating hosts | CONFIRMED | ToolDispatch.cpp:243-247: `{"page", prop("integer")}` — but `Session::pagePreview` (Session.cpp:1325-1339) explicitly handles `pageV.isString() && pageV.toString()=="all"` as a documented, working mode. |
| A-mcp-10 | LokEngine.cpp:86 | minor | error-path | a `soffice` that fails to start is reported as a 120s timeout | CONFIRMED | `findSoffice` (LokEngine.cpp:11-14) checks only `QFileInfo::exists`, not `isExecutable`; `convertTo` (LokEngine.cpp:83-88) treats any `!p.waitForFinished(120000)` as `"soffice timed out"` without checking `p.error()==QProcess::FailedToStart`, which returns immediately rather than after 120s. |

### `src/app/` — 13 findings, 12 CONFIRMED, 1 REJECTED

Closing the window, discarding a tab mid-switch, and closing a tab to the left of the active one
all lose or mis-target an open document; switching tabs clears the undo/redo stack because
`TabSnap` only stores `doc`/`path`/`dirty`/`title`; Find/Previous reset the caret and lose the
selection on every jump; the cached `fontSizePt_` starts at a hard-coded `16` and every ±1 click
applies a delta from that cache instead of the caret's real size; saving to `.doc` shows "Documento
salvo" and silently leaves `path_`/`dirty_` untouched (it's an export); `revisionAccept`/`Reject`'s
return values are discarded entirely, so a format-revision-with-no-prior-formatting failure is
silent; Left-arrow moves the caret by raw UTF-16 code units instead of grapheme boundaries and
splits emoji surrogate pairs; one `page_numbers` toggle produces two separate undo entries;
`exportPdf`'s `warnings` array is never surfaced to the user; and bold/italic/underline/etc. only
ever set the attribute `true`, with no toggle-off.

| ID | File:line | Severity | Category | Title | Verdict | Evidence |
|----|-----------|----------|----------|-------|---------|----------|
| A-app-1 | qml/TitleBar.qml:90 | critical | dirty-data-loss | closing the window discards dirty documents with no prompt | CONFIRMED | `grep -rn onClosing src/app/qml` → 0 hits; the only handler on the close button is `bar.window.close()` (TitleBar.qml:90), with no `gateUnsaved`/`UnsavedSheet` wiring anywhere in the window lifecycle. |
| A-app-2 | AppController.cpp:1064 | major | dirty-path-data-loss | discarding while switching tabs blanks the *source* tab's snapshot, not just the session | CONFIRMED | `confirmDiscard` (AppController.cpp:1058-1080) calls `session_.newDocument({force:true})` **before** dispatching `pendingAction_=="switch"` to `runPending()`→`switchTab`→`snapshotActive()` (AppController.cpp:933-941), which copies the now-blanked `session_` state into `tabSnaps_[activeTab_]` — the tab being navigated *away from* is the one that gets permanently blanked, losing its path/title/content even if it had been saved before. |
| A-app-3 | AppController.cpp:1042 | major | tabs-document-identity | closing a tab to the left of the active one loads the wrong document | CONFIRMED | `closeTab` (AppController.cpp:1030-1044): `tabSnaps_.removeAt(index)` then `if (activeTab_ >= tabSnaps_.size()) activeTab_ = tabSnaps_.size()-1;` — when `index < activeTab_`, `activeTab_` is never decremented, so the same numeric index now refers to the tab that used to be one slot to the right; `loadTab(activeTab_)` reloads that document under the identity of the one the user thought was still active. |
| A-app-4 | AppController.cpp:950 (`restoreSnapshot`) | major | undo | switching tabs clears undo/redo | CONFIRMED | `Session::restoreSnapshot` (Session.cpp:5740-5755): `undo_.clear(); redo_.clear();` unconditionally. `TabSnap` (confirmed via `snapshotActive`, AppController.cpp:933-941) stores only `doc`/`path`/`dirty`/`title` — no undo/redo stacks to restore. |
| A-app-5 | AppController.cpp:930 | major | find | Find Next/Previous reset the caret to offset 0 and drop the selection every jump | CONFIRMED | `jumpFind` (AppController.cpp:895-931) calls `placeCaret(h.id, h.end, ...)` then `session_.setSelection(...)`, then `jumpToBlock(s.focusId)` — and `jumpToBlock` (AppController.cpp:876-884) unconditionally ends with `placeCaret(id, 0, -1, -1, false)`, resetting the caret to the block start and (with `extend=false`) discarding the just-built selection. "Previous" always picks `hits.size()-1`, i.e. the first hit in document order, regardless of caret position. An `ok:false` search (e.g. invalid regex) is never distinguished from "no hits" since `jumpFind` only checks `hits.isEmpty()`, never `r.ok`. |
| A-app-6 | AppController.cpp:303 | major | formatting | font-size ±1 buttons apply a delta from a hard-coded `fontSizePt_=16` cache, not the caret's real size | CONFIRMED | `fontSizePt_` default member initializer is `16` (AppController.h:262); `setFontSizePt` (AppController.cpp:302-311) compares/writes only against this cached member, never reading the actual run/style size at the caret before applying the delta. |
| A-app-7 | AppController.cpp:404 (`savePath`) | major | dirty-path | saving to `.doc` shows "Documento salvo" but leaves `path_`/`dirty_` untouched | CONFIRMED | `savePath` (AppController.cpp:397-405) calls `handleResult(..., "Documento salvo")` unconditionally on `ok:true`; `isWorkingDocumentPath` (Session.cpp:211-217) only recognizes `.odt`/`.docx`/native json — `.doc` is explicitly in `isExportSaveExtension` (Session.cpp:221-226), so `saveDocument` leaves `path_`/`dirty_` alone for it, while the GUI still reports success as if it had become the working file. |
| A-app-8 | AppController.cpp:522 | major | revisions-error-path | `revisionAccept`/`revisionReject`'s return values are discarded | CONFIRMED | `acceptRevision`/`rejectRevision` (AppController.cpp:518-525): `session_.revisionAccept(...)`/`session_.revisionReject(...)` — results assigned to nothing, no `handleResult` call; an `{ok:false,"format revision has no prior formatting"}` response is silently swallowed. |
| A-app-9 | DocumentCanvas.cpp:221 | major | input-corruption | Left/Right arrow move the caret by raw UTF-16 units, splitting surrogate pairs | CONFIRMED | DocumentCanvas.cpp:221-232: `s.focusOff = s.anchorOff = qMax(0, s.focusOff - 1)` / `qMin(n, s.focusOff + 1)` — plain `QString`-index arithmetic; `grep -n leftCursorPosition src/app/DocumentCanvas.cpp` → 0 hits, even though `DocumentLayout` exposes grapheme-aware cursor movement. |
| A-app-10 | qml/Sidebar.qml:113 | major | stale-ui | outline sidebar does not track the open document | **REJECTED** | The scenario does not reproduce: `Sidebar.qml:152-155` has `Connections { target: app; function onDocumentChanged() { outlineRep.model = app.outline() } }`, which re-evaluates the model on every `documentChanged` emission. `AppController` does emit `documentChanged()` from `session_.setOnChanged(...)` (AppController.cpp:26-28), which fires via `markChanged()`/`onChanged_()` on `document_open` and every edit. The static `model: app.outline()` binding at line 113 is indeed a one-shot evaluation, but the explicit `Connections` handler refreshes it correctly — opening a DOCX with headings does repopulate ESTRUTURA. No code change needed. |
| A-app-11 | AppController.cpp:829 | major | undo | one `page_numbers` toggle produces two separate undo entries, needing two Ctrl+Z | CONFIRMED | `setPageNumbers` (AppController.cpp:829-835) issues two independent `session_.setHeader(...)`/`session_.setFooter(...)` calls, each of which pushes its own undo frame (per `Session::setHeader`/`setFooter`'s standard `pushUndo()` pattern used throughout `Session.cpp`); the getter `pageNumbers()` (AppController.cpp:812-815) is `header.pageNumbers \|\| footer.pageNumbers`, so one undo only clears one side and the switch stays visually on. |
| A-app-12 | AppController.cpp:358 | minor | swallowed-warning | `exportPdf`'s `warnings` are never surfaced | CONFIRMED | `handleResult` (AppController.cpp:358-366) checks only `ok`/`error`; no reference to a `warnings` field anywhere in the function. |
| A-app-13 | AppController.cpp:436 | minor | formatting | bold/italic/underline/strike/super/subscript only ever set the flag `true` | CONFIRMED | `applyBold`/`applyItalic`/etc. (AppController.cpp:436-...): each does `args["bold"]=true;` (or the equivalent) unconditionally — no read of the current selection's state to toggle off. |

### `qa/*.py` + `scripts/` + `CMakeLists.txt` + `.circleci/config.yml` + `shell.nix` — 6 findings, 6 CONFIRMED

Five QA harness false-greens plus one CI artifact-path mismatch: the foreign-DOCX revision
round-trip comparator never checks `prior_runs`/`payload`/`block_id`; the EPUB "nested list"
check only counts `<li>` siblings; the RTF round-trip criterion fixture uses the literal word
"bold" as its formatting proof and declares no runs/table/footnote/field/bookmark expectations;
the PDF reading-order fixture expects two indistinguishable `/P` nodes so a reversed export still
matches; `check_wave19_pdf_tags.py` prints "ok" and exits 0 even when `verapdf` isn't on `PATH`;
and CircleCI's `store_artifacts` points at a `.json` path that the job never writes (it writes
`.txt`), and the step lacks `when: always`.

| ID | File:line | Severity | Category | Title | Verdict | Evidence |
|----|-----------|----------|----------|-------|---------|----------|
| A-qa-1 | qa/check_roundtrip.py:288 | major | test-gap | foreign DOCX revision round-trip never compares `prior_runs`/`payload`/`block_id` | CONFIRMED | `compare_revision_lists` (check_roundtrip.py:268-295): `norm()` computes a `payload` field but the comparison loop (`for key in ("kind","author","start","end","text")`, line 288) never includes it; `prior_runs`/`block_id` are not captured by `norm()` at all. Only `deleted_runs` (a separate, explicit check at 291-295) is actually compared. |
| A-qa-2 | qa/check_epub.py:286 | major | test-gap | `nested_list:"required"` only counts `<li>` siblings, not actual nesting | CONFIRMED | check_epub.py:282-287: the check is `"<ol" not in bundle or "<ul" not in bundle` plus `bundle.count("<li") < 2` — a flat `<ul><li>A</li><li>B</li></ul>` with no nested `<ol>`/`<ul>` inside an `<li>` satisfies both conditions. |
| A-qa-3 | qa/wave20-rtf-roundtrip-criterion.rtf.json:15 | major | test-gap | RTF round-trip criterion checks the literal word "bold", not actual bold formatting | CONFIRMED | The fixture's `plain_contains` list includes the literal string `"bold"` (matching the English word in the source text, not a formatting check), and its `body` array specifies only `type`/`plain` per entry — no `runs`/`bold`/`italic`/`dir`/`type:table`/`footnote`/`field`/`bookmark` assertions anywhere in the file. |
| A-qa-4 | qa/wave19-pdf-reading-order.pdf.json:7 | major | test-gap | reading-order fixture expects two indistinguishable `/P` nodes | CONFIRMED | The fixture's `document_child_types` is `["P","P"]`; `check_pdf.py:398-403` compares the whole list, but exporting the frame and paragraph in the wrong relative order still yields `["P","P"]` since both node types collapse to the same string — the comparison cannot detect a reversed order. |
| A-qa-5 | qa/check_wave19_pdf_tags.py:131 | major | false-pass | missing `verapdf` prints a notice and still exits 0 | CONFIRMED | check_wave19_pdf_tags.py:122-143: `except FileNotFoundError: print("verapdf not on PATH")` falls through to the unconditional `print("ok")` at the end of `main()` with no `SystemExit` — unlike `check_pdf.py`'s equivalent path. |
| A-qa-6 | .circleci/config.yml:103 | minor | ci | `store_artifacts` path doesn't match what the job writes, and lacks `when: always` | CONFIRMED | The `run` step redirects to `/tmp/last-playbook-output.txt` (config.yml:94); `store_artifacts` (config.yml:101-103) requests `/tmp/last-playbook-output.json` — a different file that no step ever creates — and has no `when: always`, so on a failing `run` (`exit 1`) the step is skipped entirely. |

---

## Playbooks executed (`qa/audit/*.json`)

All run with:
```
export AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
./build/linux/aioffice-cli play qa/audit/<file>.json
```

| File | Proves |
|------|--------|
| `A-session-1-undo-path.json` | A-session-1 |
| `A-session-2-batch-save-rollback.json` | A-session-2 |
| `A-session-3-revision-id-collision.json` | A-session-3 |
| `A-session-6-set-selection-dirty.json` | A-session-6 |
| `A-session-7-insert-table-no-cap.json` | A-session-7 |
| `A-session-8-document-include-drops-revisions.json` (input `qa/audit/fixtures/include-source.aio.json`, copied by `qa/audit/setup.sh`) | A-session-8 |
| `A-document-1-table-caption-lost.json` | A-document-1 |
| `A-{docx-3,odf-9,rtf-3}-write-failure-ignored-{docx,odt,rtf}.json` (`qa/audit/setup.sh` links `/tmp/aio-audit/fail.{docx,odt,rtf}` to `/dev/full`) | A-docx-3, A-odf-9, A-rtf-3 |

`A-session-8`'s input fixture (`qa/audit/fixtures/include-source.aio.json`) is a hand-built
minimal `.aio.json` with one paragraph (`p9`, text `"AC"`), one `delete` revision whose
`deleted_runs` carry the missing `"B"`, and one bookmark on that block — reproduced here for the
record:

```json
{
  "schema": 1,
  "meta": {"title": "Source"},
  "body": [
    {"id": "p9", "type": "paragraph", "style": "Normal", "runs": [{"text": "AC"}]}
  ],
  "revisions": [
    {"id": "rev1", "author": "IA", "kind": "delete", "block_id": "p9", "start": 1, "end": 1, "text": "B",
     "deleted_runs": [{"text": "B"}]}
  ],
  "bookmarks": [
    {"id": "bm1", "name": "aqui", "block_id": "p9", "offset": 0}
  ]
}
```

None of these 8 files are referenced by `CMakeLists.txt`/`ctest` — they are intentionally kept
out of the green suite because they currently fail (demonstrate open bugs), per the task brief.
