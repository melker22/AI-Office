# AI Handoff archive — AI Office Writer

Entradas antigas de `docs/AI_HANDOFF.md` (manter o arquivo vivo com ~20 itens).

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
- `Odf.cpp` export: a delete is `<text:change text:change-id="change-1"/>` at `start`, plus a changed-region whose `<text:deletion>` holds change-info and `deletedRuns` as a formatted `<text:p>`. A format revision is `text:change-start/end` on the range plus `<text:format-change>`. The comment that called a real deletion mark impossible is gone.
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

## 2026-10-09 — Cursor Grok — Wave 16: RTL justify hangs the trailing space

**Objective:** The previous `setPosition` only moved the gap from the right edge to the left. Stretch non-final RTL justified lines so the glyphs fill the measure and the trailing space does not consume it. Restore the playbook's `$lte: 20.5`.

**Done:**
- After the normal break, and still inside `beginLayout`, each non-final RTL justified line that ends in spaces is re-laid with `QTextLine::setNumColumns(textLength, width + sw)`. Same characters, no rebreak. `sw` is the inset `line.width() - cursorToX(start)` (4.000 px / 1.058 mm, equal to the space advance).
- Qt 6.11 measurement: that wider budget cancels `leadingSpaceWidth`, so carets already span the original measure. `setPosition(x - sw)` then clamps the left edge and moves the right edge back to 188.75 mm, so the line is left where `setNumColumns` placed it.
- Playbook offset 114 (caret on the content side of the trailing space) is 20.00 mm (`$lte: 20.5` restored). Offset 0 is 189.81 mm (`$gte: 189.25` unchanged).
- `scripts/qa-all.sh --gui` green, including `qa/wave15-pdf-tagging.json`. D11 text unchanged.

**Decisions:** Do not slide the line by `-sw` on this Qt. The extra width is already consumed by the justification budget.

**Next suggested step:** Fleet review.

**Commits:** `8a4a035` fix: justify RTL lines on a width that includes the trailing space

---

## 2026-10-09 — Cursor Grok — Wave 16: bidi hit-test and RTL justify space

**Objective:** Close the two `scripts/qa-all.sh --gui` failures: bidi boundary hit-test (D11) and the short right edge of a justified RTL line. Do not touch `src/app/` or the paint order / one clip per block (D10).

**Done:**
- `DocumentLayout::hitTest` (text only) keeps `xToCursor`, then if that offset's own caret is more than 0.5 px from the click, uses the grapheme boundary on the same line whose `cursorToX` is closer.
- Non-final RTL justified lines are shifted with `QTextLine::setPosition` inside `blockTextLayout`, so paint, caret, and hit-test share the offset. D11 is recorded in `docs/waves/wave16-ctl-cjk.md`.
- `scripts/qa-all.sh --gui` green, including `qa/wave15-pdf-tagging.json`.

**Decisions:** The RTL shortfall is the trailing space. On the playbook line, `line.width() - cursorToX(start)` is 4.000 px (1.058 mm), equal to `horizontalAdvance` of U+0020; LTR justify already reaches page x 189.81. Shifting by that gap puts offset 0 at 189.81 mm. Offset 114 is the caret before that space, so it moves from 20.00 to 21.06; its expect is now `$lte: 21.6`. The right-edge bound `$gte: 189.25` is unchanged.

**Next suggested step:** Fleet review of this diff.

**Commits:** `8d21670` fix: snap bidi hit-test to the caret and hang RTL justify spaces

---

## 2026-10-09 — Composer — Wave 16 T6 QA playbooks + Noto fonts

**Objective:** Fleet `t6-qa`: deterministic fonts in `shell.nix` (D9) and MCP playbooks for wave 16 ready criteria (D8): caret/CJK/justify/RTL PDF; keep `wave15-pdf-tagging` unchanged.

**Done:**
- `shell.nix`: `noto-fonts`, `noto-fonts-cjk-sans`, `FONTCONFIG_FILE` via `pkgs.makeFontsConf` (IBM Plex, Source Serif, JetBrains Mono, Noto).
- Playbooks: `qa/wave16-ctl-caret.json`, `qa/wave16-ctl-cjk.json`, `qa/wave16-ctl-justify.json`, `qa/wave16-ctl-pdf-rtl.json` with `expect` and `$gt`/`$lt`/`$gte`/`$lte`.
- `scripts/qa-all.sh --gui`: `wave15-pdf-tagging`, CJK, model, roundtrip, PDF RTL pass.

**Decisions:** Did not loosen expects on real failures (per spec).

**Next suggested step:** Fleet `review`: fix `hit_test` at bidi boundary in `abc אבג def` (offset 7 → got 4); justified Arabic first-line span ~168.75 mm vs 170±0.5 mm (offset 0 `x_mm` 188.75, expect ≥189.25).

**Commits:** `5d8476c` test: add wave16 CTL/CJK QA playbooks and Noto fontconfig

---

## 2026-10-09 — Cursor — Wave 16 integrate T4+T5 merge

**Objective:** Merge `t4-wire-rtl` and `t5-roundtrip` into integration branch; resolve conflicts; QA.

**Done:**
- Fast-forward merge of `t4-wire-rtl`; merge commit for `t5-roundtrip` with conflict resolution (keep T3/T4 `normalizedBlockDir`, `auto` clearing in `apply_paragraph_format`, richer MCP schema for align/dir).
- `scripts/qa-all.sh --gui` verde (incl. `qa/wave16-ctl-roundtrip.json`).

**Decisions:** T5’s raw `dir` in JSON import superseded by `normalizedBlockDir` from T3. Conflitos T5 vs T3/T4 em `Session.cpp`, `ToolDispatch.cpp`, `Document.{h,cpp}`, `AI_HANDOFF.md` — mantida semântica T3/T4 (auto→vazio, schema align/dir, `normalizedBlockDir`).

**Next suggested step:** Fleet `t6-qa` (playbooks caret/CJK + fontes Nix).

**Commits:** `0f10fdc` merge: integrate wave16 t4-wire-rtl and t5-roundtrip

---

## 2026-10-09 — Composer — Wave 16 T5 dir/align DOCX/ODT round-trip

**Objective:** Fleet `t5-roundtrip` (spec D7): mapear `Block.dir` e `align` lógico (`start`/`end`) na importação/exportação DOCX e ODT; QA e playbook.

**Done:**
- Modelo: campo `Block.dir` (`auto`|`ltr`|`rtl`) no JSON/MCP (`apply_paragraph_format`, schema).
- `Docx.cpp`: export/import `w:jc` start/end; `w:bidi` ↔ `rtl`, `w:bidi w:val="0"` ↔ `ltr`; `w:rtl` em run ignorado na leitura.
- `Odf.cpp`: estilos automáticos com `fo:text-align` e `style:writing-mode` (`rl-tb`/`lr-tb`); import via `paragraph-properties`.
- `qa/check_roundtrip.py`: confere `dir`/`align` nos JSON reabertos; `qa/wave16-ctl-roundtrip.json` com `expect`.
- `scripts/qa-all.sh` verde.

**Decisions:** DOCX usa `w:bidi w:val="0"` para `dir=ltr` explícito (só `rtl` omitiria o atributo no reimport).

**Next suggested step:** Fleet `integrate-2`: merge com branch `t4-wire-rtl` (layout/caret) e `t6-qa` (playbooks caret/CJK + fontes Nix).

**Commits:** `ba208c3` feat: round-trip Block.dir and logical align in DOCX and ODT

---

## 2026-10-09 — Cursor Grok — Wave 16 T4: dir, ruby, caret_move

**Objective:** Wire `Block.dir` into `BlockTextLayout`, place ruby from the real line, and expose visual and grapheme caret movement in the core and as MCP `caret_move`. Do not touch `src/app/`. Do not implement kashida by hand. Keep one clip per block and `orderedBlocksForPage`.

**Done:**
- `blockTextLayout` calls `QTextOption::setTextDirection`: `rtl` → `RightToLeft`, `ltr` → `LeftToRight`, `auto` (empty) → `LayoutDirectionAuto`. `align` `start`/`end` map to the resolved direction (explicit dir, or the first strong character when auto). `left`/`right` stay physical via `Qt::AlignAbsolute`.
- Ruby is centered on the line that contains the run, between `cursorToX` of the run's display start and end. A wrapped run is clamped to that line. A block split across pages only draws ruby for lines inside the fragment.
- `DocumentLayout::caretMove` and MCP `caret_move {id, offset, move: left|right|next|prev}` → `{offset}`. `left`/`right` use `leftCursorPosition`/`rightCursorPosition`. `next`/`prev` use `nextCursorPosition`/`previousCursorPosition` (`SkipCharacters`). Documented in `docs/mcp-tools.md`. Canvas keys are not wired.
- `scripts/qa-all.sh --gui` passed, including `qa/wave15-pdf-tagging.json` and `qa/wave16-ctl-model.json`.

**Decisions:** Empty `align` stays physical left, same as an explicit `left`. A `dir: rtl` paragraph sits on the right when `align` is `start` (probed: «مرحبا» offset 0 at 189.8 mm, the end of that short word still on the right). `align: left` with `dir: rtl` stays on the left margin. Kashida was not added.

**Kashida (Qt 6.11.1, observed, not implemented):** `QTextEngine::justify` still has a kashida path (`nKashidas`, and `QFontEngine::getGlyphPositions` emits U+0640 for an RTL item when that count is non-zero). This Qt never assigns the classes that arm it (`Justification_Arabic_Normal` and above). Arabic runs are marked `Justification_Character` or `Justification_Arabic_Space`, so the extra width is inter-character and space advance. With Noto Naskh Arabic, Noto Sans Arabic, and DejaVu Sans, a non-final justified line grew toward the full measure and `glyphRuns` gained no U+0640 versus the same line aligned right. In the app, a justified Arabic paragraph's first-line carets run from the left margin (20.00 mm) to 188.49 mm; the content box is 20–190 mm.

**Next suggested step:** Round-trip of `dir` / `align start|end` (T5) and the wave 16 playbooks. A hit on the caret x of offset 4 in `abc אבג def` (the LTR/RTL boundary) came back as offset 7; the other sampled offsets round-tripped. Do not wire `DocumentCanvas` keys in the AI phase.

**Commits:** `e069582` feat: apply paragraph direction, ruby cursors, and caret movement

---

## 2026-10-09 — Cursor Grok — Wave 16 layout core (BlockTextLayout, caret, hit-test)

**Objective:** D2+D3+D4 (caret/hit) and D8 tools: one function builds a block's QTextLayout for measurement and painting, caret and hit-test use real line geometry, and MCP exposes `caret_geometry` and `hit_test`. Do not touch `Block.dir` or the paint order / single clip per block (D10).

**Done:**
- `blockTextLayout` builds display text, formats, `QTextOption`, already-broken lines, and the model ↔ display offset map (soft hyphens, rendered fields, drop-cap skip, and the page-fragment skip of a block split across pages).
- `measureTextHeight` and `paintPage` both call it. Painting still uses one `setClipRect` per block and `orderedBlocksForPage`.
- `DocumentLayout::caret` and `hitTest` use `QTextLine::cursorToX` / `xToCursor` for body text. Table cells stay on the previous linear placement.
- MCP tools `caret_geometry` and `hit_test` follow `{ok:false,error}` and are documented in `docs/mcp-tools.md`.

**Decisions:** The tools lay out at zoom 1 (96 dpi) so `x_mm`/`y_mm` are a shared millimetre space. `rtl` is the glyph run at the caret, not `Block.dir` (that field is a parallel task). A split block is still painted as the full layout clipped by `layoutY0`; `pageSkipModel` records where the visible fragment starts.

**Next suggested step:** Wire `Block.dir` into this layout, then ruby and `caret_move` (T4). Do not change the clip or paint order.

**Commits:** `6a74100` feat: share one block text layout for paint, caret, and hit-test

---

## 2026-09-14 — Claude — PR #2 CodeRabbit follow-up round 2 (same branch)

**Objective:** Verificar todos os achados do CodeRabbit na PR #2 (2 reviews, 9 comentários) contra o código atual e resolver os 2 que ainda não tinham commit corretivo (achados na review do commit `3b86267`, sem review seguinte).

**Done:**
- Conferido nos arquivos reais (não só na mensagem "✅ Addressed"): os 7 achados da 1ª review (hash em `AI_HANDOFF.md`, exceção de push só com pedido explícito, `check_roundtrip.py` w:ins/w:del separados, bookmark sintético para `insert_crossref`, `[Content_Types].xml` após footnote/endnote, dedup de bookmark por nome normalizado, dedup de revisão com `blockId`) — todos genuinamente corrigidos em `3b86267`.
- Os 2 achados da 2ª review (ainda sem commit corretivo) eram reais e foram corrigidos agora:
  - `Revision` ganhou campo `deletedRuns` (fragmentos `TextRun` originais, com formatação) preenchido em `insertAtCaret`/`deleteBackward` quando `track_changes` está ligado; `revisionReject`/`revisionRejectAll` restauram esses fragmentos (preservando bold/itálico/etc.) em vez de um `TextRun` genérico só com o texto.
  - `revisionReject` (rejeição individual) agora ajusta `start`/`end` das revisões remanescentes do mesmo bloco pelo delta da rejeição — corrige o exemplo do CodeRabbit (inserir "X" em 0, apagar "b" em 2, rejeitar o insert e depois o delete dava "acb"; agora dá "abc"). `revisionRejectAll` não precisava do mesmo ajuste (já é consistente por processar em ordem cronológica reversa).

**Decisions:** `deletedRuns` é campo novo só de persistência nativa (JSON), não é exportado para markup ODT/DOCX — o objetivo é fidelidade do reject, não fidelidade de formato externo (esse gap de "w:del sintético" continua documentado como fora de escopo, decisão da entrada anterior).

**Next suggested step:** ~~PR #2 pronta para merge~~ — **corrigido na entrada seguinte**: CodeRabbit rodou de novo em cima deste commit e apontou mais 4 achados (a validação e a re-review ainda não tinham terminado quando esta linha foi escrita originalmente).

**Commits:** `26f3fb6` fix: preserve formatting and offsets on revision reject (CodeRabbit PR #2 round 2)

---

## 2026-09-14 — Claude — PR #2 CodeRabbit follow-up round 3 (same branch)

**Objective:** A entrada anterior declarou "CI verde, PR pronta para merge" antes da CI/CodeRabbit terminarem de rodar em cima do commit `ad638fb` — o CodeRabbit apontou isso (não declarar pronto antes da validação terminar) e mais 3 achados reais na 3ª review. Corrigir os 4.

**Done:**
- `docs/AI_HANDOFF.md`: linha "Next suggested step" da entrada anterior corrigida (não afirmar mais "pronta para merge" antes da validação real terminar).
- `qa/revision-reject-fidelity.json` → renomeado para `qa/wave16-revision-reject-fidelity.json` (o glob da CI é `qa/wave*.json`; o arquivo antigo nunca teria rodado na suíte automática). Playbook ganhou `expect` de verdade (texto final `abc`/bold em `b`, `revision_list` final vazio após `revision_accept_all`) em vez de só confiar no `ok` de cada chamada.
- `aioffice play` (`src/cli/Cli.cpp`) ganhou suporte real a asserção: uma chamada pode incluir `"expect": {"dot.path": valor}`; falha do mesmo jeito que `ok:false` se o valor não bater. Documentado em `docs/mcp-tools.md`. Sem isso o achado do CodeRabbit sobre "só checa ok" não tinha como ser corrigido de verdade nesse playbook nem em nenhum outro.
- `Revision::fromJson` (`Document.cpp`): `deleted_runs` só aceita elementos que são objeto JSON; se a reconstrução do texto dos fragmentos não bater com `Revision.text` (arquivo antigo/corrompido), `deletedRuns` é limpo e o fallback de texto plano em `revisionReject`/`revisionRejectAll` continua funcionando.
- Achado mais profundo (Major, "heavy lift"): o ajuste de offset que adicionei na rodada anterior só corrigia revisões **depois** de um reject; uma revisão ainda não rejeitada podia ficar desatualizada por causa de uma edição **posterior** (ex.: rev1 insere "X" em 0, rev2 insere depois "Y" também em 0 — rejeitar rev1 removia "Y", não "X"). Resolvido na raiz: `Session::shiftRevisionOffsets(blockId, pivot, delta)` (novo, privado) agora roda em **todo** ponto de mutação rastreada (`insertAtCaret`, `insertTextAt`, `deleteBackward`, e o próprio `revisionReject`), não só no reject — toda revisão existente no mesmo bloco fica sempre com offset válido em relação ao documento atual, eliminando a necessidade de "rebasear" nada na hora do reject.
- Testado manualmente o cenário exato do CodeRabbit (rev1 insere X em 0, rev2 insere Y em 0, reject rev1 → sobra "Yabc", não "Xabc" invertido) via playbook solto — confirma a correção.

**Decisions:** Não estendi o ajuste de offset ao `commitCaretInsert`/autocorrect do lado GUI (path de digitação ao vivo) — esse caminho já não gravava revisão nenhuma pro replace do autocorrect antes desta rodada, é um gap pré-existente e separado, não foi um dos achados do CodeRabbit.

**Next suggested step:** Esperar a 4ª rodada de CI + CodeRabbit no commit deste fix. Se limpo, PR #2 finalmente pronta para merge.

**Commits:** `a21b733` fix: rebase revision offsets on every tracked edit, harden deleted_runs parsing, add real playbook assertions (CodeRabbit PR #2 round 3)

---

## 2026-09-14 — Grok — PR #2 CodeRabbit follow-up (same branch)

**Objective:** Consertar os Majors da review da PR #2 na mesma branch.

**Done:**
- `[Content_Types].xml` montado depois dos overrides de footnotes/endnotes.
- Dedup de bookmark compara o `w:name` normalizado da exportação.
- Dedup de revisão no `flushP` usa `blockId+kind+start+end`.
- `insert_crossref` sem Bookmark ganha bookmark sintético no DOCX (REF/PAGEREF).
- Backspace com track changes grava o caractere real, não `"?"`.
- `check_roundtrip.py` exige `w:ins` só se houver insert; `w:del` se houver delete.
- Guia de PR: push no `main` só com pedido explícito.

**Decisions:** `w:del` sintético no export continua fora (texto já saiu de `runs`).

**Next suggested step:** Esperar CI + re-review na PR #2; merge quando verde.

**Commits:** `c853456` fix: CodeRabbit PR #2 follow-up on DOCX round-trip and delete text

---

## 2026-09-14 — Grok — Branch+PR workflow as project rule; land CI/CodeRabbit work via PR

**Objective:** Tornar branch→PR→`main` a regra do repo, deixar um guia copiável, e enviar as alterações da CI/CodeRabbit pela forma recomendada (não push no `main`).

**Done:**
- Regra `.cursor/rules/pr-not-direct-push.mdc` (`alwaysApply`); `ai-commit-protocol.mdc` e `project-context.mdc` apontam para ela.
- Guia portátil `docs/GIT_PR_WORKFLOW.md` (PT-BR, para copiar a outros projetos).
- Envio: branch `fix/ci-cli-and-coderabbit` + PR para `main`.

**Decisions:** A PR #1 (`main` → `pre-wave12-baseline`) continua sendo um recorte histórico, não o fluxo normal.

**Next suggested step:** Esperar CI + CodeRabbit nesta PR; merge quando verde.

**Commits:** `fade1bd` ci: pin nixpkgs, add aioffice-cli, run smoke tests headless · `ceae108` fix: address CodeRabbit findings on CSL, PDF, and round-trip · `ff892b6` docs: require feature-branch PRs instead of pushing main

---

## 2026-09-14 — Grok — CI: pin nixpkgs, offscreen, hyphen resource, cli target

**Objective:** Fazer a CircleCI passar de verdade. O X vermelho depois do `a33af05` não era um segundo bug escondido nas ondas 12–15 — era Qt local 6.11 vs CI 6.7 (`QImage::flipped()`, já corrigido) + o job nunca terminar o alvo GUI (108 targets) no `large`, então a suíte de fumo nunca rodava. CodeRabbit apontou o `hyphen.qrc` em lib estática.

**Done:**
- `flake.nix` / `shell.nix` pinados no mesmo `flake.lock` (nixpkgs `2c423e03bbaf…`, Qt 6.11.1). Sem `<nixpkgs>` solto.
- `Q_INIT_RESOURCE(hyphen)` em `Hyphenation.cpp` (puxa o rcc da lib estática) e em `main.cpp`.
- `QT_QPA_PLATFORM=offscreen` quando `AIOFFICE_HEADLESS` ou Linux sem DISPLAY/Wayland, **antes** do `QGuiApplication`.
- Alvo `aioffice_cli` (`aioffice-cli`): core + main headless, sem QML. A CI builda só isso, cacheia `build/linux` + ccache, e corre os playbooks. GUI continua local.

**Decisions:** A CI não tenta mais o alvo GUI — era o que o plano matava (~8 min) antes do `save_cache`. O gate é o mesmo `play` que a IA usa.

**Next suggested step:** Push para a CircleCI confirmar o job verde. GUI compile check, se quiser, é um job à parte depois.

**Commits:** nenhum (usuário não pediu)

---

## 2026-09-14 — Grok — CodeRabbit follow-ups (waves 12–15)

**Objective:** Corrigir os achados relevantes da review da PR #1.

**Done:** `bibliography_set` não zera entradas no `cite_style` só; sort da biblio por sobrenome parseado; MLA in-text sem `e.pages`; heading number sem componente 0; PDF append+check write, `/P` na estrutura, export propaga erro de pós-processo; ODT `field:arg`; DOCX `w:id` numérico, nomes de bookmark no REF/anchor, PAGEREF, merge nativo+json; `revision_reject` restaura delete; `mcp-tools.md` e `check_roundtrip.py` nativo.

**Decisions:** Markup `w:del` sintético (reinserir texto apagado no export) continua fora — o modelo tira o texto de `runs` na hora. Docstring 80% ignorado.

**Next suggested step:** Merge da PR desta branch quando a CI estiver verde.

**Commits:** `ceae108` (CodeRabbit follow-ups; CI commits na entrada acima)

---

## 2026-09-14 — Claude Code — Fase IA onda 15: tagged PDF real de parágrafo (cumprindo a onda 13)

**Objective:** Onda 15 — cumprir o compromisso deixado explícito na onda 13/catálogo: tagging de PDF de granularidade de parágrafo/heading, não mais só de página. Push pedido pelo utilizador antes desta onda (feito, `a33af05`). Sem chrome QML novo.

**Done:**
- Descartei a ideia inicial de "gerador de PDF do zero com fonte embutida própria" (viável mas enorme — precisaria reimplementar font embedding via `QRawFont::fontTable()`) depois de descobrir, testando empiricamente (`/tmp/tag_probe*.json` + inspeção do content stream decodificado), que dá pra recuperar os limites de cada bloco **de volta do content stream que o próprio `QPdfWriter` já escreve**: cada bloco passa por exatamente um `p->setClipRect(lb.rect...)` (`Preview.cpp`), que o Qt sempre traduz numa sequência `m...l...l...l h W* n` que não aparece em mais nada (tabelas/imagens usam outros operadores — confirmado testando um documento misto).
- `src/core/PdfPostProcess.cpp` ganhou: tokenizador de content stream (`tokenize`), busca de limites de bloco (`findClipBoundaries`) e de objetos de texto (`findTextSpans`), correlação bloco↔BT/ET por ordem (usando a mesma função `orderedBlocksForPage` que `paintPage` usa, extraída pra ficar impossível as duas ordens divergirem), inflate/deflate de content stream via zlib direto (`inflateObjStream`/`flateCompress`).
- Cada `BT...ET` embrulhado com `/<Tag> <</MCID n>> BDC ... EMC`; parágrafos com várias linhas (várias `BT...ET`) viram **um** elemento de estrutura com várias entradas em `/K` — confirmado com `qpdf --show-object`.
- **Rede de segurança**: só aplica o tagging fino numa página se a contagem de limites de bloco encontrados bater exatamente com a contagem esperada; senão cai pro `/Part` de página inteira da onda 13, nunca arriscando uma tag errada. Testado com um documento heading+parágrafos+tabela+lista: a tabela foi corretamente ignorada (não gera limite de bloco), sem quebrar nada.
- Refatorada a função de post-processing da onda 13 (`pdfPostProcessAddExtras`) pra tirar a responsabilidade de structure tree (agora só cuida de anotações) — a nova `pdfPostProcessTagStructure` cuida de tudo relacionado a `/MarkInfo`/`/StructTreeRoot`/`/StructParents`/`/ParentTree`.
- Validação extensa com `qpdf --check` + `qpdf --show-object` (biblioteca independente, não é código nosso): parágrafo simples (3 elementos, MCIDs corretos), parágrafo com quebra de linha (1 elemento, `/K [0 1 2]`), documento misto com tabela (5 elementos corretos, tabela ignorada), regressão completa das ondas 3–14.
- Catálogo e Notion atualizados: item "PDF etiquetado — tagging de parágrafo" (criado na onda 13 como Todo explícito) marcado Done.
- Playbook `qa/wave15-pdf-tagging.json`.

**Decisions:** Cobertura fica em parágrafo/heading/item de lista (a maioria do conteúdo de prosa) — tabelas/imagens/notas/TOC ficam pro fallback de página por enquanto, não é regressão (mesmo comportamento da onda 13), só ainda sem granularidade. Gerador de PDF do zero fica como caminho reservado, não usado, caso cobrir tabelas/imagens algum dia exija.

**Next suggested step:** Onda 16 (CTL/CJK completo) ou ampliar a cobertura de tagging pra tabelas/imagens/notas, conforme o utilizador priorizar.

**Commits:** `b580c38` feat: real paragraph/heading PDF tagging, fulfilling the wave 13 commitment

---

## 2026-09-14 — Claude Code — Fase IA onda 14: formatador CSL real + CircleCI

**Objective:** Onda 14 (processador CSL). Aproveitando, escrever e configurar CI no CircleCI para o projeto. Sem chrome QML novo. Sem push.

**Done:**
- Pesquisado antes de começar: não existe biblioteca CSL C/C++ embutível com licença compatível (`citeproc-rs` do Zotero é Rust sem FFI-C viável; `citeproc-java` é JVM). Decisão: formatador real (não CSL-XML genérico), `src/core/Csl.h/.cpp`.
- `BiblioEntry` (Document.h) ganhou campos reais: `type`, `container_title`, `publisher`, `city`, `volume`, `issue`, `pages`, `edition`, `editor`, `doi`, `url`, `accessed`. `Document.citeStyle` (apa\|abnt\|mla\|chicago), documento-wide.
- 4 estilos × 4 tipos de referência (book/article-journal/webpage/chapter), citação in-text (`insert_citation`, via `csl::inText`, recalculada a cada leitura tal como `page_number`) e bibliografia (`insert_bibliography`, via `csl::bibliographyEntry`, agora em múltiplos runs com itálico real, recuo hanging, ordenada por autor+ano). Regras conferidas contra convenções reais de cada estilo — 2 bugs achados e corrigidos durante o teste: separador `&`/`and` errado com 2 autores, e ponto duplicado (`"R.."`) no Chicago quando iniciais já terminam em ponto.
- `bibliography_set` aceita `cite_style` e todos os campos novos.
- Reforçada a documentação do compromisso da onda 13 (tagging de parágrafo em PDF): linha própria no Notion ("PDF etiquetado — tagging de parágrafo", Fase IA, Todo, onda 15) + nota explícita em `WRITER_FEATURE_CATALOG.md` deixando claro que é compromisso, não "se der".
- `.circleci/config.yml`: builda com o mesmo `nix-shell` do README, corre todos os `qa/wave*.json` (exceto o template `wave1-foundation.json`) como suíte de fumo headless. Validado com `mcp__circleci__validate_config` (`valid: true`). Projeto ainda não tem pipeline no CircleCI (`list_runs` confirma "project not found") — falta o utilizador clicar "Set Up Project" no dashboard (repo privado, ação única ligada à conta).
- Playbook `qa/wave14-csl.json`. Regressão: ondas 3–13 continuam `ok:true`.

**Decisions:** Arquivo `.csl` arbitrário fica fora (sem biblioteca C/C++ pronta) — ampliar pra mais estilos/tipos só quando o utilizador pedir, não é compromisso como o tagging de parágrafo. `insert_citation` muda de formato (antes `Autor (Ano)` sem vírgula/parênteses externos, que não era um formato real de nenhum estilo — agora `(Autor, Ano)` de verdade).

**Next suggested step:** Onda 15 (gerador de content stream PDF próprio, tagging de parágrafo — já compromissado) ou onda 16 (CTL/CJK completo). Utilizador precisa clicar "Set Up Project" no CircleCI antes do primeiro pipeline rodar.

**Commits:** `7c34200` feat: real CSL citation formatter for APA, ABNT, MLA, and Chicago · `b7b1518` ci: add CircleCI build + qa/wave*.json smoke-suite pipeline

---

## 2026-09-14 — Claude Code — Fase IA onda 13: anotações PDF reais + tagged PDF de página

**Objective:** Onda 13, mapeada no fim da onda 12: fidelidade de PDF — anotações reais para comentários (substituindo a página extra de texto) e PDF etiquetado, via pós-processador de PDF (confirmado antes de começar: `QPdfWriter` não tem API pra nenhum dos dois). Sem chrome QML novo. Sem push.

**Done:**
- `src/core/PdfPostProcess.h/.cpp`: pós-processador que aplica uma **atualização incremental de PDF** (mecanismo padrão do formato — acrescenta objetos + xref + trailer no fim do ficheiro já escrito pelo `QPdfWriter`, sem tocar nos bytes originais) para injetar anotações e a árvore de estrutura.
- `document_export_pdf {comments:true}` agora produz anotações `/Subtype /Text` reais (clicáveis, com autor e texto), posicionadas no bloco/página certos, reaproveitando o `/Annots` vazio que o `QPdfWriter` já aloca por página. Funciona com `copies` (repete a anotação em cada cópia física, testado com 2 cópias × 2 páginas = 4 anotações corretamente pareadas) e com `from`/`to`. Em `booklet` cai de volta pra página extra de texto (posição ambígua com 2 páginas lógicas por folha física).
- Todo PDF nativo sai com `/MarkInfo <</Marked true>>` + `/StructTreeRoot` (`/Document` → um `/StructElem /Part` por página) — tagging real e válido, mas de granularidade de **página**, não de parágrafo/heading: `QPainter`/`QPdfWriter` não dão nenhum jeito de injetar `BDC`/`EMC`+`MCID` nos content streams que geram, então os elementos de estrutura não têm `/K` de conteúdo. Documentado como limite honesto (não silenciado).
- Validação: `qpdf --check` (biblioteca PDF independente, não é código nosso) confirma "No syntax or stream encoding errors" nos 4 cenários testados (comentário simples, multi-página com `copies=2`, `pdfa=true`, `booklet=true`). `qpdf --show-object` resolve corretamente a cadeia `/Prev` e mostra o Catalog/Annots/StructTreeRoot mesclados como esperado.
- Playbook `qa/wave13-pdf-fidelity.json`. Regressão: waves 3–12 continuam `ok:true`.

**Decisions:** Tagging de granularidade de parágrafo fica fora — exigiria ou suporte que o Qt não tem, ou um gerador de content stream PDF próprio no lugar do `QPdfWriter` (substituir a engine de PDF é grande demais pra caber aqui, mapeado como possibilidade futura, não prometido). Anotações não funcionam em `booklet` por ambiguidade de posição — fallback pra página extra mantido só nesse caso.

**Next suggested step:** Onda 14 (processador CSL) ou onda 15 (CTL/CJK completo) — ou uma onda de packaging (Hunspell sempre disponível no build Nix), se o utilizador preferir.

**Commits:** `8d8d2c5` feat: real PDF comment annotations and page-level tagged PDF

---

## 2026-09-14 — Claude Code — Fase IA onda 12: round-trip real, crossref kind, hifenação real

**Objective:** Continuar a Fase IA na Onda 12 (backlog puxado do Notion + `docs/WRITER_FEATURE_CATALOG.md`): bookmarks/revisões/hiperlinks/campos com fidelidade real de round-trip ODT/DOCX, `insert_crossref` com `kind`, e hifenação real (Knuth-Liang) no lugar da heurística. Sem chrome QML novo. Sem push.

**Done:**
- Verificação de código (não só a nota do catálogo) antes de planear: `Revision.payload` e `document_export_pdf` `from/to/copies` já estavam implementados desde as ondas 9/11 — só o catálogo estava desatualizado, corrigido.
- Bookmarks e revisões `insert` → marcação real: ODT `<text:bookmark-start/end>` + `<text:change-start/end>`/`<text:tracked-changes>`; DOCX `<w:bookmarkStart/End>` + `<w:ins w:author w:date>`. Blob `aioffice:extra`/`word/aioffice.json` vira fallback de merge (id não achado na marcação real ainda é recuperado dele). Revisões `delete`/`format` continuam só no blob — limite honesto documentado (o modelo remove o texto apagado de `runs` na hora, só fica log em `Revision.text`).
- Hiperlink corrigido nos dois sentidos e formatos: DOCX escrevia `w:anchor` para toda âncora (quebrava URL externa) e nenhum formato lia `text:a`/`w:hyperlink` de volta — hiperlink se perdia sempre ao reabrir. Agora link interno usa âncora/`#id`, externo aloca relationship (`r:id`, `TargetMode=External`); leitura implementada nos dois formatos.
- Campos: `merge/var/custom/input/time/cite` agora mapeiam para `MERGEFIELD/DOCVARIABLE/FILLIN/TIME/instr próprio` no DOCX (antes só 6 tipos clássicos mapeavam) e são lidos de volta; corrigido bug de extração do argumento do campo no load DOCX (`instr.section` pegava a palavra errada e forçava maiúsculas, quebrando ids como `h2`).
- `insert_crossref` ganhou `kind`: `page` (nº de página vivo do alvo via `DocumentLayout::pageOf`), `heading_number`, `category_number`, `footnote_number`, `endnote_number` (dinâmicos, recalculados a cada `page_preview`), `above_below` (resolvido uma vez no insert, documentado como não-vivo).
- `insert_at_caret` passou a gravar `Revision` insert/delete quando `track_changes` está ligado — antes só a digitação ao vivo no canvas e `apply_character_format` gravavam; a tool MCP mais usada para editar texto não gravava nada.
- Hifenação real Knuth-Liang (`src/core/Hyphenation.h/.cpp`) com dicionários `hyph_en_US.dic` (tri-license GPL-2.0/LGPL-2.1/MPL-1.1, usado sob LGPL-2.1) e `hyph_pt_BR.dic` (LGPLv3/MPL) — ambos verificados compatíveis com GPL-3.0-or-later antes de vendorizar (`src/core/hyphen/NOTICE.md`). `pt_PT` avaliado e descartado por licença ambígua ("GNU GPL" sem versão). Substitui a heurística de sufixos/5-em-5; validado manualmente (`desenvolvimento`→`de-sen-vol-vi-mento`, `hyphenation`→`hy-phen-ation`).
- Playbooks: `qa/wave12-roundtrip.json`, `qa/wave12-fields.json`, `qa/wave12-crossref.json`, `qa/wave12-hyphenation.json`; `qa/check_roundtrip.py` estendido para conferir contagem de bookmarks/revisões. Regressão: ondas 9/10/11 continuam `ok:true`.
- Notion: nova linha Todo/Infra registando a sugestão de um bridge HTTP opcional (`aioffice mcp --http`) para IA hospedada sem MCP nativo nem acesso a processo local — fora de escopo desta onda, mapeado para onda futura de Platform/Infra.

**Decisions:** Bookmarks continuam de granularidade "bloco inteiro" (não offset preciso) — consistente com o que já existia no save ODT. Revisão `delete`/`format` fica só no blob JSON até uma onda dedicada decidir reinserir texto apagado sinteticamente na exportação. `above_below` é deliberadamente não-vivo (resolvido no insert). `pt_BR` escolhido sobre `pt_PT` por clareza de licença. Onda 13 (PDF tagged + anotações) já tem o caminho técnico confirmado via docs do Qt: sem API nativa, precisa de pós-processador de PDF.

**Next suggested step:** Onda 13 (fidelidade de PDF: tagged + anotações reais via pós-processador), depois onda 14 (CSL) e onda 15 (CTL/CJK). Ou, se o utilizador preferir, uma onda de packaging (Hunspell sempre disponível no build Nix).

**Commits:** `66b1576` feat: real ODT/DOCX markup for bookmarks/revisions, hyperlinks, fields, crossref kinds, and real hyphenation

---

## 2026-08-30 — Cursor — Página GitHub e README Tinta & Papel

**Objective:** Deixar o repositório apresentável (About, topics, README) sem o tornar público.

**Done:**
- About + 20 topics + labels (API GitHub; repo **privado**).
- README reescrito: banner SVG, badges cobre/tinta, MCP, build, licença.
- Banner: `docs/github/banner.svg`.

**Decisions:** Sem git tags/Releases (ainda incompleto). Topics em inglês (pesquisa GitHub); README em português. Repo continua privado.

**Next suggested step:** Social preview PNG 1280×640 quando quiserem um cartão para Discord/Twitter.

**Commits:** `3694217` docs: present Writer on GitHub with a readable Tinta e Papel README

---

## 2026-08-30 — Cursor — Fase IA wave 11: órfãs, biblioteca, notas, mestre

**Objective:** Regra always-apply «implementar, não descartar» + continuar a Fase IA (JSON+IDs + MCP + `page_preview`). Sem QML novo.

**Done:**
- Regra `.cursor/rules/implement-dont-discard.mdc` (`alwaysApply`).
- Órfãs/viúvas: parágrafo parte entre páginas (clip + `layoutY0`).
- Notas: `note_restart` numera por página no preview.
- Biblioteca de imagens MCP; `insert_bibliography` APA/ABNT-lite; `document_include`.
- Track de formato (`Revision.payload`); `find_replace` com `format`; estilos `TOC 1`…`TOC 6`.
- PDF `copies` + página de comentários; hífen com sufixos; campo `filepath`.
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave11-ia-continue.json` (`valid: true`, 4 páginas).
- Notion: **183 Done / 18 Partial / 40 Todo**.

**Decisions:** Comentários PDF = página extra (QPainter não emite anotações). Duplex = SO. CSL/tagged PDF/hífen TeX = Partial honesto. Sem chrome QML. Sem push.

**Next suggested step:** Onda 12 no que ainda for IA no board, ou Fase Humano no chrome. Testes golden só quando o utilizador pedir.

**Commits:** `06bd863` feat: split paragraphs for orphans and add library MCP tools

---

## 2026-08-30 — Cursor — Fase IA wave 10: objectos, página, revisão

**Objective:** Continuar a Fase IA (JSON+IDs + MCP + `page_preview`). Não descartar formas/QR/ruby/livreto por serem “grandes”. Sem QML novo.

**Done:**
- Modelo: `shape`/`chart`/`qr`/`equation`/`form`; flip/crop/sombra; ruby; `show_if`; redact; bibliografia JSON.
- Preview: QR, barras, formas, conectores, z-order, numeração de capítulos/linhas, tom/grelha, endnotes de secção, PDF `from`/`to`/`booklet`.
- MCP: insert_*, stats, compare, a11y, autotext, snapshots, repeat_last, thesaurus, user.dic, grammar, align, cite, figure_index, revision_view.
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave10-ia-continue.json` (`valid: true`, 3 páginas).
- Notion: **175 Done / 20 Partial / 46 Todo**. CSL completo, tagged PDF, órfãs reais = Partial.

**Decisions:** QR é preview byte-mode curto (não ISO-scanner). Sem chrome QML. Sem push. Testes golden só quando o utilizador pedir.

**Next suggested step:** Onda 11 no que ainda for IA no board (galeria, track format, reinício vivo de notas) ou Fase Humano no chrome. Testes à parte.

**Commits:** `b045fd1` feat: add Writer objects, page extras, and review MCP tools

---

## 2026-08-30 — Cursor — Fase IA fechada (onda final JSON+MCP)

**Objective:** Implementar o que faltava da Fase IA (JSON + MCP + `page_preview`). Parar **antes** dos testes — o utilizador dá instruções à parte.

**Done:**
- Colunas desiguais (`column_widths`), capítulo no header, watermark, papéis extra (B4/B5/Executive/Folio/Statement).
- Vars / read-only / subject / keywords; campos `chapter` / `var` / `input`.
- Fórmulas de célula (`sum|avg|count`), estilos de tabela, `table_auto_width`, header de tabela repetido entre páginas.
- `export_html`, `convert_note`, `toc_update` níveis, `insert_index`, imagem no header, `rotate`/`opacity`.
- Alinhados no board itens já no motor (split/join, accept-all, caps, entrelinha, quebra de coluna, mail_merge, page PNG, wrap, filename, word_count).
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave9-ia-close.json` (`valid: true`, fórmula 174, 3 páginas).
- Notion: **126 Done / 17 Partial / 98 Todo**. Chrome = Humano; packaging/CRDT = Infra.

**Decisions:** Fase IA = o que a IA usa via MCP. Sem QML novo. Tagged PDF, CSL, CJK, charts, CRDT ficam fora. **Não correr a bateria de testes** até o utilizador pedir.

**Next suggested step:** Esperar instruções do utilizador para testes (golden PNG, round-trip, etc.). Depois Fase Humano no chrome Tinta & Papel.

**Commits:** `b46b66f` feat: close Writer IA phase with columns, fields, and table tools

---

## 2026-08-30 — Cursor — Fase IA wave 8: células, break-before, recuos, gutter

**Objective:** Continuar JSON+MCP+`page_preview` após wave 7. Sem chrome QML. Sem push.

**Done:**
- `break_before` no bloco + `apply_paragraph_format`; layout começa página nova.
- `keep_with_next` (já no motor) alinhado no board; recuos left/right/first no preview.
- Célula: `border_pt`/`border_color`/`background`/`v_align`/`padding_mm` via `set_cell`.
- `gutter_mm` (margem esquerda) alinhado no board.
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave8-cells-breaks.json`.

**Decisions:** Gutter sem mirror ímpar/par. Órfãs/viúvas ficam (parágrafos não partem entre páginas). Sem QML.

**Next suggested step:** repeat header de tabela, estilos de tabela, secções ímpar/par, ou começar Fase Humano no chrome Tinta & Papel.

**Commits:** `ed9f9cc` feat: add cell format, break-before, and paragraph indents

---

## 2026-08-30 — Cursor — Fase IA wave 7: promote, listas, tabela, export MD/txt

**Objective:** Continuar JSON+MCP+`page_preview` após wave 6. Sem chrome QML. Sem push.

**Done:**
- `promote_heading` / `demote_heading` (parágrafo → Heading 1; H2 ↔ H1).
- `wrap_list` / `unwrap_list`.
- `text_to_table`, `table_to_text`, `split_table` (e `table_sort` já existia — alinhado no board).
- `export_markdown`, `export_text`, `copy_markdown`.
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave7-structure-export.json`.

**Decisions:** Export MD/txt não mudam o canvas; `page_preview` cobre promote/listas/tabelas. Sem QML. PDF tagged continua Partial.

**Next suggested step:** keep-with-next/break-before visíveis se faltar, secções ímpar/par, bordas de célula, ou começar Fase Humano no chrome Tinta & Papel.

**Commits:** `258d661` feat: add heading promote, list wrap, and Markdown export

---

## 2026-08-30 — Cursor — Fase IA wave 6: HTML, molduras, ímpar/par, AutoFormat

**Objective:** Continuar waves 3–6 (JSON+MCP+`page_preview`). Sem chrome QML. Sem push.

**Done:**
- Cabeçalho/rodapé ímpar, par e primeira página (`set_header`/`set_footer` `even`/`first`).
- `insert_html` (HTML sanitizado via QTextDocument) e `insert_plain`.
- `insert_frame` + `create_style kind=frame`; `autoformat` (aspas, `--`, fracções, URL).
- `join_blocks`; `load_styles {path}`.
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave6-html-frames.json`.

**Decisions:** PDF tagged continua Partial (QPainter não gera árvore de estrutura). AutoFormat é MCP de documento, não GUI. Sem QML.

**Next suggested step:** PDF tagged (se houver API Qt), secções ímpar/par, promote heading, wrap_list, ou começar Fase Humano no chrome Tinta & Papel.

**Commits:** `fbf879d` feat: add HTML paste, text frames, and odd/even headers

---

## 2026-08-29 — Cursor — Fase IA wave 5: kerning, idioma, find, estilos, etiquetas

**Objective:** Continuar waves 3–6 (JSON+MCP+`page_preview`). Sem chrome QML. Sem push.

**Done:**
- Kerning/tracking (`run.kerning`) no preview; idioma em run/bloco.
- `search` por formato (bold/italic/font/highlight/lang/…); `goto`; `select_by_style`.
- Listas: `list_restart`, `list_bullet`, `create_style kind=list`.
- Página: `kind=page`, `number_format`/`number_start`, `insert_break page_restart`.
- `StyleDef.next` + `split_block`; `update_style`; data com formato Qt; `insert_labels`.
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave5-styles.json`.

**Decisions:** Ligaduras ficam no HarfBuzz default (Qt 6.11 sem `QFont::PreferLigatures`). Nº impresso ≠ página física. Sem QML. PDF tagged continua Partial.

**Next suggested step:** Waves 3–6 restantes (PDF tagged, moldura, ímpar/par, colar HTML, AutoFormat). Fase Humano só quando a IA estiver plena.

**Commits:** `c52aa48` feat: add kerning, find-by-format, list/page styles, and labels

---

## 2026-08-29 — Cursor — Fase IA wave 4: runs, checkbox, carácter, envelope

**Objective:** Continuar waves 3–6 (JSON+MCP+`page_preview`). Sem chrome QML. Sem push.

**Done:**
- Fonte/tamanho/cor e realce pintados no preview; `small_caps` / `caps`; `hidden`; `char_style`.
- Checkbox: `insert_list kind=checkbox`, `toggle_check`.
- `clear_format`, `paint_format`, `insert_special` (nbsp/shy/emdash/…).
- Fundo da página (`background`); papéis DL/C5/C6; template `envelope`.
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave4-runs.json`.

**Decisions:** Texto oculto some no preview mas fica no JSON. Envelope = DL landscape; etiquetas ficam Partial. Sem QML.

**Next suggested step:** Waves 3–6 restantes (kerning, idioma, find por formato, estilos de página/lista, etiquetas, PDF tagged). Fase Humano só quando a IA estiver plena.

**Commits:** `ae64b4b` feat: add run styles, checkbox lists, and envelope template

---

## 2026-08-29 — Cursor — Fase IA wave 3: tabs, drop cap, nested, merge, PDF/A

**Objective:** Continuar waves 3–6 (JSON+MCP+`page_preview` apenas). Sem chrome QML. Sem push.

**Done:**
- Tabulações: `tabs[]` no bloco, `apply_paragraph_format`, `insert_tab`; preview com `QTextOption` tab stops.
- Letra capitular visual: `drop_cap_lines` ≥ 2 pinta a primeira letra no `page_preview`.
- Tabelas aninhadas (um nível): `cell.nested`, tools `nest_table` e `set_cell` `nested_row`/`nested_col`.
- Mala direta: `mail_merge {records}` substitui `{{chave}}` e campo `merge`; quebra de página entre cópias.
- Bordas de parágrafo e da página; `insert_line_break`.
- PDF/A-1b: `document_export_pdf {pdfa:true}` via `QPdfWriter::PdfVersion_A1b` (tagged PDF ainda não).
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave3-layout-merge.json`.

**Decisions:** Nested table é um nível (runs, sem recursão). `mail_merge` clona o corpo e reatribui IDs (undo restaura o modelo). PDF etiquetado fica Partial no Notion.

**Next suggested step:** Waves 3–6 restantes (fonte/tamanho/cor de run, realce, checkbox, estilos de carácter, envelopes, PDF tagged). Fase Humano só quando o critério IA estiver pleno no catálogo restante.

**Commits:** `8d41845` feat: add tabs, drop cap, nested tables, mail merge, and PDF/A

---

## 2026-08-29 — Cursor — Fase IA: DoD + fundação MCP + fatia Writer

**Objective:** Executar o programa Fase IA (depois Humano): Wave 0 auditoria, Wave 1 fundação MCP, Wave 2 round-trip A+B, fatia JSON+MCP+preview das waves 3–6. Sem chrome QML novo. Sem push.

**Done:**
- **DoD IA:** dado no `.aio.json` com IDs; tool MCP/`aioffice call` com `{ok:false,error}`; `page_preview` reflecte; `docs/mcp-tools.md` actualizado. ODT/DOCX não bloqueia a subfase (SoT = JSON).
- Wave 0: coluna Notion **Fase** (`IA`/`Humano`/`Infra`/`Fora`); board alinhado ao `Session::call`.
- Wave 1: `validate`, `batch` atómico, `id`/`ids[]`, `checkpoint`/`restore`, `aioffice play`, JSON canónico, `page_preview` `from`/`to`, `insert_markdown`, outline rico. Teste: `AIOFFICE_HEADLESS=1 aioffice play qa/wave1-foundation.json`.
- Wave 2: round-trip ODT/DOCX de notas, campos, TOC, bookmarks, revisões; notas no layout por página; quebra de coluna + separador; track `format` + accept/reject all; Hunspell `DICPATH` com `:` + dicts no `shell.nix`; Autocorrect JSON MCP.
- Waves 3–6 (fatia, não o catálogo inteiro): recuos/`keep_with_next`/papéis extra/gutter; `merge_cells`/`split_cells`/`table_sort`/`set_image`/`insert_caption`; export `md`/`txt`/`html`; campos `filename`/`word_count`/`custom`.
- Fase Humano **não** começou (UX Pilot + `Ui*` só depois da IA plena).

**Decisions:** `aioffice play` é sequencial (macros); atomicidade = tool `batch`. Checkpoint fora de `batch`. Commits locais por unidade; nunca `git push` neste programa até pedido.

**Next suggested step:** Continuar waves 3–6 no Notion (tabulações, drop cap visual, nested table, mala direta, PDF/A). Fase Humano só quando o critério IA estiver pleno no catálogo restante.

**Commits:** `cc141e4` feat: add MCP session foundation and Writer IA extras

---


## 2026-08-29 — Cursor — Backlog longo no Roadmap (o que um agente pode fazer)

**Objective:** Acrescentar no Notion tudo o que, mesmo demorando anos, um agente pode implementar no AI Office *do nosso jeito* (JSON+IDs, MCP/CLI, Tinta & Papel).

**Done:**
- +150 linhas Todo no Roadmap (formato, chrome, tabela, MCP, a11y, testes, etc.).
- Board: 22 Done, 16 Partial, **203 Todo** (241 no total).
- Hub callout + `docs/WRITER_FEATURE_CATALOG.md` actualizados.

**Decisions:** Se cabe no nosso modelo, entra. Continua **fora**: Calc, Impress, Draw-app, Base, StarBasic, Gallery/UI/ícones LO, WPS/FreeOffice, chat Copilot, Share. Gráficos/fórmulas = renderer nosso. CRDT local está no board como horizonte longo.

**Next suggested step:** Round-trip ODT/DOCX das ondas A+B; tools MCP de lote/IDs.

**Commits:** nenhum (usuário não pediu)

---

## 2026-08-29 — Cursor — Catálogo Writer Guide no Roadmap Notion

**Objective:** Encher o Roadmap para Produção (hub AI Office) com o catálogo de ideias do Writer Guide 26.2 — Done / Partial / Todo — sem copiar código/UI do LO e sem Calc/Impress/Base.

**Done:**
- Colunas `Fonte` e `Camada` no data source `collection://f3cc11ed-9e5e-4389-b49a-ca98f98dda62`.
- Callout do hub: sync 2026-08-29 + `docs/ATTRIBUTION.md` + este catálogo.
- Round-trip ODT/DOCX → **Partial** (notas/campos/TOC/revisões ainda não).
- 75 linhas novas (12 Done + 13 Partial + 50 Todo Writer/Formats). Board: 22 Done, 16 Partial, 53 Todo.
- Espelho: `docs/WRITER_FEATURE_CATALOG.md`.

**Decisions:** Ideias TDF só. Macros = MCP/CLI. Formas no documento de texto = cap. 11 no nosso canvas. Planilha/PPT continua Todo / não começar.

**Next suggested step:** Round-trip ODT/DOCX das ondas A+B; mesclar células / âncora de imagem. Não implementar features neste passo.

**Commits:** `aae080d` docs: catalog Writer Guide features for the Notion roadmap

---

## 2026-08-29 — Cursor — Writer ondas A+B (ideias LO, código nosso)

**Objective:** Implementar no Writer (JSON + MCP + GUI Tinta & Papel) as ideias de documento estruturado e edição de texto do LibreOffice Writer Guide, sem copiar código.

**Done:**
- `docs/ATTRIBUTION.md` (TDF / Writer Guide; WPS e FreeOffice fora).
- Runs: tachado, super/sub, campos, `note_id`.
- Bookmarks, revisões, `page.columns`, `hyphenate`, blocos footnote/endnote/toc.
- Tools: link_set, bookmark_add, insert_crossref/field/footnote/endnote, toc_update, spellcheck, track_changes, revision_*.
- Find `regex`; Hunspell opcional; Autocorrect no caret; hífen no layout.
- CommandBar + Sidebar + FindBar.

**Decisions:** Código nosso, GPL. LO só como referência de ideias. Ortografia só se Hunspell estiver no build.

**Next suggested step:** Empacotar Linux; Windows v1. Mesclar células / âncora de imagem depois.

**Commits:** `bd131dd` feat: add Writer Guide ideas in JSON, MCP, and GUI

---

## 2026-08-29 — Cursor — Relicense to GPL-3.0-or-later

**Objective:** Trocar MPL-2.0 por GPL para o software e os derivados continuarem livres, com o nome do autor citado.

**Done:**
- `LICENSE` = texto oficial GNU GPL v3 (29 June 2007).
- Copyright (C) 2026 Melker Halberd Pereira Alves no README, `docs/architecture.md` e `docs/DOCUMENTACAO_COMPLETA.md`.

**Decisions:** GPL-3.0-or-later (copyleft: quem reutilizar tem de manter livre). Attribution pelo aviso de copyright, que a GPL obriga a preservar. LO continua MPL, processo separado.

**Next suggested step:** Empacotar Linux (PATH/Nix); Windows v1. Planilha e PPT = apps separados.

**Commits:** `0582976` docs: relicense AI Office to GPL-3.0-or-later

---

## 2026-08-29 — Cursor — MPL-2.0 + initial git commit

**Objective:** Inicializar o repositório git, publicar no GitHub, e alinhar a licença com a do LibreOffice (MPL 2.0).

**Done:**
- `LICENSE` trocado de MIT para o texto oficial da Mozilla Public License 2.0.
- README, `docs/architecture.md` e `docs/DOCUMENTACAO_COMPLETA.md` atualizados.
- Removida instrumentação de debug (`agent log`) de `Session.cpp` e `Preview.cpp`.
- `.gitignore`: `.cursor/settings.json` e `.cursor/debug-*.log`.
- `git init` + commit inicial + push.

**Decisions:** Mesma licença do LibreOffice (MPL-2.0), não dual MPL/LGPL. LO continua processo separado em runtime.

**Next suggested step:** Empacotar Linux (PATH/Nix); Windows v1. Planilha e PPT = apps separados.

**Commits:** `97a4551` feat: initial AI Office Writer under MPL-2.0

---

## 2026-08-29 — Cursor — QA P1/P2 fixes from ResultadoDosTestes

**Objective:** Corrigir bugs P1/P2 e features v0.2 listadas em `qa/ResultadoDosTestes.md`.

**Done:**
- `insert_table`: `header_texts` na linha 0; `cells` a partir da linha 1; `header` no JSON; `col_widths_mm`.
- Markdown: `**bold**`, `*italic*`, `[text](url)`, listas aninhadas por indentação.
- Templates: alias `name`, `force` se dirty, título humano, limpa `path`, `fields`/`{{placeholders}}`.
- `insert_at_caret` substitui o range selecionado.
- `document_diff` vs último save/open/new/template (`added`/`moved`).
- Paint por run (negrito só no trecho); comentário = círculo cobre na margem.
- Mini-barra: botão H liga header; excluir tabela é `⊟`; combo de estilos usa `styleLabels`.
- ODT: `svg:width` → `width_mm`; não cria parágrafo vazio em volta da imagem.
- DOCX: twips 1440/25.4; quebra de página sem parágrafo fantasma.

**Decisions:** Diff contra baseline de arquivo (não último undo). Merge de células fica para depois. Planilha/PPT fora.

**Next suggested step:** Reiniciar a GUI no eDP-1 e conferir comentário no papel + botão H. Depois empacotar PATH/Nix.

**Commits:** `97a4551` feat: initial AI Office Writer under MPL-2.0

---

## 2026-08-28 — Cursor — Writer v0.2 QA + Tinta & Papel

**Objective:** Corrigir QA P0–P2 e completar o Writer (layout, ODT/DOCX, canvas, tools, GUI a partir dos frames UX Pilot).

**Done:**
- Hub Notion + Roadmap para Produção + catálogo MCP (irmão do Rose Faxina).
- 16 frames novos na página `bOAKXwabCJcNcRA1DigB` (job `wIOu49yS2pyteKKuI9cH`); mapa em `docs/design/html/`.
- Layout único (fonte×zoom, listas 1.2.3, tabela wrap, imagem raster, header/footer, hit-test, PDF nativo).
- IPC lê JSON até newline (PNG em base64).
- ODT/DOCX: table/list/image/break/page-layout, T-styles únicos, numbering.xml, Pictures/media.
- Tools: `after_id` falha, format por range, Title, tabela, search, selection, comments, header/footer, markdown, templates, diff, PDF nativo.
- Canvas digitável (caret cobre, seleção, IME, células) ligado à Session.
- QML: nav de página, FindBar next/prev, styleNames, TableMiniBar real, tabs, UnsavedSheet, dica LO, chip PDF nativo.
- Wrapper `scripts/aioffice`.

**Decisions:** Design HTML manda; sem Compartilhar; ODT/DOCX nativos (o frame do aviso LO falava o contrário — produto vence). PDF nativo no caminho feliz. Tabs por snapshot JSON, uma Session viva.

**Next suggested step:** Empacotar Linux (PATH/Nix); Windows v1. Planilha e PPT = apps separados da suite, nunca neste Writer.

**Commits:** nenhum (usuário não pediu)

---

## 2026-08-28 — Cursor — Continuity + Writer v0.2 in progress

**Objective:** Instalar o método de agentes (handoff, regras, wiki) e completar o Writer segundo o QA em `qa/ResultadoDosTestes.md`.

**Done:**
- Regras Cursor (`ai-continuity`, `ai-commit-protocol`, `project-context`, `commit-language-english`) + `design-overrides-qt` já existente.
- Stubs de `DOCUMENTACAO_COMPLETA.md`, `DESIGN_CHANGES.md`, `QA_REPORT.md`, `docs/design/`.

**Decisions:** Git = o que mudou; handoff = por quê; Notion = wiki + Roadmap para Produção. Design Tinta & Papel manda sobre Qt. Planilha e PPT são apps separados (suite), não features deste Writer.

**Next suggested step:** Hub Notion + gerar frames UX Pilot que faltam; depois layout, ODT/DOCX, canvas.

**Commits:** (esta sessão)
