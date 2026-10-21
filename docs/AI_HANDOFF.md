# AI Handoff — AI Office Writer

> **Propósito:** log operacional entre sessões de IA. Leia este arquivo **antes** de qualquer tarefa; atualize ao **concluir** (ou ao parar com trabalho parcial).
>
> **Não duplicar:** documentação de produto fica em `DOCUMENTACAO_COMPLETA.md` e `DESIGN_CHANGES.md`.
>
> **Manutenção:** manter no máximo ~20 entradas aqui; mover as mais antigas para `AI_HANDOFF_ARCHIVE.md`.

---

## 2026-10-21 — Fleet wave20 — Composer / Cursor Grok / Grok Build / Sonnet

**Objective:** Close wave 20 (native EPUB 3 export + RTF import/export, `--formats` QA, format-compliance gate, Fleet review). Docs + catalog sync; no push.

**Done:**
- EPUB 3 via `document_save` (`.epub`): `XhtmlWriter`, epubcheck 0 errors / 0 warnings on `epub_wave20-epub-criterion.json` and `epub_wave20-epub.json`.
- Native RTF: `RtfWriter` / `RtfReader`; `.rtf` opens only through the native path; export/import playbooks wired in CMake (`wave20-epub.epub.json`, `wave20-rtf-in.rtf.json` with `{"mode":"import"}`).
- Format compliance: RTF spacing/headings/ODT table columns; list-table braces; bookmark destination groups; fixtures aligned with closed `hide_if` / `sender.name` rules (no criterion loosening).
- Fleet review iteration 2: **0 findings** after playbook wiring fix (`4669334`).
- `scripts/qa-all.sh --gui --interop --pdf --formats` ( `nix-shell` ): **68/68** ctest passed (~139 s); interop ok — docx↔odt, fields resave chain, our RTF→ODT and LO RTF→native import.
- `git log main..HEAD --oneline`: `4669334` … `0eb5e27` (17 commits on branch; EPUB export through review fix).

**Decisions:** Revisions, block comments, and shapes/forms in RTF export stay plain text with deduplicated `warnings[]` per spec D4/D7; DOC binary native remains out of scope (soffice); `export_html` unchanged — XHTML writer is EPUB-only until a follow-up Todo.

**Next suggested step:** Notion sync for wave 20 Done rows; next IA wave or chrome as board prioritizes (import EPUB, faithful RTF revisions/comments, migrate `export_html`).

**Commits:** `4669334`, `514629d`, `f18e12e`, `345065a`, `b007c0a`, `ef02651`, `7433d14`, `8ce1536`, `6819c63`, `8c7e9ba`, `02eda0d`, `d0c9131`, `056f259`, `4796ff4`, `3c356f3`, `4f12fa5`, `6b0dc00`, `0eb5e27`

---

## 2026-10-21 — Cursor Grok 4.7 — Fleet wave20 t5 format compliance

**Objective:** Take `scripts/qa-all.sh --gui --interop --formats` and `--gui --interop --pdf --formats` to green without weakening epubcheck or LibreOffice checks. No push.

**Done:**
- RTF character formatting is emitted in a group, so the delimiter space no longer eats the next run (`Mix bold and italic.`).
- Headings emit the stylesheet slot and a 0-based `\outlinelevel`, so the reader restores `type: heading`.
- `\cellx` lists column right edges. A leading 0 is not a column.
- ODT tables write one `table:table-column` per column. Without it, LibreOffice's ODT→RTF kept only the first column (`H`/`1`, dropped `V`/`2`).
- Nesting errors say `RTF group nesting depth exceeds 64`. A `{\listtext}` group that contains `\par` keeps that paragraph; marker-only groups stay skipped. If every inner group is closed and only the final `{\rtf1` brace is missing, the file opens. A truncated file still errors.
- Plain-text checks walk table cells the same way as `Block::plainText`. The interop probe sets cells on `t1`, the id `insert_blocks` assigns.
- `scripts/qa-all.sh --gui --interop --pdf --formats`: **64/66** ctest. Interop ok (docx, odt, fields, RTF both ways). All 10 PDF tests passed.

**Decisions:** Two expectations contradict closed rules. Not loosened.
- EPUB: `hide_if` is `show_secret == "yes"` while `show_secret` is `"no"`. `field_evaluate` returns false. D2 omits the paragraph only when the expression is true, so the chapter contains `SIMSecret`. epubcheck on that package is 0 errors and 0 warnings. The sibling check `hide_if_omitted: ["Secret"]` requires the text to be absent.
- RTF: `set_vars` stores `name=Ada` and the field is `sender` with arg `name`. Wave 18 reads `doc.vars["sender.name"]`, which is empty, so the run text is empty and `Ada` is not in the RTF.

**Next suggested step:** Opus on those two playbooks (`hide_if` true, or `sender.name`), then re-run the format gate.

**Commits:** `345065a` fix: keep RTF spacing, headings, and ODT table columns

---

## 2026-10-21 — Cursor Grok 4.7 — Fleet wave20 RTF list-table braces

**Objective:** Fix `rtf_wave20_rtf_out`. LibreOffice wrote an empty ODT because `\listtable` closed the `{\rtf1` root group. The check exited 0 when `soffice` was missing, so CI without LibreOffice could not see the bug. No `src/app/`, no `.cockpit/`, no push.

**Done:**
- `RtfWriter`: `{\leveltext...}` and `{\levelnumbers;}` sit inside `\listlevel`; `\listid` stays inside `{\list ...}`; the override table no longer adds a `}` that closed `{\rtf1`. Decimal lists use `\levelnfc0` (bullet stays 23). Font names are RTF-escaped. `\listid` and `\ls` in `\listoverride` both get the list id.
- `qa/check_wave20_rtf_out.py`: always checks balanced braces, that `{\rtf1` closes only at EOF, and that no text sits outside the root group; reimports with `document_open` and applies `odt_text_contains` to that text. LibreOffice RTF→ODT runs when `soffice` is on PATH. `--interop` (or `"interop": true` in the `.rtf.json`) fails with `soffice not on PATH: interop RTF check requires LibreOffice` instead of skipping.
- `qa/check_rtf.py` has no silent soffice skip, so it was left unchanged.
- Suite RTF opens in LibreOffice and in the native reader with Titulo RTF, English, Item um, Nome, Alpha, Nota wave20, and Pagina dois. The old list-table shape fails with `RTF root group {\rtf1 closes before end of file (text outside the root group)`.
- `scripts/qa-all.sh --gui`: **49/49** with soffice (`rtf_wave20_rtf_out` 2.16s) and again with `/run/current-system/sw/bin` removed from PATH (same test 0.43s; structure and native reimport only).

**Decisions:** The playbook ctest is not marked interop, so a machine without LibreOffice still validates structure and native reimport. Marking the check `--interop` makes a missing `soffice` a hard failure.

**Next suggested step:** Fleet `build-1` / `t5-compliance`.

**Commits:** `ef02651` fix: keep RTF list tables inside the root group

---

## 2026-10-21 — Composer 2.5 — Fleet wave20 RTF bookmark destination groups

**Objective:** Fix `rtf_wave20_rtf_out` (LibreOffice ODT almost empty): emit `\bkmkstart`/`\bkmkend` as ignorable destination groups; reader L1 for legacy loose markers; QA structural check. No push.

**Done:**
- `RtfWriter`: bookmarks as `{\\*\\bkmkstart name}…{\\*\\bkmkend name}` with RTF-escaped names; other destinations already grouped (`footnote`, `field`, `pict`, `listtable`).
- `RtfReader`: grouped `{\\*\\bkmkstart}` unchanged; loose `\bkmkstart`/`\bkmkend` consume only the name token (no document swallow).
- `qa/wave20-rtf-out.rtf.json`: requires `{\\*\\bkmkstart`.
- `scripts/qa-all.sh --gui`: **48/48** (including `rtf_wave20_rtf_out` + soffice).

**Next suggested step:** Fleet `build-1` / integrate wave20 RTF+EPUB.

**Commits:** `8ce1536` fix: emit RTF bookmarks as ignorable destination groups

---

## 2026-10-21 — Composer 2.5 — Fleet wave20 T4 formats QA harness

**Objective:** D6 — `epubcheck` in Nix, `qa/check_epub.py` / `qa/check_rtf.py`, criterion playbooks and RTF fixtures, ctest labels `epub`/`rtf`, `scripts/qa-all.sh --formats`, CI labels, RTF interop in `qa-interop.py`. No push.

**Done:**
- `shell.nix`: pinned `epubcheck` 5.3.0 alongside veraPDF.
- `qa/check_epub.py`: playbook export, epubcheck parse (0 errors + 0 warnings), zip checks (mimetype stored first, nav, lang, footnotes, hide_if omissions); sibling `.epub.json`; self-test includes a minimal EPUB that passes epubcheck.
- `qa/check_rtf.py`: aio→rtf→aio round-trip and fixture import with `.rtf.json`; hostile fixtures expect `open_error`; subprocess timeouts and `check=False`.
- Suíte: `wave20-epub-criterion` (critério de pronto EPUB), `wave20-rtf-roundtrip-criterion`, fixtures Word/LibreOffice + hostile deep/malformed RTF.
- CMake `LABELS epub|rtf`; `scripts/qa-all.sh --formats` combinable with `--pdf`; CircleCI `playbook|roundtrip|pdf|epub|rtf`; interop RTF both directions when `soffice` exists.
- `scripts/qa-all.sh --gui`: **47/47** ctest (epub/rtf label tests excluded until `--formats`). Criterion EPUB fails with `play: Unknown extension: epub` until T1; self-tests pass.

**Next suggested step:** Merge T1–T3, then `integrate-1` and `t5-compliance` (`--formats --interop` green).

**Commits:** `1874538` feat: add wave 20 EPUB and RTF format QA harness

---

## 2026-10-21 — Cursor Grok 4.7 — README banner SVG UTF-8

**Objective:** Make `docs/github/banner.svg` parse on GitHub. The file was Windows-1252, so `·`, `é`, and `á` were illegal bytes.

**Done:**
- Converted the banner with `iconv -f windows-1252 -t utf-8` and added `<?xml version="1.0" encoding="UTF-8"?>` as the first line.
- The aria-label contained a raw `0x14` (U+0014). XML 1.0 rejects that control character even after the charset conversion, so it is now a middle dot: `AI Office Writer · Tinta e Papel`.
- The only local image in `README.md` is `docs/github/banner.svg`. The badges are shields.io URLs.
- `.gitattributes`: `*.svg text working-tree-encoding=UTF-8 eol=lf`.
- `iconv -f utf-8 -t utf-8` passes, and `xml.dom.minidom` parses the SVG.

**Decisions:** Charset conversion alone cannot make the file well-formed, because U+0014 is not a Windows-1252 punctuation character.

**Next suggested step:** Merge the pull request. This does not change the product board.

**Commits:** `7569907` fix: encode the GitHub banner SVG as UTF-8 · `99518f2` docs: record the banner encoding fix in the handoff · `f8d9c88` docs: give the archived ODT change mark a change id

---

## 2026-10-20 — Cursor Grok 4.7 — Wave 20 EPUB 3 export

**Objective:** Fleet `t1-epub`. Export EPUB 3 from `document_save` (D1–D2; the XHTML writer is D3). Import stays out. Do not change `export_html` or RTF. No push. Notion sync is the later `notion-sync` node.

**Done:**
- `document_save` with a `.epub` path writes an EPUB 3 package: `mimetype` first and stored, `META-INF/container.xml`, `OEBPS/content.opf` (`dc:title` / `dc:creator` / `dc:language`, stable `urn:uuid`, `dcterms:modified`), `nav.xhtml` (heading TOC + landmarks), one XHTML file per Heading 1, CSS from styles, and images in the manifest with the right media type. Opening an `.epub` returns `{ok:false,error}` `EPUB import is not supported`.
- Tables use `<th scope="col">`. Nested lists become nested `ol`/`ul`. Notes are `<aside epub:type="footnote|endnote">` plus a `noteref`. `lang`/`xml:lang` and `dir` are set on blocks and spans. Images carry `alt`. Fields are evaluated text (`runDisplayText`). `hide_if` true and page breaks are omitted. Shapes, charts, and QR codes are PNG from the existing page painter (`paintFigureMarker` via `renderBlockPng`). OPF carries schema.org `accessMode`, `accessibilityFeature`, and `accessibilitySummary`.
- `src/core/Xhtml.{h,cpp}` is a separate writer. `export_html` is unchanged. `Zip` already stored entries, so `Zip.cpp` was not edited.
- epubcheck 5.3.0 on the proof document (H1 chapters, header table, nested list, footnote, image alt, rasterized shape and chart, RTL paragraph, foreign-language span, `if` field, `hide_if`): 0 fatal, 0 errors, 0 warnings.
- Playbook `qa/wave20-epub.json`. `scripts/qa-all.sh --gui`: 46/46 passed.

**Decisions:** The JSON model has no document id, so the identifier is UUID v5 of `title + "\n" + author` (namespace `8c0e2b1a-7d4f-4e2a-9b11-6f0a0ff1ce20`). `nav.xhtml` is in the spine with `linear="no"` so the landmarks link does not fail epubcheck RSC-011. Invalid BCP 47 tags are coerced to `pt-PT`.

**Next suggested step:** `t4-formats-qa` (epubcheck in the Nix shell and a ctest label). Then integrate with the RTF nodes.

**Commits:** `4f12fa5` feat: export EPUB 3 from document_save

---

## 2026-10-20 — Cursor Grok 4.7 — Wave 19 CodeRabbit PDF review fixes

**Objective:** Fix the seven CodeRabbit defects still open after the architect's Fleet/spec pass (PR #7, base `878c6e3`). No `src/app/`, no `.cockpit/`, no push.

**Done:**
- A paragraph with a run in another language is no longer redrawn with `pdfDrawText` in the block font. Each language slice is its own clip around glyph runs from the normal `QTextLayout`, so bold, color, size, the drop cap, and ruby match a paragraph without a foreign language. `/Span /Lang` still wraps the foreign slice. One `pieceLangs` entry covers every text object in that clip.
- Numeric `rows`/`cols` are a validation error unless `type` is `table`, including a block with no `type`. `document_open` returns `numeric rows/cols are only valid on a table block`.
- `docs/mcp-tools.md`: `copies` on `document_export_pdf`; the tagged-PDF sentence lists the wave 19 tags (`/L`, `/Table`/`/TH`/`/TD` with a repeated header, `/Figure`+`/Alt`, `/Note`, `/TOC`, `/Annot`, artifacts, `/Lang`) and the `cell.nested` limit.
- Catalog typo: "sem âncora".
- `qa/check_pdf.py`: a missing `verapdf` is `shutil.which` or `FileNotFoundError`, not `"not found"` in stderr.
- `qa/check_wave19_pdf_tags.py`: explicit `check=False`, timeouts, and exit non-zero when veraPDF UA-1 is not compliant. The proof export requests `ua: true`. The script sets headless Qt so it does not abort without a display.
- New playbook `qa/wave19-pdf-lang-runs.json`: bilingual paragraph, one bold colored run in each language, drop cap, and ruby. veraPDF UA-1, span `/Lang`, Bold and Sans in `pdffonts`, both colors in the content stream, and the same font names after `lang` is removed.
- `scripts/qa-all.sh --gui --interop --pdf`: **55/55** ctest passed. Interop ok.

**Decisions:** `/Lang` is a clip around text the layout already shaped. The integer rows/cols shortcut stays table-only.

**Next suggested step:** Fleet review of this fix, then `notion-sync` and human `approve-pr`.

**Commits:** `fix: preserve run formatting in tagged PDF language spans`

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

## Template (copiar para o topo)

```
## YYYY-MM-DD — Cursor — <título curto>

**Objective:**

**Done:**

**Decisions:**

**Next suggested step:**

**Commits:** nenhum (usuário não pediu) | `<hash>` <mensagem>
```
