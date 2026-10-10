# Onda 17 — Round-trip fiel de revisões (DOCX/ODT) + suíte ctest — spec arquitetural

> Autor: Opus (Chief Architect), 2026-10-10. Fleet: `.cockpit/fleets/wave17-roundtrip.yaml`.
> Roteamento: [`docs/ORCHESTRATION.md`](../ORCHESTRATION.md). Fase IA: sem `src/app/`.

## Diagnóstico (código em `main` @ 19ee1cc)

O round-trip **com o próprio app** já é sem perdas: o DOCX/ODT exportado leva o JSON nativo embutido
(`word/aioffice.json`) e a importação o mescla. O problema é a **interoperabilidade** — o que o Word e o
LibreOffice leem do nosso arquivo, e o que nós lemos de um arquivo deles (sem `aioffice.json`).

1. **Bug — import de `w:del` vira texto vivo.** `Docx.cpp` trata `<w:delText>` igual a `<w:t>`
   (`run.text += …`) e cria uma revisão `delete` com faixa `start..end` sobre esse texto. Isso viola o
   invariante do modelo (revisão `delete` = ponto de largura zero, conteúdo em `deletedRuns`): um DOCX do
   Word com exclusões abre mostrando o texto excluído como normal; `revision_accept` não o remove e
   `revision_reject` o duplica. O import de `text:deletion` do ODT deve ser verificado pelo mesmo critério.
2. **Bug — rejeitar revisão `format` não desfaz a formatação.** `Session::revisionReject` não trata
   `kind == "format"`; o `payload` guarda só os argumentos NOVOS, não a formatação anterior.
3. **Gap — export de `delete`.** Desde a PR #2 os offsets de toda revisão ficam válidos a cada edição
   (`shiftRevisionOffsets`) e `deletedRuns` guarda o conteúdo com formatação: o comentário em `Docx.cpp`
   ("exigiria reinserção sintética") já não é bloqueio — basta emitir o conteúdo no ponto `start`.
4. **Gap — export de `format`.** OOXML tem `w:rPrChange` (com o `w:rPr` anterior); ODF tem
   `text:format-change` (só marca, sem atributos antigos).
5. **"Notas aninhadas"** (Partial no catálogo): nota dentro de nota não existe em DOCX nem em ODF.
6. **Sem ctest.** `CMakeLists.txt` não tem `enable_testing()`; a suíte é só o laço de `qa/wave*.json`.

## Decisões (fechadas)

| # | Decisão | Motivo |
|---|---|---|
| D1 | **Invariante canônico de `Revision`**: `insert` = faixa `[start,end)` de texto vivo; `delete` = ponto `start == end`, conteúdo em `deletedRuns` (e `text` = texto plano dele); `format` = faixa `[start,end)` com `payload` = args novos **e** novo campo `priorRuns` (fatias de `TextRun` da faixa ANTES da mudança). `validate` passa a checar esse invariante | Uma forma só; todo import/export/accept/reject se apoia nela |
| D2 | `applyCharacterFormat` com track changes grava `priorRuns`; `revisionReject`/`RejectAll` de `format` restauram `priorRuns` na faixa (mesmo `shiftRevisionOffsets` da PR #2). Revisão `format` sem `priorRuns` (vinda de ODT de terceiros) → reject devolve `{ok:false,error:"format revision has no prior formatting"}`; accept funciona | Corrige o bug 2 sem inventar dados |
| D3 | **DOCX export**: `delete` → no ponto `start`, `<w:del>` com um `<w:r>` por fatia de `deletedRuns` (rPr próprio + `<w:delText xml:space="preserve">`); `format` → cada `<w:r>` da faixa ganha `<w:rPrChange w:id w:author w:date><w:rPr>…anterior…</w:rPr></w:rPrChange>` como ÚLTIMO filho do `<w:rPr>` (ordem do schema `CT_RPr`). Ids `w:id` únicos no documento (mesmo `DocxOut::wId`) | É o que o Word lê |
| D4 | **DOCX import**: conteúdo de `<w:del>` vai para `Revision.deletedRuns`/`text` com `start == end == curLen()`, **nunca** para `runs`; `<w:ins>` como hoje (faixa); `<w:rPrChange>` → revisão `format` com `priorRuns` lidos do `w:rPr` interno e faixa = o run. Um `w:del`/`w:ins` que cobre vários runs vira UMA revisão | Corrige o bug 1 |
| D5 | **ODT export**: `delete` → `<text:change text:change-id="X"/>` no ponto + `<text:changed-region>` com `<text:deletion><office:change-info/><text:p>…spans de deletedRuns…</text:p></text:deletion>`; `format` → `text:change-start/end` na faixa + região `<text:format-change>`. **ODT import** simétrico: conteúdo da `text:deletion` → `deletedRuns`; `format-change` → revisão `format` sem `priorRuns` (D2) | Formato que o LibreOffice escreve/lê |
| D6 | Precedência: arquivo com `aioffice.json` continua com o JSON como verdade (inalterado). Todo teste de interop roda também o **caminho estrangeiro**: o mesmo pacote sem `aioffice.json` (e sem o JSON nativo no ODT), importado só pela marcação nativa → revisões equivalentes (kind, bloco, offsets, texto, autor) | Prova a marcação, não o atalho |
| D7 | **Fixtures estrangeiras** escritas à mão em `qa/fixtures/` (DOCX e ODT mínimos, no formato que Word/LibreOffice produzem: ins, del multi-run, rPrChange, format-change, deleção no meio de palavra). Nenhum binário de terceiros copiado | Licença limpa e determinístico na CI |
| D8 | **Gate de interop local**: `scripts/qa-all.sh --interop` (opcional, pula se não houver `soffice`): exporta DOCX/ODT com revisões, `soffice --headless --convert-to` (docx→odt, odt→docx), reimporta e compara revisões. CI não tem `soffice` → não roda lá; a Fleet roda localmente | Única prova real de que o LibreOffice entende o que escrevemos |
| D9 | **ctest**: `enable_testing()`; cada `qa/wave*.json` vira um teste (`LABELS playbook`), e `check_roundtrip.py` sobre as fixtures vira testes `LABELS roundtrip`. `scripts/qa-all.sh` passa a chamar `ctest --output-on-failure`; a CI troca o laço por `ctest -L "playbook|roundtrip"` | Item Todo "Suite de round-trip (ctest)" |
| D10 | "Notas aninhadas": **Fora** (nenhum dos dois formatos representa nota em nota). Notas com vários parágrafos viram item Todo novo — fora desta onda | Escopo honesto |
| D11 | Não regredir: ondas 12 (bookmarks/campos/hiperlinks nativos), 15 (tagging de PDF) e 16 (CTL) — todos os playbooks existentes continuam verdes | — |

## Tarefas e roteamento

| Nó | Executor | Por quê | Custo | Risco | Depende |
|---|---|---|---|---|---|
| `t1-docx-revisions` | Cursor Grok (`grok-4.7-high`) | D1–D4: modelo + Session + Docx.cpp, invariante sutil | Alto | **Alto** | — |
| `t2-odt-revisions` | Cursor Grok (`grok-4.7-high`) | D5: Odf.cpp, simétrico, isolado | Médio | Médio | — (paralelo; só usa `deletedRuns`, já existente) |
| `integrate-1` | Composer 2.5 | Merge (Document.h/Session vs Odf.cpp quase disjuntos) | Baixo | Baixo | t1, t2 |
| gate `build-1` | comando | `qa-all.sh --gui` | 0 | — | integrate-1 |
| `t3-ctest-fixtures` | Composer 2.5 | D7, D9: fixtures, CMake, CI, check_roundtrip.py (caminho estrangeiro) | Baixo | Baixo | build-1 |
| `t4-interop` | Cursor Grok (`grok-4.7-high`) | D8: script soffice + correções que o LibreOffice revelar | Médio | Médio | build-1 (paralelo a t3) |
| `integrate-2` | Composer 2.5 | Merge | Baixo | Baixo | t3, t4 |
| gate `qa-full` | comando | `qa-all.sh --gui --interop` | 0 | — | integrate-2 |
| loop `review` | Sonnet revisa · Grok Build (`grok-4.7`) corrige (≤3) | QA contra esta spec | Médio | — | qa-full |
| gate `qa-final` | comando | branch final testado | 0 | — | review |
| `handoff` | Composer 2.5 | AI_HANDOFF + catálogo | Baixo | — | qa-final |
| humano `approve-pr` → `open-pr` | Sonnet | push/PR | Baixo | — | handoff |

## Critério de pronto

- Fixture DOCX estrangeira com `w:del` multi-run: após import, o texto excluído **não** está em `runs`; `revision_accept_all` mantém o texto sem ele; `revision_reject_all` o reinsere **uma** vez, com a formatação original.
- Export DOCX de documento com insert+delete+format → caminho estrangeiro (sem `aioffice.json`) reimporta as 3 revisões com kind/bloco/offset/texto/autor iguais; `format` volta com `priorRuns`.
- Mesmo para ODT (format sem `priorRuns`, D2).
- Rejeitar revisão `format` restaura bold/itálico/cor anteriores (playbook com `expect`).
- `validate` falha para revisão `delete` com `start != end` (playbook negativo).
- Local: `scripts/qa-all.sh --gui --interop` verde, incluindo a ida e volta pelo LibreOffice. CI: `ctest -L "playbook|roundtrip"` verde.
