# AI Handoff — AI Office Writer

> **Propósito:** log operacional entre sessões de IA. Leia este arquivo **antes** de qualquer tarefa; atualize ao **concluir** (ou ao parar com trabalho parcial).
>
> **Não duplicar:** documentação de produto fica em `DOCUMENTACAO_COMPLETA.md` e `DESIGN_CHANGES.md`.
>
> **Manutenção:** manter no máximo ~20 entradas aqui; mover as mais antigas para `AI_HANDOFF_ARCHIVE.md`.

---

## 2026-10-25 — Fleet wave24 t2-docx-out — Grok 4.7

**Objective:** DOCX export for wave 24: nested tables and merged cells (D4), captions with a native SEQ (D5), a field inside a tracked delete (D7), XML 1.0 C0 controls and `w:color` (D9), and FILLIN escaping (D11). Leave DOCX import and ODT alone.

**Done:**
- `qa/check_wave24_docx_export.py` is ctest `wave24_docx_export` and `wave24_docx_export_selftest`. It failed on the previous export, then passed. With `--interop`, LibreOffice must keep the column span, the row span, and a nested table inside the cell.
- `scripts/qa-interop.py` runs that checker with `--interop`.
- Export writes `w:gridSpan`, `w:vMerge` (`restart` / `continue`), and omits a cell covered on the same row. A nested `w:tbl` is followed by a paragraph so LibreOffice keeps the inner table inside the cell. A caption is the next paragraph, style `Caption`; a `seq` in `caption_runs` is a native `SEQ`.
- A field inside `w:del` is `w:delText` of the displayed text. The field kind stays on the revision in `word/aioffice.json`.
- `xmlEscape` drops C0 controls other than tab, LF, and CR. `w:color` is written only for six hex digits.
- Bookmark `w:id` values were already unique document-wide. A FILLIN prompt is field-quoted and XML-escaped once.
- `scripts/qa-all.sh --gui --formats` on `3a46b22`: **118** passed, **0** failed, **2** disabled (`roundtrip_foreign_odt_wave17`, `roundtrip_foreign_docx_wave17`); ctest real time **349.68 s**. The checker `--interop` passed on the same tree. Not pushed.

**Decisions:** The current importer does not read `gridSpan`, nested tables, or caption attachment (T1). This export checker reopens only what that importer already keeps. LibreOffice lifts a nested table out of the cell unless the cell ends with a paragraph after the inner `w:tbl`.

**Next suggested step:** T1 DOCX import and T3 ODT. Notion sync is the `notion-sync` node.

**Commits:** `05df4a2` test: check wave 24 DOCX export of spans, captions, and fields · `160a213` fix: export DOCX nested tables, merged cells, captions, and safe XML · `3a46b22` test: register the wave 24 DOCX check after Python is found

---

## 2026-10-25 — Fleet wave23 — Grok Build / Composer / Sonnet

**Objective:** Close Fleet wave 23 (integrity, hostile I/O, MCP stdio transport, session invariants, T4 QA/CI). Document delivery on branch `fleet/wave23-integrity-89eb4702`; no push.

**Done:**
- MCP stdio writes newline-delimited JSON (`qa/check_mcp_stdio.py` / ctest `mcp_stdio`); legacy `Content-Length` still accepted on read only.
- CLI/MCP IPC: no silent fallback to a blank local session after a connected GUI socket fails; user-scoped socket and LibreOffice convert in a private temp dir before atomic move.
- Session: undo restores `{document, path, dirty}`; `batch` rejects `document_save`/`document_open`/exports/`set_read_only`; revision ids, structural edits, `set_selection` without dirty, table cap, `document_include` revisions; export uses a copy so read-only export does not mutate the working doc; field refresh order stabilized (`if`/`stat` with real page count).
- Hostile I/O: zip inflation limits and under-declared size detection; truncated DOCX/ODT XML rejected; atomic `QSaveFile` on writes; JSON body/duplicate-id rules; captions/header-footer/EPUB id collision suffixes; EPUB image size/type limits.
- T4: audit playbook gate, `qa-all` label filter, CircleCI artifact path, `fleet-verify-merged.sh` multiline output, merge-gate/fleet adjudication `success_output`, RTF `\u` self-test (A-qa-2..6, CR-3-032, CR-9).
- Sonnet review loop (`wave23_io_hostile`, test #70) green; empty fix-pass findings on `3a52198`.
- `scripts/qa-all.sh --gui --interop --pdf --formats`: **118/118** ctest passed, **0** failed (**2** disabled: `roundtrip_foreign_odt_wave17`, `roundtrip_foreign_docx_wave17` — A-qa-1 export/reimport gap, wave 24); real time **203.50 s**; interop skipped (no LibreOffice in this run).
- CodeRabbit on PR #13: format `priorRuns` split on `split_block`; `find_replace` updates `priorRuns` inside a format revision; `document_include` remaps xref bookmark ids; `plainText()` is runs only and word/char stats use `statText()` (caption still counts, D15); field refresh skips layout when there is no `if`/`seq`/`xref`/`stat` and stats are not refreshed; LibreOffice publish uses `writeFileAtomic`; parent-directory fsync after commit; EPUB nested lists parsed per XHTML file; fleet-verify self-test uses `mktemp`.
- `scripts/qa-all.sh --gui --interop --pdf --formats` after those fixes: **123** passed, **0** failed (**2** disabled, same pair); ctest real time **170.53 s**; interop passed (docx/odt/rtf). Not pushed.
- CodeRabbit round 2 on PR #13: regex `find_replace` applies `expandRegexReplacement` in both phases (`\0`, `\&`, `\\`); the EPUB nested-list check skips navigation documents (name contains `nav`, or a manifest `properties` token `nav`); a failed LibreOffice publish counts only when fake soffice ran and the interposer logged a publish onto the destination; `O_TMPFILE` forwards its mode through `needs_mode` in `open`, `open64`, `openat`, and `openat64`. Spec and `scripts/pr-merge-gate.py` were not edited.
- `scripts/qa-all.sh --gui --interop --pdf --formats` after that round: **124** passed, **0** failed (**2** disabled, same pair); ctest real time **165.69 s**; interop passed (docx/odt/rtf). Not pushed.
- CodeRabbit round 3 on PR #13: clearing a whole format range with `find_replace` keeps one empty `TextRun` in `priorRuns`, so `revision_reject` and `revision_reject_all` still succeed. An included xref stored as a bookmark name that collides with the host is rewritten to the imported bookmark id after the id map, on runs and caption runs. A unique imported name stays as the name. `scripts/pr-merge-gate.py` and `docs/waves/` were not edited.
- `scripts/qa-all.sh --gui --interop --pdf --formats` after that round: **126** passed, **0** failed (**2** disabled, same pair); ctest real time **204.00 s**; interop passed (docx/odt/rtf). Not pushed.

**Decisions:** Foreign DOCX/ODT revision round-trip compare is hardened but full green waits on wave 24 fidelity (disabled ctests document A-qa-1). GUI backlog (A-app-*, PR #11 QML) stays Fase Humano per spec. D15 stands: captions count in statistics via `statText()`, not via `plainText()`.

**Next suggested step:** PR #13 already tracks this branch locally. Orchestrator runs `scripts/pr-merge-gate.py`; this session did not push.

**Commits:** `3ddf3fe` fix: merge gate accepts a completed CodeRabbit review with nothing to post · `62f4a21` docs: wave 23 spec and fleet (integrity, hostile input, MCP stdio) · `caaa99a` docs: update lessons from PR #12 · `5ecf90e` fix: harden wave 23 T4 QA, CI, and Fleet gates (D18) · `c4df652` fix: reject hostile I/O and write saves atomically · `3c3ad52` fix: stop MCP and CLI from writing the wrong document · `1907ee6` Fix session history, revision ranges, and field refresh · `f888867` Merge 1907ee6: session history, revision ranges, and field refresh · `07cb77d` Merge c4df652: reject hostile I/O and write saves atomically · `d1671b2` Merge 5ecf90e: harden wave 23 T4 QA, CI, and Fleet gates · `8f6dc66` fix: unblock EPUB export QA after wave 23 T2×T3 merge · `363b669` fix: tag text frames as PDF figures in reading order · `3a52198` fix: count real zip inflation and publish PDF atomically · `83f0b12` fix: atomic writes for export tools; repeat_last cannot escape the batch guard · `efce971` test: hostile I/O check no longer depends on privileges · `bbac744` test: cover CodeRabbit wave 23 integrity gaps · `36ee684` test: expect insert_break ids in the field layout playbook · `498c96d` fix: split format priorRuns on split_block · `5bd2d35` fix: keep find_replace inside format priorRuns · `f5caace` fix: remap included xref bookmark ids · `c63bf75` fix: count captions in statText and keep plainText as runs · `c65322c` fix: skip field layout when nothing needs it · `53f2369` fix: publish LibreOffice output with writeFileAtomic · `b98e853` fix: fsync the parent directory after an atomic write · `e11c988` fix: detect nested EPUB lists per XHTML file · `061ea50` fix: use mktemp in fleet-verify self-test · `54fd213` docs: record wave 23 CodeRabbit integrity fixes · `b67e915` fix: a 'Review completed' status counts only when the head is not shared · `fecf1fc` docs: wave 23 decisions match the delivered code; gate counts only same-head PRs · `165d772` test: require helper expansion for regex find_replace · `2ca7626` test: reject a nested nav when chapter lists are flat · `9e74fdc` test: require a real publish attempt and an O_TMPFILE mode · `851f607` fix: expand regex replacements with the same helper · `75fc7c3` fix: ignore navigation documents in the nested-list check · `95f66fa` fix: forward the open mode when O_TMPFILE is set · `30c9d2a` docs: record the second CodeRabbit round on wave 23 · `336bf2c` test: keep the placeConverted interposer out of child processes · `058a0fd` test: reject a format revision cleared by find_replace · `d9e812f` test: retarget an included xref that uses a colliding bookmark name · `b401bfe` fix: keep an empty prior run when a format slice is cleared · `7135558` fix: remap included xrefs that use a colliding bookmark name

---

## 2026-10-21 — Cursor Grok 4.7 — CodeRabbit PR #10

**Objective:** Fix the 8 CodeRabbit findings on PR #10 (`fix/coderabbit-outside-diff`), then the next round on head `ab116ad`. Minimal changes, no loosened tests, no push, and do not touch `.cockpit/fleets/audit-codebase.yaml`.

**Done:**
- #1 `docs/mcp-tools.md` hide_if: it is not redaction. A true expression is omitted from layout, preview, PDF, EPUB, and stats. ODT keeps the paragraph (heading and list item too) and writes `text:hidden-paragraph` (`text:is-hidden="true"` when true). An ODT image with bytes stays in `Pictures/` and is not marked hidden. DOCX keeps the paragraph and sets `w:vanish` when true; image bytes stay in `word/media/`. A DOCX table of contents is not vanished. The expression is stored in `word/aioffice.json` conditionals when JSON is embedded.
- #2 `qa/check_epub.py`: each XHTML `{xhtml}aside` counts once when `{ops}type`, split on whitespace, contains the token `footnote`.
- #3 `scripts/pr-merge-gate.py`: optional `Authorization: Bearer` from `GITHUB_TOKEN` or `GH_TOKEN`; HTTP 403/429 rate limit exits 2 with a message; comments and reviews are paged `per_page=100&page=N`; `mergeable` null or `mergeable_state` `unknown` is pending (exit 2). `docs/ORCHESTRATION.md` matches.
- #4 Read-only `document_save` allows every export extension `saveTo` writes, including `.markdown` and `.htm`, from one list (`isExportSaveExtension`).
- #5 `apply_paragraph_format` validates hide_if before `pushUndo` and before any block edit. A failure leaves the document, dirty flag, and undo stack unchanged.
- #6 The same check is in `validateBlock`, so `insert_blocks`, `replace_range`, `document_include`, and native JSON open reject an invalid expression or one that references `page`/`pages`. Nothing is changed. `document_validate` still reports `invalid_expression`.
- Playbook `qa/wave22-hide-if-ingress.json` (ctest via the existing `qa/wave*.json` glob) plus fixtures `qa/wave22-hide-if-ingress-bad.aio.json` and `qa/wave22-hide-if-ingress-syntax.aio.json`. `qa/wave22-hide-if-page.json` now expects the bad fixture to fail on open.
- `scripts/qa-all.sh --gui --formats`: **73/73** ctest passed (109.15 s).
- `python3 scripts/pr-merge-gate.py 10` (no token): exit 1, no traceback. The published head still has the 8 inline comments because this branch was not pushed.
- Import follow-up: `dropInvalidImportedHideIf` in `Session::loadFrom` after the ODT and DOCX parse, and after a `.doc`/`.ott` conversion to ODT, before the session document is replaced. `document_open` has no warnings channel, so the drop is silent. `qa/wave22-hide-if-import.json` (ctest via the `qa/wave*.json` glob).
- `scripts/qa-all.sh --gui --formats` after that import change: **74/74** ctest passed (91.05 s).
- Architect commit `ab116ad`: the page/pages check is case-insensitive, so ODT/DOCX import drops `ooow:Page` and `ooow:PAGES` the same way as `ooow:page`.
- `write_hide_import_docx` now writes `docProps/core.xml` and the content-type Override `application/vnd.openxmlformats-package.core-properties+xml`. The `.docx` was regenerated by that function only.
- Merge gate: `--since` and each `created_at` are UTC instants (`Z` or an offset such as `+02:00`). Before exit 0 the script reads the PR again; a moved head is exit 2, and mergeability is that second read. While a commit status is still pending, `unstable` or `blocked` is pending (exit 2). With every status at `success`, both stay problems. Offline `--self-test` is ctest `pr_merge_gate_selftest` (label `playbook`). `docs/ORCHESTRATION.md` matches.
- `visibleWordCount` is the one visible word count. `document_get.words` (CLI and MCP), `document_stats.words`, `integrate_help.words`, and the status bar (`AppController::words`) use it. `Document::wordCount()`, the expression identifier `words`, and the `word_count` field stay the all-blocks total so a hide_if evaluation does not recurse. `qa/wave22-outside-diff.json` expects `document_get.words` 3 beside a hidden paragraph (the old total was 6).
- `scripts/qa-all.sh --gui --formats` after this round: **75/75** ctest passed (179.22 s).

**Decisions:** Native JSON open of an invalid `hide_if` now fails, with the same message as `document_validate`. That is acceptable: the only in-repo file that used to open was the test fixture `qa/wave22-hide-if-page-bad.aio.json`; the file on disk is not rewritten; ODT and DOCX open do not use `validateBlock`, so a round-trip still opens (`w:vanish` comes back as `hide_if` `1`, which is valid). On ODT and DOCX import the same checker drops the expression instead of rejecting the file. `ab116ad` made the page/pages check case-insensitive, so `ooow:Page > 1` is dropped on import the same way as `ooow:page > 1`. An unparseable condition is dropped. A valid `flag == "on"` and `w:vanish` (`hide_if` `1`) stay. Exposed word counts go through `visibleWordCount`. `Document::wordCount()` remains the total of every block.

**Next suggested step:** Push this branch when the user asks, so PR #10 updates. Then re-run the merge gate after CodeRabbit reviews the new head. Notion sync is still pending for the orchestrator.

**Commits:** `f365d54` docs: state that hide_if is conditional hiding, not redaction · `58711ef` fix: count EPUB footnotes from parsed aside elements · `ee54b67` ci: paginate the merge gate and treat rate limits as pending · `2891efe` fix: accept .markdown and .htm as read-only export saves · `8dcb182` fix: reject invalid hide_if before any document change · `3cb034f` fix: drop invalid hide_if on ODT/DOCX import · `ab116ad` fix: treat Page/PAGES in hide_if as page-dependent · `ad1e088` fix: add the missing core.xml part to the hide_if docx fixture · `71d16d6` ci: compare merge-gate times as instants and reread before clean · `538aba5` fix: count visible words in one helper · `cbe3b8e` fix: use visible counts for word_count and expression stats

---

## 2026-10-21 — Composer — hide_if evaluated once (page=1, pages=1)

**Objective:** Closed architect decision on `fix/coderabbit-outside-diff`: `hide_if` uses `page=1`/`pages=1` everywhere; reject `page`/`pages` in hide expressions; align Docx, stats, and docs. No `src/app/`, no `.cockpit/`, no push.

**Done:**
- `Docx.cpp` `paragraphVanished` and stat visibility use `blockHiddenByExpr(..., 1, 1)` like layout/PDF/EPUB/ODT.
- `documentBlockVisibleForStat` always evaluates hide with (1,1); `document_stats` passes real `pages` only for the pages total.
- `fieldExprIdentifiers` / `hideIfExprUsesPageOrPages`; `apply_paragraph_format` and `validate` reject page-dependent hide_if with a clear message.
- Docs: `docs/waves/wave18-fields.md` D3, `docs/mcp-tools.md`, ToolDispatch prose.
- Playbook `qa/wave22-hide-if-page.json` + fixture `qa/wave22-hide-if-page-bad.aio.json`.
- `scripts/qa-all.sh --gui --interop --pdf --formats`: **82/82** ctest passed (~123 s).

**Decisions:** Stat field `pages` gets layout page count in `refreshIfFields` (not inside `documentStatCount`, avoids relayout recursion). DOCX round-trip test re-applies `flag` after open (vars are not in the embedded JSON part alone).

**Next suggested step:** Push branch when the user asks; fold into PR #6/#9 review.

**Commits:** `6274475` fix: evaluate hide_if once with page=1 and pages=1 everywhere

---

## 2026-10-21 — Composer — CodeRabbit outside-diff (PR #6/#9)

**Objective:** Fix four CodeRabbit “outside diff range” findings on branch `fix/coderabbit-outside-diff` from `b8345e5`. Tests with real expects; no `src/app/`, no `.cockpit/`, no push.

**Done:**
- `document_save` with a new `title` calls `pushUndo()` before changing `doc_.title` (undo restores the prior title).
- Read-only sessions allow `document_save` to export paths (`.epub`/`.pdf`/`.rtf`/`.md`/`.txt`/`.html`/`.doc`) without `title`; working saves and title changes stay rejected.
- `qa/check_epub.py` counts each footnote `<aside epub:type="footnote">` once; existing `footnote_aside_min` values (1/1/7) already match real aside counts.
- `document_stats` uses `documentBlockVisibleForStat` / `documentStatCount` with layout page and page count (same rules as `stat` fields); headings added to stat kinds.
- Playbook `qa/wave22-outside-diff.json` covers undo title, read-only export guard, and hidden block stats.
- `scripts/qa-all.sh --gui --interop --pdf --formats`: **81/81** ctest passed (~124 s).

**Decisions:** Export-only read-only save uses the same extension set as wave 20 exports. EPUB footnote mins unchanged because real aside counts were already 1 and 7.

**Next suggested step:** Push branch and open PR when the user asks; sync Notion if the board is updated.

**Commits:** `7202b26` fix: address CodeRabbit outside-diff findings for save, stats, and EPUB QA

---

## 2026-10-21 — Cursor Grok 4.7 — CodeRabbit wave 20 follow-up

**Objective:** Fix the five new CodeRabbit findings after `d16f7a7`. Bound same-flush footnote resolution. Keep hyperlink character formatting, re-encode non-JPEG pictures, mark a new export title dirty, and document `.doc` as a LibreOffice export. No `src/app/`, no `.cockpit/`, no push.

**Done:**
- #0: `resolveNotes` collects note ids from `body[start..]` once, records tempId to finalId while adding footnote blocks, and runs one `replaceNoteIds`. A failed `addBlock` still rewrites the ids already assigned. `rtf_footnote_flush` rejects a 100000-note paragraph with the block-limit error and opens a 20000-note table; the test finished in 2.02 s. `rtf_footnote_scale` (8000 notes, one per paragraph) still passes.
- #1: `\fldrslt` wraps the result in the run's character properties. Round trip: hyperlink and page number in bold and color (`qa/wave20-rtf-hyperlink.json`).
- #2: non-JPEG bytes are decoded and saved as PNG before `\pngblip`. Decode or encode failure warns once and writes the alt text. GIF and BMP fixtures round-trip as PNG (`qa/wave20-rtf-pict.json`).
- #3: `document_save` with a new `title` calls `markChanged` before the write. Only `.aio.json`, `.odt`, and `.docx` clear dirty. `qa/wave21-export-path.json` expects `document_new` to refuse the unsaved title.
- #4: ToolDispatch and `docs/mcp-tools.md` name `.doc` as an export-only LibreOffice Word 97 write. `mcp_doc_export_prose` checks both.
- `scripts/qa-all.sh --gui --interop --pdf --formats`: **80/80** ctest passed (140.39 s). Interop ok.

**Decisions:** `.doc` stays export-only; the playbook does not call soffice. A title marks dirty only when the string changes. On the block cap, ids already added are rewritten before the reader returns. Every non-JPEG, including a valid PNG, is re-encoded. Nested note ids that are not in this flush's map stay until a later flush.

**Next suggested step:** Push is still waiting on the user. Sync the review fixes to Notion when the board is updated.

**Commits:** `6dcc356` fix: bound RTF note resolution and keep export titles dirty

---

## 2026-10-21 — Cursor Grok 4.7 — CodeRabbit wave 20 EPUB/RTF review

**Objective:** Fix all 18 CodeRabbit findings on PR #9 (wave 20). Working saves stay `.aio.json`, `.odt`, and `.docx`. EPUB, PDF, Markdown, text, HTML, and RTF are exports. No `src/app/`, no `.cockpit/`, no push.

**Done:**
- #15: `saveDocument` updates `path_` and clears dirty only for `.aio.json`, `.odt`, and `.docx`. The other extensions write the file and return ok or warnings.
- #16: RTF export warnings reach `document_save` `warnings[]`.
- #9: `\uN` is a signed 16-bit value. The round trip covers CJK (U+8000), Hangul, and an emoji surrogate pair.
- #10 / #11: a hyperlink run with an empty field exports as `HYPERLINK`. JPEG uses `\jpegblip`. `\picw` / `\pich` are pixels and `\picwgoal` / `\pichgoal` are twips.
- #12 / #13 / #14: `\deff0` is Liberation Serif. Each cell starts with `\pard\intbl`. A table ends with `\pard` and the round trip does not add an empty paragraph after it.
- #8: footnote import keeps a running counter and scans the body only from the last resolved index, plus the open row and table buffers. The reader assigns ids locally so `Document::assignId` does not rescan the whole body for every block.
- #4: notes are emitted in first-reference order. Each use vector is copied before writing, so the map is not mutated during iteration.
- #5: heading levels are clamped to at most previous+1 on a copy before `nav.xhtml`. Chapter heading tags stay as authored. epubcheck is 0/0 for a skip (H1, H3, H2) and for a document that starts at H2.
- #6: an ODT row wider than 256 columns fails with `ODT table row is wider than 256 columns` before the package is written.
- #7: `renderBlockPng` clears the caption on a copy of the block and sizes an image from its aspect ratio, including crop and 90/270 rotation.
- #3: `isBcp47` turns underscores into hyphens and accepts a singleton when a subtag follows (`zh-Hant-TW`, `pt_BR`, `en-x-custom`). Invalid tags still fall back.
- #2: `qa/check_wave20_rtf_out.py` runs `soffice` with `-env:UserInstallation=file://<tmp>`.
- #0 / #1 / #17: `docs/mcp-tools.md` describes native RTF import. D1 records a UUID v5 of title, newline, and author (the JSON model has no document id). `document_save` says EPUB splits on Heading 1 with optional front matter, and which extensions are exports.
- `scripts/qa-all.sh --gui --interop --pdf --formats`: **76/76** ctest passed (139.25 s). Interop ok (docx↔odt, fields, our RTF→ODT, LO RTF→native). `rtf_footnote_scale` (8000 notes, 8 s cap) passed in 0.62 s.

**Decisions:** `.doc` stays an export. PDF save still does not return PDF warnings. The table trailer is `\pard` only; `pushTable` does not set `suppressEmptyPar_`. A note aside is written only in the chapter that owns the note. Caption clearing applies only inside `renderBlockPng`. Liberation Serif is registered before the style scan so `\deff0` stays index 0.

**Next suggested step:** Push is still waiting on the user. Sync the review fixes to Notion when the board is updated.

**Commits:** `90c3f5f` fix: keep exports off the working path and repair EPUB and RTF

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

## Template (copiar para o topo)

```
## YYYY-MM-DD — Cursor — <título curto>

**Objective:**

**Done:**

**Decisions:**

**Next suggested step:**

**Commits:** nenhum (usuário não pediu) | `<hash>` <mensagem>
```
