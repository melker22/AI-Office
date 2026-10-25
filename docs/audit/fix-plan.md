# Auditoria 2026-10 — plano de correção (Opus)

Fontes (sem duplicatas entre elas):

- **Frente A:** `coderabbit-history.md`. Dos 114 achados históricos do CodeRabbit, **6 continuam VALID**.
- **Frente B:** `module-sweep.md`. **100 CONFIRMED**: 12 critical, 81 major, 7 minor. 8 deles foram reproduzidos com os playbooks de `qa/audit/`.
- **PR #11:** `coderabbit-pr11.json`, revisão completa do CodeRabbit com **29 achados**: 1 critical e 28 minor. Cerca de 9 se sobrepõem à Frente B.

O total é de cerca de 125 defeitos únicos. Cada onda é uma Fleet própria e segue o mesmo padrão: contrato de executor, gates `fleet-verify-merged` e `qa-all`, revisão do Sonnet, PR e `scripts/pr-merge-gate.py` com `cr-ack`.

Os executores desta rodada são **Grok Build 4.7** (principal) e **Composer 2.5** (tarefas mecânicas), porque a cota mensal da Cursor está em 73%. O **Sonnet** revisa.

Todo defeito corrigido ganha um teste que falha antes da correção (L9, L19).

Os 10 playbooks de `qa/audit/` (um por defeito e formato) têm `expect` com o comportamento certo e falham hoje, cada um no passo do defeito; `scripts/check-audit-playbooks.sh` (que roda `qa/audit/setup.sh` antes) exige isso. A onda que corrige o defeito move o playbook para `qa/waveNN-*`, onde ele passa, e o liga ao ctest.

## Onda 23 — Integridade, entrada hostil e transporte — **entregue** (2026-10-25, branch `fleet/wave23-integrity-89eb4702`, HEAD `3a52198`)

Gate: `scripts/qa-all.sh --gui --interop --pdf --formats` — 118/118 pass, 0 falha, 2 ctest `DISABLED` (ver A-qa-1 abaixo).

### T1 MCP/CLI/IPC (Grok Build)

| Id | Estado | Notas |
|---|---|---|
| CR-11 (MCP stdio) | **Corrigido** | Escrita JSON por linha; `qa/check_mcp_stdio.py` / ctest `mcp_stdio`. |
| A-mcp-1, A-mcp-2 | **Corrigido** | Falha IPC → `{ok:false}`; sem sessão local em branco após socket conectado. |
| A-mcp-3 | **Corrigido** | `convertTo` em diretório temp privado; move atómico para destino. |
| A-mcp-4..10 | **Corrigido** | Frame parcial no buffer; modo headless/GUI fixo no MCP; CLI JSON inválido; `aioffice play` documentado não-atómico; `page_preview` `page`/`"all"`; socket por uid + `UserAccessOption`; LO falha rápida. |
| PR #11 (socket IPC) | **Corrigido** | Coberto pelos playbooks `wave23_ipc`, `wave23_socket`, `wave23_cli`, `wave23_lok`. |

### T2 Invariantes da sessão (Grok Build)

| Id | Estado | Notas |
|---|---|---|
| A-session-1..8 | **Corrigido** | Playbooks `qa/wave23-session-*` + merges T2. |
| CR-10-113 | **Corrigido** | Export/read-only numa cópia (`wave23-session-export-copy`). |
| CR-10-114, A-document-5, A-document-6 | **Corrigido** | Refresh de campos com páginas reais; `wave23-session-fields`. |

### T3 Falhas de E/S e entrada hostil (Grok Build)

| Id | Estado | Notas |
|---|---|---|
| A-docx-1, A-docx-2, A-docx-3 | **Corrigido** | Limites zip, XML inválido, `QSaveFile`; `wave23-io-*` + ctest `wave23_io_hostile`. |
| A-odf-2, A-odf-3, A-odf-9 | **Corrigido** | XML truncado, limite `text:s`, escrita atómica ODT. |
| A-rtf-3 | **Corrigido** | Falha de escrita propagada; setup `wave23_io_setup`. |
| A-epub-1, A-epub-6 | **Corrigido** | Limite de imagem EPUB; sufixo de id colidente. |
| A-document-1..3, A-document-7 | **Corrigido** | Legenda/cabeçalho/plainText; JSON body e ids duplicados. |
| PR #11 (ids duplicados JSON) | **Corrigido** | `wave23-io-json-duplicate-ids`, `wave23-io-json-assign-ids`. |

### T4 QA, CI e Fleet (Composer 2.5)

| Id | Estado | Notas |
|---|---|---|
| A-qa-2..6 | **Corrigido** | EPUB listas aninhadas, critério RTF, ordem PDF reading-order, verapdf ausente falha, artefacto CircleCI. |
| A-qa-1 | **Adiado (onda 24)** | Checker compara payload de revisão; export/reimport foreign DOCX/ODT ainda perde payload — ctests `roundtrip_foreign_odt_wave17` e `roundtrip_foreign_docx_wave17` ficam `DISABLED TRUE` até a onda 24 (DOCX/ODT fidelity). |
| PR #11 (`qa-all` label vazio, presets, artefactos, `fleet-verify-merged`) | **Corrigido** | `5ecf90e` / D18. |
| CR-3-032 | **Corrigido** | `success_output` nas Fleets antigas. |
| CR-9 (`\u` RTF self-test) | **Corrigido** | Autoteste deixa de aceitar substring falsa. |

## Onda 24 — Fidelidade DOCX/ODT

Itens:

- **DOCX:** A-docx-4..12, CR-1-009 (mescla de revisões nativas com o JSON embutido). Do PR #11: ids únicos de bookmark, escape duplo no `FILLIN` e validação de `w:color`.
- **ODT:** A-odf-1, A-odf-4..8 (notas, página real em mm, fonte, células mescladas, tipos de lista, formas). Do PR #11: escape de nomes de estilo e bookmarks dentro do parágrafo.
- **QR:** A-odf-10 (o QR gerado não é decodificável).

Executores: Grok Build em DOCX e em ODT, em paralelo; Composer no QR e nas fixtures.

## Onda 25 — Fidelidade RTF/EPUB/PDF

Itens:

- **RTF:** A-rtf-1, A-rtf-2, A-rtf-4..14, e do PR #11 a sanitização do `\fldinst` e as endnotes.
- **EPUB:** A-epub-2..5, A-epub-7..10.
- **PDF:** A-pdf-1..4 (escape de string, estrutura com cópias, listas aninhadas, ColSpan/RowSpan).

## Onda 26 — Layout e preview

A-preview-1..7 (números de capítulo e nota, hifenização por idioma do span, `note_restart`, TOC sem a formatação do run, seleção proporcional, células sem formatação, hit-test com `gutterMirror`), o nitpick CR-3 (`modelToDisplay` O(M·D)) e A-document-4 (`seq` em tabela aninhada).

## Fase Humano (GUI) — ficam no backlog, sem correção agora

São 13 achados da Frente B e 9 do PR #11:

- **A-app-1..13:** fechar a janela descarta documento sujo sem perguntar (critical); trocar ou fechar aba carrega o documento errado ou perde o undo; salvar `.doc` não limpa dirty; seta divide par substituto; entre outros.
- **QML do PR #11:** 5 cores com o alfa na ordem errada, `UiSpin` quebrando o binding, `UiButton` sem teclado e `OutlineRail`.
- **`DocumentCanvas` (PR #11):** renderizar a página só para medir e `inputMethodQuery`.
