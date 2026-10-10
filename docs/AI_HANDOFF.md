# AI Handoff — AI Office Writer

> **Propósito:** log operacional entre sessões de IA. Leia este arquivo **antes** de qualquer tarefa; atualize ao **concluir** (ou ao parar com trabalho parcial).
>
> **Não duplicar:** documentação de produto fica em `DOCUMENTACAO_COMPLETA.md` e `DESIGN_CHANGES.md`.
>
> **Manutenção:** manter no máximo ~20 entradas aqui; mover as mais antigas para `AI_HANDOFF_ARCHIVE.md`.

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

## Template (copiar para o topo)

```
## YYYY-MM-DD — Cursor — <título curto>

**Objective:**

**Done:**

**Decisions:**

**Next suggested step:**

**Commits:** nenhum (usuário não pediu) | `<hash>` <mensagem>
```

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
