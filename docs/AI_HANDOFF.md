# AI Handoff — AI Office Writer

> **Propósito:** log operacional entre sessões de IA. Leia este arquivo **antes** de qualquer tarefa; atualize ao **concluir** (ou ao parar com trabalho parcial).
>
> **Não duplicar:** documentação de produto fica em `DOCUMENTACAO_COMPLETA.md` e `DESIGN_CHANGES.md`.
>
> **Manutenção:** manter no máximo ~20 entradas aqui; mover as mais antigas para `AI_HANDOFF_ARCHIVE.md`.

---

## 2026-10-20 — Fleet wave19 — Composer / Cursor Grok / Grok Build / Sonnet

**Objective:** Close Fleet wave19 PDF (review pass #2, handoff, feature catalog). Run `scripts/qa-all.sh --gui --interop --pdf`; record `git log main..HEAD --oneline`. No push.

**Done:**
- Review #2: empty findings after fix commit `a1a9114` (repeated table headers per page fragment; Arabic molded-glyph `ToUnicode` for PDF/UA; table `rows`/`cols` validated on `document_open`/`document_compare`/`document_include`; `wave19-pdf-tags` wired into ctest). Independent re-checks: split-table `/TH`+`/MCR` per page, comments+ua/pdfa veraPDF compliant, bad table JSON rejects with `ok:false`.
- `scripts/qa-all.sh --gui --interop --pdf`: **54/54** ctest passed (9 `pdf` tests, including `comments`, `fonts-ar-ua`, `split-table`, `table-shorthand`; up from 49). **Interop:** skipped (`soffice` not on PATH in this shell).
- `docs/WRITER_FEATURE_CATALOG.md`: wave 19 closes PDF/A+tagged PDF, XMP, font embed, reading order, document accessibility; new Todo **Subconjunto de fontes no PDF**.

**Decisions:** Font subsetting stays out of scope (D8); nested list/table in cell and footnote-in-cell tagging limits documented in `docs/mcp-tools.md`.

**Next suggested step:** Fleet `notion-sync`, then human `approve-pr` / `open-pr`.

**Branch commits (`main..HEAD`):**

```
f53b306 chore: ignore Python bytecode caches
67c1512 docs: use Cursor Sonnet only while Claude is exhausted
a1a9114 fix: tag repeated table headers and map Arabic glyphs for PDF/UA
c5cf5c5 docs: record the Claude usage-limit fallback for Fleet nodes
14f77d6 docs: record the PDF compliance commit hash in the handoff
5cbbbba fix: make PDF/A-1b and PDF/UA-1 exports pass veraPDF
012b44c merge: integrate wave19 PDF QA harness with T1/T2 handoff history
0fc93a9 merge: integrate wave19 PDF export metadata with structure tagging
e97af63 docs: record the PDF structure tagging commit hash in the handoff
64c8cea feat: tag PDF structure for lists, tables, figures, notes, and TOC
e3457a7 feat: add wave 19 PDF compliance QA harness (veraPDF, qpdf, pdffonts)
b748399 feat: PDF export XMP metadata, PDF/A flatten warnings, and ua mode
508d35c docs: update lessons from PR #6
4538e0e docs: add wave 19 PDF compliance spec and Fleet
343a16d docs: add evidence-backed lessons and a lessons node at the start of every Fleet
27a3aea docs: require later-phase isolated Fleet nodes to merge the prior integration commit
```

**Commits:** `a1a9114` fix: tag repeated table headers and map Arabic glyphs for PDF/UA; `5cbbbba` fix: make PDF/A-1b and PDF/UA-1 exports pass veraPDF; `64c8cea` feat: tag PDF structure…; `e3457a7` feat: add wave 19 PDF compliance QA harness; `b748399` feat: PDF export XMP metadata…

---

## 2026-10-20 — Cursor Grok 4.7 — Wave 19 PDF/A and PDF/UA compliance

**Objective:** Fleet `t4-compliance`. Make `scripts/qa-all.sh --gui --pdf` green: veraPDF PDF/A-1b and PDF/UA-1 on every suite document, structure and `/Lang` per the `.pdf.json` files, no fallback `/Part`, embedded fonts. Do not weaken expectations. No push.

**Done:**
- XMP is written with the prefixes veraPDF requires (`dc`, `pdf`, `xmp`, `pdfaid`, `pdfuaid`, `pdfaExtension` / `pdfaSchema` / `pdfaProperty`). `dc:title` and `dc:description` are language alternatives, `dc:creator` is a sequence, `pdfaid:part` and `pdfuaid:part` are elements. `dc:language` is omitted (catalog `/Lang` is the document language). No `pdf:Trapped`.
- The same title, author, subject, keywords, creator, producer, and dates are written into `/Info`. Dates are one instant in ISO-8601 and `D:YYYYMMDDHHmmSSOHH'mm'`. `/Trapped` is not copied forward.
- `ua: true` sets `/ViewerPreferences << /DisplayDocTitle true >>`.
- A table block with integer `rows`/`cols` (1–200 and 1–64) builds the same grid as `insert_table`. Out of range or a non-integer returns `{ok:false,error}`.
- `/Document` children are also stored inline so `qpdf --json=1` exposes each child's `/S`. Indirect copies remain for the parent tree.
- CTest registers only `wave19-pdf-*.json` playbooks that have a sibling `.pdf.json`. Expectation files and the `.aio.json` fixture are not playbooks.
- `scripts/qa-all.sh --gui --interop --pdf`: **49/49** ctest passed; LibreOffice interop ok. veraPDF 1b and ua1 compliant on the suite, including Arabic/CJK embedded fonts and the flattened watermark.

**Decisions:** Catalog `/Lang` stays the first block or run language, otherwise `pt-PT`. PDF/A-1 forbids `dc:language` as a bare attribute and forbids `pdf:Trapped`, so language stays on `/Lang` only. Top-level structure elements are inlined in `/Document /K` because the QA reader does not follow indirect references; the parent tree still points at the indirect objects, and veraPDF UA-1 accepts that.

**Next suggested step:** Fleet `review`, then `qa-final`.

**Commits:** `5cbbbba` fix: make PDF/A-1b and PDF/UA-1 exports pass veraPDF

---

## 2026-10-20 — Composer 2.5 — Wave 19 t3 PDF QA (D1)

**Objective:** Fleet `t3-pdf-qa`: veraPDF/qpdf/pdffonts in Nix, `qa/check_pdf.py`, ctest label `pdf`, `--pdf` in `qa-all.sh`, CI label, wave19 playbooks + `.pdf.json` expectations. No `src/`. No push.

**Done:**
- `shell.nix`: `verapdf`, `qpdf`, `poppler_utils` (flake.lock pin).
- `qa/check_pdf.py`: playbook export, veraPDF flavours, qpdf struct checks, pdffonts, `.pdf.json` schema, `--self-test`, subprocess timeouts.
- Playbooks `qa/wave19-pdf-{pdfa,ua,reading-order,fonts-ar-cjk,watermark-pdfa}.json` + expectations; `wave19-pdf-reading-order.aio.json`; fixture `qa/fixtures/wave19-dot.png`.
- CMake: exclude `wave19-pdf-*` from plain playbooks; `pdf_*` tests + `pdf_check_selftest` (playbook label).
- `scripts/qa-all.sh --pdf`; CircleCI `playbook|roundtrip|pdf`.
- Doc: `docs/waves/wave19-pdf-qa.md`.

**Decisions:** PDF ctests fail until T1/T2/T4; node `qa_green` = `scripts/qa-all.sh --gui` only. CI already runs `pdf` label (red until compliance node).

**Next suggested step:** Fleet `integrate-1` (T1+T2+T3 merged), then `t4-compliance`.

**Commits:** `e3457a7` feat: add wave 19 PDF compliance QA harness (veraPDF, qpdf, pdffonts)

---

## 2026-10-20 — Fleet wave19 t2-metadata — Composer 2.5

**Objective:** Wave 19 D2/D3/D7: custom PDF XMP on every export, PDF/A transparency flattening with `warnings[]`, `document_export_pdf {ua}` MCP arg, playbook `qa/wave19-metadata.json`. No `PdfPostProcess.cpp` or `paintPage` edits.

**Done:**
- `exportNativePdf`: `QPdfWriter::setDocumentXmpMetadata` (dc/pdf/xmp, `pdfaid` when `pdfa`, `pdfuaid` + PDF/A extension schema when `ua`+`pdfa`; no `pdf:Trapped`); PDF/A paints via document copy + opaque watermark overlay; `pdfPostProcessSetDisplayDocTitle(ua)` for T1 post-process.
- `Session::exportPdf`: `ua` validation (missing image alt → error); non-UA missing alt → `warnings[]`; returns `pdfa`/`ua` flags.
- MCP schema + `docs/mcp-tools.md`; playbook covers flatten warnings and UA alt error.
- `scripts/qa-all.sh --gui`: **43/43** ctest passed.

**Next suggested step:** integrate-1 merges with T1 tagging markers + T3 PDF QA; T1 consumes `pdfPostProcessPendingDisplayDocTitle()`.

**Commits:** `b748399` feat: PDF export XMP metadata, PDF/A flatten warnings, and ua mode

---

## 2026-10-20 — Cursor Grok 4.7 — Wave 19 PDF structure tags

**Objective:** Fleet t1-tagging (D4–D6, D9). Copy the original trailer `/ID` on every incremental update. Paint each taggable element inside exactly one `setClipRect`, with header, footer, page number, watermark, background, and page border inside artifact clips. One ordered marker list drives both painting and the post-processor. Structure is `/L`>`/LI`>(`/Lbl`,`/LBody`), `/Table`>`/TR`>(`/TH`|`/TD`), `/Figure` with `/Alt`, `/Note`, `/TOC`>`/TOCI`, artifacts outside the tree, catalog `/Lang`, and `/Span` `/Lang` when a run's language differs. `/Document` children follow `readingOrder` when it is set. The proof document has zero fallback `/Part`.

**Done:**
- `pdfTagMarkersForPage` is the shared order: artifact passes for background, page chrome, and overlay, and content clips in `orderedBlocksForPage` z-order. `paintPage` issues one `setClipRect` per marker. The post-processor rewrites the page stream only when the `W*` count matches that list.
- Structure elements follow `doc.readingOrder`, then any tagged id not listed, by document order. Header cells are `/TH` with `/ID` and `/Scope /Column`; body cells name that header. Notes carry `/ID`. A run whose language differs from the document language is its own `/Span`.
- Every new trailer copies the first trailer's `/ID` array.
- Proof document (heading, mixed-language paragraph, list, header-row table, image with alt, footnote, TOC, header/footer, watermark, border): `qpdf --json=1` shows `/Figure` first when `reading_order` puts the image first, catalog `/Lang (pt-PT)`, span `/Lang (en-US)`, figure alt, and no `/Part`. veraPDF UA-1: 104 passed, 2 failed — PDF/UA XMP identification and `DisplayDocTitle`, both t2 (D7).
- `scripts/qa-all.sh --gui`: **43/43** passed, including `qa/wave19-pdf-tags.json`.

**Decisions:** There is no document-level language field. Catalog `/Lang` is the first non-empty block or run language, otherwise `pt-PT`. Booklet physical pages and comment-only pages stay empty and keep the `/Part` safety net. Missing-alt warnings, XMP, and `DisplayDocTitle` stay with t2. Watermark opacity is not flattened here.

**Next suggested step:** integrate-1 with T2 metadata + T3 QA harness, then t4 full veraPDF UA and PDF/A.

**Commits:** `64c8cea` feat: tag PDF structure for lists, tables, figures, notes, and TOC

---

## 2026-10-20 — Cursor Grok 4.7 — DOCX conditional type and occurrence

**Objective:** CodeRabbit major on PR #6: `rememberConditionals` / `applyFieldConditionals` matched only the block-text fingerprint and took the first candidate from `boundCi+1`. A TOC entry with the same text as a heading, or an earlier copy of the same paragraph, received `hide_if` / `if`. Do not touch `src/app/` or `.cockpit/`. No push.

**Done:**
- DOCX `conditionals` also store the block type and the 1-based occurrence of that (type, fingerprint) pair in export order. Import ignores other types and applies the record to the n-th block with the same type and fingerprint. No exact candidate means the record is not applied. A field still requires a plain run with the same text.
- Playbook `qa/wave18-cond-occ.json`: a TOC of 3 entries with `hide_if` on one heading and a compound `if` on another; two empty paragraphs with `hide_if` only on the second; three paragraphs with the same text and `hide_if` only on the third.
- `scripts/qa-all.sh --gui --interop`: **42/42** ctest passed; LibreOffice interop ok.

**Decisions:** Occurrence counts every content block, including ones with no conditional. A TOC expands into `toc` blocks only, so heading and paragraph counts do not move. A fingerprint without a type or an occurrence is not applied.

**Next suggested step:** Fleet `notion-sync`, then human `approve-pr`.

**Commits:** `0f4bcae` fix: match DOCX conditionals by block type and occurrence

---

## 2026-10-20 — Cursor Grok 4.7 — CodeRabbit wave 18 field fixes

**Objective:** Fix the nine CodeRabbit defects on the wave 18 branch. The Fleet finding was already fixed. Do not touch `src/app/` or `.cockpit/`. No push.

**Done:**
- DOCX `conditionals` store the block id, a SHA-256 of the block text, and the run text. Import finds the block by that fingerprint in order and applies a field only when the run text matches a run that is still plain text. A TOC of 3 entries no longer moves `hide_if` / `if` onto an earlier paragraph. A native field that happens to show the same cached text is left as native.
- Field expressions share a depth limit of 64 for parentheses and `!`. Past that, the error is `expression too deeply nested`, the `if` field shows `#ERR`, and `validate` reports `invalid_expression`.
- `aioffice play` treats IPC/timeout/bad payload as a transport failure and aborts even when `expect` is `{"ok":false}`. A tool result `{ok:false}` still honors `expect`. `AIOFFICE_PLAY_CLASSIFY_CHECK=1` runs the classifier self-check.
- A native Word `IF` is written only when both operands are a number or a double-quoted string. An identifier or single quotes exports the evaluated text; the expression comes back from the embedded JSON.
- `stat` `words` uses `blockVisibleForStat` (`hide_if` and `section_hidden`), the same filter as chars and paragraphs.
- An `expect` that is empty or not an object fails the playbook before the call.
- `insert_field` serializes an object `arg` once for `seq` and `input_list`.
- The `insert_field.arg` schema is `anyOf` string or object.
- `FieldExpr.h` forward-declares `struct Document`.

**Tests:** `qa/wave18-review.json` (TOC round-trip, depth, `stat` words, IF operands, object args). `qa/check_play_expect.py`. `play_classify`. `scripts/qa-all.sh --gui --interop`: **41/41** ctest passed; LibreOffice interop ok.

**Decisions:** `document_stats.words` and the `word_count` field still use `Document::wordCount()` over every block. Only the `stat` field filters hidden blocks. A foreign `IF page > 1` from our export is evaluated text; `word/aioffice.json` restores the expression.

**Next suggested step:** Fleet `notion-sync`, then human `approve-pr` and open the PR to `main`.

**Commits:** `98194e3` fix: match DOCX conditionals by block text and harden fields

---

## 2026-10-20 — Fleet wave18 — Composer / Cursor Grok / Grok Build / Sonnet

**Objective:** Close the fleet wave 18 branch (dynamic fields + conditional text): record real QA on the integrated branch after the review loop fixes, sync `WRITER_FEATURE_CATALOG.md`.

**Done:**
- Fleet `review` closed with **`{"issues":0,"findings":[]}`** on the third pass; fix commits landed on the branch before handoff.
- **`refreshIfFields`** on dirty `document_get`/`document_save` recalculates live `stat`/`seq` after structural edits (`insert_blocks`, `insert_at_caret`, `delete_range`); a **clean** foreign open keeps imported evaluated text until the session is marked dirty. `stat chars` self-reference converges (up to 6 passes) to match `document_stats`.
- **`6845533`:** renumber `seq` on mid-document insert; `stat` counts exclude `hideIf` hidden paragraphs (aligned with `document_stats`).
- **`f4e6cb1`:** stat refresh after structural edits (playbook `qa/wave18-expr.json` insert/delete/text-edit expects).
- Manual re-checks (outside official playbooks): mid-insert `seq` + xref `category_number`; `stat` with `hideIf`; structural stat updates — all green.
- **`scripts/qa-all.sh --gui --interop`** (nix-shell, full build): **38/38 ctest** passed (includes `playbook_wave18-expr.json`, `wave18-kinds`, `wave18-docx-fields`, `wave18-odt-fields`, foreign round-trips); **interop** LibreOffice **docx→odt**, **odt→docx**, and **wave18 fields** resave/hops green (exit 0).

**Decisions:** Wave 18 D5/D6 honest limits stay as in `docs/waves/wave18-fields.md` (compound `if` on DOCX export, foreign `stat` paragraphs/tables/images, LO docx hop for `hideIf`). Do not re-open integrate merges on handoff.

**Next suggested step:** Fleet **`notion-sync`** → human **`approve-pr`** → open PR to **`main`**.

**Commits (branch vs `main`):** `f4e6cb1` fix: refresh stat fields after structural edits · `6845533` fix: renumber seq fields and drop hidden blocks from stat counts · `1d6c55d` merge: integrate wave18 DOCX and ODT field round-trip branches · `b1d50f4` docs: record the DOCX field commit hash in the handoff · `711f1ff` feat: round-trip DOCX IF, SEQ, stat, and document-info fields · `0c9fcd1` feat: round-trip wave 18 fields through ODT · `7778b7c` merge: integrate wave18 field kinds with field expressions · `2937c4a` docs: record the wave18 expression commit hash in the handoff · `7848309` feat: evaluate field expressions for conditional text · `486ddf3` feat: add wave18 field kinds, numbered captions, and seq xrefs · `f4c5323` docs: add wave 18 fields spec and Fleet; sync Notion from every Fleet

---

## 2026-10-10 — Cursor Grok — Wave 18 t3: DOCX fields

**Objective:** D5 + D7 for DOCX only. Export and import `if`, `seq`, `stat`, `doc_info`, and `hideIf`. Foreign fixture plus the foreign path of our own export (`embed_json: false`). Do not touch ODT or the field model.

**Done:**
- `src/core/Docx.cpp`: one comparison becomes a Word `IF`; a compound expression is the evaluated text, restored from `word/aioffice.json` `conditionals` when that part is present. `SEQ` with `ARABIC`/`ROMAN`/`ALPHABETIC`. `stat` pages/words/chars → `NUMPAGES`/`NUMWORDS`/`NUMCHARS`. `doc_info` → `DOCPROPERTY` Subject/Keywords, `CREATEDATE`, `SAVEDATE`, `REVNUM`. `hideIf` true at export writes `w:vanish` on the paragraph mark and each run.
- Import matches `IF` and `SEQ` by the first instruction token, then the existing substring checks (`CREATEDATE`/`SAVEDATE` still before `DATE`). A vanished paragraph mark, or every run vanished, sets `hideIf` to `1`.
- `qa/fixtures/make_fixtures.py` writes `wave18-docx-fields-foreign.docx` (no `word/aioffice.json`) with `IF`, `SEQ`, and `NUMPAGES`, plus `wave18-docx-fields-foreign.fields.json`. `qa/check_roundtrip.py` compares `field`, `field_arg`, and `hide_if`, and loads a `.fields.json` beside the fixture. Playbook `qa/wave18-docx-fields.json` saves with JSON and with `embed_json: false`.
- `scripts/qa-all.sh --gui`: **36/36** ctests passed.

**Decisions:** Wave 12 names stay: foreign `NUMPAGES` comes back as `page_count`, `NUMWORDS` as `word_count`. `NUMCHARS` is `stat`/`chars`. `stat` paragraphs/tables/images have no Word field, so the foreign path keeps the evaluated text. `document_get` re-evaluates `if` at page 1, so a condition that needs `doc.vars` changes its cached text after a foreign open. Qt stores JSON object keys alphabetically (`cond`, `else`, `then`; `format`, `name`).

**Next suggested step:** Fleet integrate-2 merges this SHA with the ODT branch. LibreOffice round-trip of these fields is t4.

**Commits:** `711f1ff` feat: round-trip DOCX IF, SEQ, stat, and document-info fields

## 2026-10-10 — Cursor Grok — Wave 18 t4: ODT fields

**Objective:** D6 and D7 for ODT only. Export and import conditional text, sequences, statistics, document info, sender, hidden paragraphs, and drop-downs. Foreign ODT fixture plus LibreOffice interop. Do not touch DOCX (`src/core/Docx.cpp` is the parallel task).

**Done:**
- `src/core/Odf.cpp` writes and reads `text:conditional-text`, `text:sequence` plus `text:sequence-decls`, the six count elements, `text:subject` / `keywords` / `creation-date` / `modification-date` / `editing-cycles`, `text:sender-firstname|email|company|phone-work`, `text:hidden-paragraph`, and `text:drop-down`. Content declares `xmlns:ooow`. Conditions use the LibreOffice formula words (`=`, `AND`, `OR`, `NOT`). User variables and meta subject, keywords, dates, and editing cycles travel with the package.
- Playbook `qa/wave18-odt-fields.json` saves and reopens an ODT. Foreign fixture `qa/fixtures/wave18-odt-fields-foreign.odt`. `qa/check_roundtrip.py` compares `field`, `field_arg`, and `hide_if`.
- `scripts/qa-interop.py` resaves that document with LibreOffice and also converts odt→docx and docx→odt.
- `scripts/qa-all.sh --gui --interop`: 36/36 ctests passed; LibreOffice revision and field interop ok.

**Decisions:** `stat` pages and words share `text:page-count` and `text:word-count` with the wave 12 fields, so a foreign reimport of those two comes back as `page_count` and `word_count`. The other four counts stay `stat`. Sender name is `text:sender-firstname` because ODF has no sender-name. LibreOffice 26.2 keeps the D6 elements on an ODT resave; the docx hop drops the condition operator (it writes `FORMULA`) and does not turn `text:hidden-paragraph` into `w:vanish`. Details in `docs/waves/wave18-fields.md`.

**Next suggested step:** integrate-2 merges this with the DOCX field export. The interop docx→odt check compares surviving text, not a native IF, until that export lands.

**Commits:** `0c9fcd1` feat: round-trip wave 18 fields through ODT

---

## 2026-10-10 — Cursor Grok — Wave 18 t1: field expressions

**Objective:** D1 + D3. Recursive-descent expression evaluator, `showIf` expressions, `Block.hideIf`, `if` field, `field_evaluate`, and `validate` `invalid_expression`. Do not touch `src/app/`. Leave the other `runDisplayText` field branches for the parallel field-kinds task.

**Done:**
- `src/core/FieldExpr.{h,cpp}`: literals, `doc.vars`, numeric refs (`page`, `pages`, `words`, `chars`, `paragraphs`, `tables`, `images`), comparisons, `&&` `||` `!`, parentheses. Numeric compare only when both sides are numbers; otherwise string. No eval/scripting.
- A bare `showIf` name keeps the old variable rule. Any other `showIf` is an expression; a syntax error displays `#ERR`.
- `hide_if` on a block (JSON and `apply_paragraph_format`) drops the paragraph from layout, `page_preview`, and PDF, and from `document_stats` paragraphs. The block stays in the JSON and in search. `page_preview` returns `ids` of the blocks laid out on the page.
- Field `if` with arg `{cond, then, else}` follows `set_vars`. Invalid cond shows `#ERR`. `validate` reports `invalid_expression` with the block id.
- MCP tool `field_evaluate {expr}` → `{value, type}` or `{ok:false, error}`.
- Playbook `qa/wave18-expr.json`. `scripts/qa-all.sh --gui`: **33/33** ctests passed.

**Decisions:** `hideIf` is judged against unfiltered totals so the decision does not recurse. The reported `paragraphs` count (expression and `document_stats`) excludes hidden paragraphs. A single identifier in `showIf`, including `page`, stays a variable name.

**Next suggested step:** Fleet integrate-1 merges this with the field-kinds branch (`if` is the only new `runDisplayText` arm here).

**Commits:** `7848309` feat: evaluate field expressions for conditional text

---

## 2026-10-10 — Cursor Grok — Wave 17: format per block, delete inside insert

**Objective:** Fix the two new CodeRabbit majors. One `format` revision per changed non-table block. Rejecting a delete inside an insert must put the restored text back inside that insert. Do not touch `src/app/`. Do not change `shiftRevisionOffsets` for ordinary typing.

**Done:**
- `applyCharacterFormat` with track changes records one `kind=format` revision per non-table block: that block's id, the range clamped to the block, and that block's `priorRuns`. A range that misses a block records nothing. Tables still get no revision. `revision_reject` restores the named block; `revision_reject_all` restores every one.
- Rejecting a delete (only `revisionReject` and `revisionRejectAll`) grows every `insert` on the same block with `start <= P <= end` by the restored length (`end += L`). Other revisions keep the `shiftRevisionOffsets` rule. The helper is `shiftRevisionsAfterDeleteReject`; `shiftRevisionOffsets` itself is unchanged.
- Backspace closes a multi-character insert that ended on the caret. `shiftRevisionOffsets` does not (`end > pivot` is false), so the range would stay one past the live text and `end += L` would count the deleted character twice. A one-character insert is left to the existing shift.
- Playbook `qa/wave17-reject-repairs.json`: two paragraphs, bold on both, two format revisions, reject one then the other, and `revision_reject_all` leaves neither bold. Insert `abc`, backspace `c` (insert `[0,2)`, delete at 2), `revision_reject_all` leaves empty text; `revision_accept_all` leaves `ab` and no revisions; reject the delete only leaves `abc` with insert `[0,3)`, then reject the insert leaves empty text.
- `scripts/qa-all.sh --gui --interop`: **32/32** ctests passed; LibreOffice docx→odt and odt→docx ok.

**Decisions:** The grow rule is not folded into `shiftRevisionOffsets`. Closing the insert on backspace is what makes the architect's `[0,2)` → `[0,3)` arithmetic true.

**Next suggested step:** Human `approve-pr` → open the PR to `main`.

**Commits:** `a7b199d` fix: restore every formatted block and a delete inside an insert

---

## 2026-10-10 — Cursor Grok — Wave 17 review: delete point, DOCX JSON precedence

**Objective:** Fix the five CodeRabbit findings on the wave 17 branch. Keep D1 strict (`delete` is `start == end`). Do not touch `src/app/`.

**Done:**
- `deleteBackward` and selection replacement in `insertAtCaret` record a delete at the point where the text was (`start == end`). `revisionReject` / `revisionRejectAll` shift other revisions by the restored length (`revisionRunsLen`), not `end - start`. `Revision::fromJson` collapses a legacy wide delete to that point so old files still pass `validate`.
- `loadDocx` skips native `w:ins` / `w:del` / `w:rPrChange` revisions when `word/aioffice.json` has a non-empty `revisions` array. The foreign path (no JSON) is unchanged.
- `applyCharacterFormat` with track changes stores `blockId` of the non-table block that supplied `priorRuns`, and clamps the range to that block before `splitRuns`.
- `qa/check_roundtrip.py` and `scripts/qa-interop.py` give `aioffice-cli` `timeout=300` and exit on `TimeoutExpired`. The interop playbook file is removed in a `finally`.
- D9 in `docs/waves/wave17-roundtrip.md` escapes the `|` in ``playbook\|roundtrip``.
- New playbook `qa/wave17-delete-point.json` plus `qa/wave17-delete-legacy.aio.json`. The DOCX playbook covers a format change across two runs saved with JSON (exactly one format revision).
- `scripts/qa-all.sh --gui --interop`: **31/31** ctests passed; LibreOffice docx→odt and odt→docx ok.

**Decisions:** `validate` still rejects an in-memory delete with `start != end`. Load normalizes legacy width, so the old negative expect on `wave17-docx-revisions-invalid.aio.json` now checks the remaining insert/format errors. `backspace` is an MCP tool so a playbook can drive `commitBackspace`.

**Next suggested step:** Human `approve-pr` → open the PR to `main`.

**Commits:** `659da86` fix: keep tracked deletes zero-width and trust DOCX JSON revisions

---

## 2026-10-10 — Fleet wave17 — Composer / Cursor Grok / Grok Build / Sonnet

**Objective:** Close the fleet `review` loop on wave 17 revision round-trip (D1–D10): re-run the real gate, confirm the four prior review findings are fixed, record handoff + catalog.

**Done:**
- Fleet `review` submitted **`{"issues":0,"findings":[]}`** on the second pass; no code changes on that node.
- Re-verified the four earlier findings: (1) `revision_reject`/`revision_reject_all` for `format` restores `priorRuns` via `restoreFormatPrior` + `shiftRevisionOffsets` in `Session.cpp`; (2) `applyCharacterFormat` captures `priorRuns` before mutating runs; (3) `validate()` rejects delete with `start != end` and insert/format with `end < start` (`qa/wave17-docx-revisions-invalid.aio.json` + negative expects); (4) CTest `roundtrip_foreign_docx_wave17` + `document_save {embed_json:false}` foreign reimport path.
- Extra manual check (no playbook): format revision without `priorRuns` (e.g. from third-party ODT) — `revision_accept` ok; `revision_reject` / `revision_reject_all` → `{ok:false,error:"format revision has no prior formatting"}`.
- **`scripts/qa-all.sh --gui --interop`** (nix-shell, full build): **30/30 ctest** passed (includes `playbook_wave17-docx-revisions.json`, `roundtrip_foreign_docx_wave17`, wave 17 ODT playbooks/fixtures); **interop** LibreOffice **docx→odt** and **odt→docx** green.

**Decisions:** Nested footnotes/endnotes stay **Fora** (D10); multi-paragraph notes are a new catalog Todo, not wave 17 scope.

**Next suggested step:** Fleet **`handoff`** done; human **`approve-pr`** → open PR to **`main`**.

**Commits (branch vs `main`):** `dd8378c` merge: land the wave17 t1 completion (D1/D2) left out of integrate-2 · `dee390b` docs: record the wave17 t4 merge commit hash in the handoff · `8a9efb6` merge: land t3 ctest fixtures on the DOCX revision interop branch · `80de02a` merge: integrate wave17 t3 ctest fixtures and t4 interop · `900f28d` docs: record the LibreOffice interop commit hash in the handoff · `49cad9c` feat: round-trip revision marks through LibreOffice · `c99cb33` feat: add ctest roundtrip fixtures and foreign revision QA · `5886b0b` docs: record wave17 integrate-1 merge and QA on the fleet branch · `0f5956c` merge: integrate wave17 t1 docx and t2 odt revisions · `00edabe` docs: record the ODT revision commit hash in the handoff · `47ac260` feat: round-trip ODT insert, delete, and format revisions · `157e642` docs: add wave 17 revision round-trip spec and Fleet

---

## 2026-10-10 — Cursor Grok — Wave 17 t4: ctest merge, revision invariant, foreign DOCX

**Objective:** Merge the t3 ctest/fixtures branch into this interop branch, keep the D3/D4 DOCX markup, implement D1 and D2, and register the foreign DOCX ctest. `scripts/qa-all.sh --gui --interop` must stay green.

**Done:**
- Merge conflicts: `src/core/Docx.cpp` kept this branch's export and import (`w:del`, `w:rPrChange`, `priorRuns`) and took t3's format `payload` built from the new run properties. `scripts/qa-all.sh` runs `ctest -L 'playbook|roundtrip'` and still accepts `--gui` and `--interop`. Fixture `*.revisions.json` expectations were not edited.
- D1: `validate` rejects a delete revision with `start != end`, and an insert or format revision with `end < start`. The negative playbook expects those messages.
- D2: `applyCharacterFormat` with track changes stores `priorRuns` (the range's `TextRun` slices before the change). `revisionReject` and `revisionRejectAll` of kind `format` restore that range and call `shiftRevisionOffsets`. A format revision with no `priorRuns` returns `{ok:false,error:"format revision has no prior formatting"}`. Accept is unchanged.
- Playbook `qa/wave17-docx-revisions.json`: rejecting a format revision restores bold. Saving DOCX with `embed_json: false` (no `word/aioffice.json`) reimports insert, delete, and format with the same kind, block, offsets, text, and author, and format comes back with `priorRuns`.
- CTest `roundtrip_foreign_docx_wave17` (`LABELS roundtrip`).
- `scripts/qa-all.sh --gui --interop`: 30/30 ctests passed; LibreOffice docx→odt and odt→docx both ok.

**Decisions:** Did not replace the D3/D4 importer with t3's depth-counter parser. `document_save` `embed_json: false` omits `word/aioffice.json` so a playbook can take the foreign path. An empty inner `w:rPr` still records `priorRuns` and does not invent formatting.

**Next suggested step:** Fleet integrate-2.

**Commits:** `8a9efb6` merge: land t3 ctest fixtures on the DOCX revision interop branch

---

## 2026-10-09 — Cursor Grok — Wave 17: LibreOffice revision interop

**Objective:** D8. `scripts/qa-all.sh --interop` (combinable with `--gui`) exports insert+delete+format, converts with `soffice --headless --convert-to` both ways, reimports, and compares kind, block, offsets, text, and author. Format revisions that passed through ODT must not keep `prior_runs`. No `soffice` prints `skip interop` and does not fail. Fix whatever LibreOffice showed, without changing D1–D5.

**Done:**
- `scripts/qa-all.sh --interop` and `scripts/qa-interop.py`. The probe opens `qa/wave17-odt-revisions.aio.json`, saves DOCX and ODT, converts with a temporary `-env:UserInstallation=file:///tmp/...` profile, strips embedded `aioffice` JSON, and reimports.
- LibreOffice 26.2 accepted the ODT tracked-change markup already written by D5 (change-start/end, `text:change` deletion, `text:format-change`). docx→odt of a file that only had `w:ins` dropped delete and format, because DOCX export did not emit them.
- odt→docx: LibreOffice wrote `w:del` + `w:delText` (deleted “B” is not live text), `w:ins` around “C”, and `w:rPrChange` as the last child of `w:rPr` with **no inner `w:rPr`** (no previous formatting). The old importer treated `w:delText` as live text (delete became a range, insert offsets shifted) and ignored `w:rPrChange`.
- DOCX export (D3): one `w:del` at `start` from `deletedRuns` (`w:delText xml:space="preserve"`), and `w:rPrChange` last inside `w:rPr` (previous `w:rPr` from `priorRuns`, empty when there is none). DOCX import (D4): `w:del` content goes to `deletedRuns`/`text` with `start == end` and never into `runs`; `w:ins` keeps the live range and its text; `w:rPrChange` becomes one `format` revision on that run. `priorRuns` is stored only when an inner `w:rPr` is present. `Revision.priorRuns` / JSON `prior_runs` is that field.
- `scripts/qa-all.sh --gui --interop` green, including `qa/wave17-odt-revisions.json` and both LibreOffice directions.

**Decisions:** Did not change D1–D5. LibreOffice copies italic from the previous run onto the deleted “B” (`w:b`+`w:i`) when converting ODT→DOCX; kind, block, offsets, text, and author still match, so the deletion span in ODT stays bold-only (D5). An empty `w:rPrChange` (the ODT path) does not invent `priorRuns` (D2/D4). `w:rPrChange` before `w:i` was also accepted by this LibreOffice, but export still puts `w:rPrChange` last (D3).

**Next suggested step:** Fleet integrate-2 with the ctest/fixtures branch. `t1` had not landed `priorRuns` on the integrate-1 branch; this node added the field so DOCX import can keep a real inner `w:rPr`.

**Commits:** `49cad9c` feat: round-trip revision marks through LibreOffice

---

## 2026-10-09 — Composer — Wave 17 T3: ctest, foreign fixtures, roundtrip QA

**Objective:** D6/D7/D9 — hand-built foreign DOCX/ODT fixtures, `check_roundtrip.py` foreign + fixture modes, CTest labels `playbook`/`roundtrip`, CI and `scripts/qa-all.sh` via ctest.

**Done:**
- `qa/fixtures/make_fixtures.py` emits minimal DOCX (w:ins, multi-run w:del, w:rPrChange bold→plain, mid-word delete) and ODT (insert/delete/format-change) without `word/aioffice.json`; sidecar `*.revisions.json` expectations drive ctest.
- `qa/check_roundtrip.py`: `--fixture` (playbook session), `--foreign` (export → strip native JSON → reimport, D6), legacy wave12 dir/align path unchanged.
- `enable_testing()` + one ctest per `qa/wave*.json` (excludes wave1-foundation and `*.aio.json`); roundtrip tests for fixtures + `roundtrip_foreign_odt_wave17`.
- `scripts/qa-all.sh` and CircleCI run `ctest -L 'playbook|roundtrip' --output-on-failure` (still supports `--gui`).
- Minimal `Docx.cpp` foreign import: `w:del` → `deletedRuns` (zero-width), `w:rPrChange` → format revision (payload only; full `priorRuns` remains t1).
- `scripts/qa-all.sh --gui` green (28 ctests).

**Next suggested step:** Fleet `t4-interop` / complete t1 DOCX export + `priorRuns`; add `--foreign docx` ctest when export path is ready.

**Commits:** `cadb6bc` feat: add ctest roundtrip fixtures and foreign revision QA

---

## 2026-10-09 — Composer — Wave 17 integrate-1 (t1 DOCX + t2 ODT)

**Objective:** Merge `t1-docx-revisions` and `t2-odt-revisions` into `fleet/wave17-roundtrip-c574277e`; resolve doc conflicts; gate with `scripts/qa-all.sh --gui`.

**Done:**
- `t1-docx-revisions` had no commits beyond the fleet base (`157e642`) — DOCX/`priorRuns` work not landed on that branch yet.
- Merge commit integrated `t2-odt-revisions` (`47ac260`, `00edabe`): ODF insert/delete/format round-trip, `qa/wave17-odt-revisions.json`, foreign fixture.
- No conflicts in `docs/AI_HANDOFF.md` or `docs/mcp-tools.md` (only t2 touched handoff; t1 empty).
- `scripts/qa-all.sh --gui` green, including wave 16 CTL and `qa/wave17-odt-revisions.json`.

**Decisions:** Did not block integrate on empty t1; fleet can re-merge when DOCX task lands.

**Next suggested step:** Unblock or complete `t1-docx-revisions`, then re-integrate before `build-1` if spec requires both paths.

**Commits:** `52697c8` docs: record wave17 integrate-1 merge and QA on the fleet branch · `0f5956c` merge: integrate wave17 t1 docx and t2 odt revisions

---

## 2026-10-09 — Cursor Grok — Wave 17: ODT revision round-trip

**Objective:** D5 only. Export and import ODF tracked changes for insert, delete, and format. Do not touch `Document.h`, `Session.cpp`, or `Docx.cpp` (`priorRuns` is the parallel DOCX task; ODF format-change has no previous attributes).

**Done:**
- `Odf.cpp` export: a delete is `<text:change text:change-id/>` at `start`, plus a changed-region whose `<text:deletion>` holds change-info and `deletedRuns` as a formatted `<text:p>`. A format revision is `text:change-start/end` on the range plus `<text:format-change>`. The comment that called a real deletion mark impossible is gone.
- Import is symmetric. `text:deletion` content becomes `deletedRuns`/`text` with `start == end` and is never written into live runs. `text:format-change` becomes `kind=format` with an empty payload (no prior formatting). Files that already carry `aioffice:extra` still keep that JSON (D6).
- Playbook `qa/wave17-odt-revisions.json`: save/reopen keeps insert, the zero-width delete, and format (including payload via the extra JSON); rejecting the delete puts "B" back once, bold. A foreign ODT with no extra JSON checks the native path the same way.
- `scripts/qa-all.sh --gui` green, including wave 12 round-trip, wave 15 PDF tagging, and wave 16 CTL.

**Decisions:** Delete marks are emitted at `start` even when the in-memory range is not yet a point (Session still records a width until the parallel task). Table cells are unchanged. `revisionReject` still shifts other revisions by `end - start`, so a zero-width delete reinserts the text and does not move later marks; that shift belongs to the Session task.

**Next suggested step:** Fleet integrate with the DOCX/`priorRuns` branch. Interop through LibreOffice is a later node.

**Commits:** `47ac260` feat: round-trip ODT insert, delete, and format revisions

---

## 2026-10-09 — Cursor Grok — Wave 16: hermetic test fonts

**Objective:** CircleCI failed `qa/wave16-ctl-justify.json` and `qa/wave16-ctl-caret.json` on the PR #3 branch. D9 was not hermetic: this NixOS host and the `nixos/nix` container resolved different fonts from the same `shell.nix`.

**Done:**
- Proved the leak. `pkgs.makeFontsConf` includes `/etc/fonts/conf.d`, XDG and FHS dirs, plus DejaVu. Locally `fc-list` saw 2876 faces, `fc-match "Liberation Serif"` was the system Liberation, and `:lang=ar` / `:lang=he` were DejaVu Sans. A conf limited to the Nix dirs the container actually has maps "Liberation Serif" to Source Serif 4 and reproduced the CI failures exactly: justify offset 120 stayed on line 0, offset 60 `x_mm` 108.64, offset 114 `x_mm` 28.14; mixed hit-test got offset 2, want 3.
- `shell.nix` now writes its own fonts.conf: only `liberation_ttf`, IBM Plex, Source Serif, JetBrains Mono, Noto and Noto CJK; cache `/tmp/aioffice-fontconfig-cache`; no system include. `lang=ar|he|ja|zh|ko` prefer Noto Naskh Arabic, Noto Serif Hebrew and Noto Sans CJK. `nix-shell --pure` with `HOME=/tmp/ci-home` matches Liberation Serif, Noto Naskh Arabic and Noto Serif Hebrew (1596 faces, all under `/nix/store`). CircleCI already runs `nix-shell --run`, so the shellHook `FONTCONFIG_FILE` is the job's config; the store path cannot be hardcoded in `.circleci/config.yml`.
- Recalibrated font-dependent probes only. Justified left edge is offset 102 at 20.00 mm (`$lte` 20.5 kept); offset 0 stays 189.81 mm (`$gte` 189.25 kept); offset 120 is still line 1. LTR multiline probe moved from offset 100 (now line 0, x 185.89) to 151, the middle of line 1 (offsets 103–198, x 105.72, `line == 1`). RTL final caret is offset 160 at 99.85 mm (`$lt` 105 kept; offset 114 is now on line 1 at 169.70). Mixed `hit_test(caret_geometry(o))` x_mm updated for Noto Serif Hebrew; offsets 0–11 round-trip. CJK breaks unchanged (40 / 80).
- `scripts/qa-all.sh --gui` green in that hermetic shell, including `qa/wave15-pdf-tagging.json`.

**Decisions:** Did not loosen the 20.0 / 190.0 edge bounds. Did not put a static `FONTCONFIG_FILE` in CircleCI.

**Next suggested step:** Update PR #3 with this commit (no push from this node unless asked).

**Commits:** `c9f8f24` fix: pin hermetic Nix fonts so CI and local share CTL metrics

---

## 2026-10-09 — Fleet wave16 — Composer / Cursor Grok / Grok Build / Sonnet

**Objective:** Close the fleet `review` loop on wave 16 CTL/CJK: verify revisor findings, confirm QA criteria, and record handoff + catalog.

**Done:**
- Revisor pass submitted with **`issues: 0`**, **`findings: []`**. Incremental diff review found no new defects beyond the fix commit below.
- **`967d6a0`** — table-cell **`caret_geometry`**: `Session::caretGeometry` bounds checks against `cellPlain(b->table[row][col])` when `row`/`col` are set (not the table’s full `plainText()`); `Preview.cpp` adds defensive `qBound` on line fraction. Reproduced: cell text `"ab"` (len 2), offset 10 → `{ok:false,error:"offset out of range"}` instead of a bogus `x_mm` (~859 mm).
- **`967d6a0`** — DOCX **`pPr` element order**: export emits `pStyle → numPr → bidi → jc` (`EG_PPrBase`), so `w:bidi` precedes `w:jc` (statically confirmed in `Docx.cpp`).
- Branch delivery (prior nodes): shared **`BlockTextLayout`** (paint/caret/hit/ruby), **`Block.dir`** + logical **`align`**, MCP **`caret_geometry`** / **`hit_test`** / **`caret_move`**, bidi boundary hit-test (D11), RTL justified-line width fixes, DOCX/ODT round-trip, **`qa/wave16-ctl-*.json`**, Noto fontconfig in Nix. Kashida: no app code — Qt **`QTextEngine::justify`** on this stack stretches RTL via character/space justification (no U+0640 tatweel observed on Qt 6.11 with Noto Arabic; see T4 handoff).
- **`scripts/qa-all.sh --gui`** is green under the hermetic fontconfig (same Nix shell CircleCI uses), including `qa/wave15-pdf-tagging.json` and the wave 16 CTL playbooks. The earlier "pending" note was the in-flight local run; CI then failed justify/caret because host fonts leaked (see the entry above).

**Decisions:** No code changes in the empty-findings review pass; documentation only on this node.

**Next suggested step:** Fleet **`handoff`** + PR to **`main`**. Font hermeticity is the entry above.

**Commits:** `967d6a0` fix: reject cell caret offsets past the cell and order w:bidi before w:jc · `7252ee9` docs: record the RTL justify commit hash in the handoff · `8a4a035` fix: justify RTL lines on a width that includes the trailing space · `9413a36` docs: record the bidi caret commit hash in the handoff · `8d21670` fix: snap bidi hit-test to the caret and hang RTL justify spaces · `c25abce` test: add wave16 CTL/CJK QA playbooks and Noto fontconfig · `f5eca54` docs: record t4+t5 merge QA and conflict resolution in handoff · `0f10fdc` merge: integrate wave16 t4-wire-rtl and t5-roundtrip · `53515e5` docs: record the paragraph-direction commit in the handoff · `e069582` feat: apply paragraph direction, ruby cursors, and caret movement · `8457f91` docs: record wave 16 T5 round-trip handoff entry · `ba208c3` feat: round-trip Block.dir and logical align in DOCX and ODT · `6a65d8a` Merge branch 'fleet/wave16-ctl-cjk-250f6dfa' into fleet/wave16-ctl-cjk-250f6dfa-t4-wire-rtl · `04920f4` Merge branch 'fleet/wave16-ctl-cjk-250f6dfa-t3-model-dir' into fleet/wave16-ctl-cjk-250f6dfa · `74e5c4f` feat: add block dir, logical align, and play numeric expect operators · `5b4f29b` docs: record the layout-core commit in the handoff · `6a74100` feat: share one block text layout for paint, caret, and hit-test · `2329c88` docs: route Cursor executor nodes to grok-4.7-high · `d2ed302` docs: scope wave 16 to the AI phase and record the AI-phase audit · `a30824b` docs: add wave 16 CTL/CJK spec, Cockpit Fleets, and model routing

---

## Template (copiar para o topo)

```
## YYYY-MM-DD — Cursor — <título curto>

**Objective:**

**Done:**

**Decisions:**

**Next suggested step:**

**Commits:** nenhum (usuário não pediu) | `<hash>` <mensagem>
```
