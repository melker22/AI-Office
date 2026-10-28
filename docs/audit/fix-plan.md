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

## Onda 24 — Fidelidade DOCX/ODT — **entregue** (2026-10-28, branch `fleet/wave24-docx-odt`, HEAD `d8a1a40`)

Gate: `scripts/qa-all.sh --gui --interop --pdf --formats` — 166/166 ctest pass, 0 falha, 0 `DISABLED` (os dois ctests de A-qa-1, `roundtrip_foreign_odt_wave17` e `roundtrip_foreign_docx_wave17`, voltaram a rodar e passam). Interop LibreOffice ok (docx↔odt, fields, rtf, wave24 docx export).

Executores: Cursor Grok 4.7 (nós iniciais T1/T2 DOCX import/export), depois Sonnet (fix-pass, CI label e sparse-table ODT), já que o Grok Build ficou indisponível durante a onda.

### DOCX

| Id | Estado | Notas |
|---|---|---|
| A-docx-4 | **Corrigido** | `loadDocx` lê `w:sz`/`w:rFonts`/`w:color`; `b/i/u/strike` respeitam `w:val`; `bidi` só é RTL com valor vazio, `1`, `true` ou `on` (`bidiIsOn`). |
| A-docx-5 | **Corrigido** | Parágrafo antes de um `w:drawing` passa por `flushP()` em vez de ser descartado com `runs.clear()`. |
| A-docx-6 | **Corrigido** | `pgSz` guarda `widthMm`/`heightMm` reais; nomes A4/Letter/Legal/A3/A5 só como atalho dentro de 1 mm. |
| A-docx-7 | **Corrigido** | Tabela aninhada usa pilha de `tableBlock`/`tableRow` em vez de uma única variável. |
| A-docx-8 | **Corrigido** | Export emite `w:gridSpan`, `w:vMerge`, tabela aninhada e a legenda (`Caption` + `SEQ` nativo). |
| A-docx-9 | **Corrigido** | `loadDocx` lê `docProps/core.xml` (`dc:title`/`subject`/`keywords`/`creator`) quando o JSON omite o campo. |
| A-docx-10 | **Corrigido** | Um campo dentro de `w:del` exporta como `w:delText` e reimporta para `delAcc`, não para `runs` vivos. |
| A-docx-11 | **Corrigido** | `word/numbering.xml` é mapeado; numId fora da tabela cai no fallback decimal/bullet (1/2) só quando a parte não existe. |
| A-docx-12 | **Corrigido** | `xmlEscape` descarta controles C0 fora de tab/LF/CR; `w:color` só escreve com seis dígitos hex, validados num laço sobre os caracteres, não por regex (`docxHexColor`, Docx.cpp:109-121). |
| CR-1-009 (Docx.cpp) | **Corrigido** | `jsonOwnsRevisions` foi removido; `loadDocx` sempre faz parsing nativo de `w:ins`/`w:del`/`w:rPrChange` e só pula uma marca quando uma revisão JSON do mesmo bloco/tipo já cobre o intervalo. |
| PR #11 — bookmark `w:id` único | **Corrigido** | Contador `DocxOut::nextBmId` substitui o índice local por parágrafo (Docx.cpp:50, 1022). |
| PR #11 — `FILLIN` escapado uma vez | **Corrigido** | `quoteFieldResult(r.fieldArg)` substitui o duplo `xmlEscape` (Docx.cpp:489). |
| PR #11 — validação de `w:color` | **Corrigido** | `docxHexColor` aceita só seis dígitos hex, verificados caractere a caractere (não por regex), antes de escrever o atributo. |

### ODT

| Id | Estado | Notas |
|---|---|---|
| A-odf-1 | **Corrigido** | Import de nota distingue `text:note-citation` de `text:note-body`; só o corpo entra no texto da nota. |
| A-odf-4 | **Corrigido** | Página ODT guarda `widthMm`/`heightMm` reais; Legal/A3/custom não colapsam mais em Letter/A4. |
| A-odf-5 | **Corrigido** | `applyTextProps`/import lê `style:font-name` de volta para o run. |
| A-odf-6 | **Corrigido** | Export escreve `table:number-rows-spanned` além de `number-columns-spanned`; import lê os dois (`parseTableBound`). |
| A-odf-7 | **Corrigido** | `listKindFromNumFormat`/`listKindFor` derivam o tipo real da lista do `style:num-format`, não um `"decimal"` fixo. |
| A-odf-8 | **Corrigido** | Blocos `shape`/`chart`/`qr`/`form` têm leitura e escrita próprias (Odf.cpp:1063, 1221-1223). |
| PR #11 — nomes de estilo escapados | **Corrigido** | `parent`, `odfStyleName(s.name)`, `s.align` e a orientação da página passam por `xmlEscape`; alinhamento validado por `isValidBlockAlign`. |
| PR #11 — bookmark dentro do parágrafo | **Corrigido** | A marca de bookmark é emitida inline na sequência de `spansXml`/`RevMark` no deslocamento certo do parágrafo, não envolvendo o `text:p` inteiro. |

### QR

| Id | Estado | Notas |
|---|---|---|
| A-odf-10 | **Corrigido** | Encoder usa byte mode nível M (versões 1–10) e reserva os módulos de função (finders, timing, separadores, área de formato, módulo escuro) antes de posicionar os dados, para a informação de formato não ser sobrescrita. QA roda `zbar` para confirmar que o símbolo é decodificável. |

### QA / CI (fix-pass do Sonnet, após o Cursor Grok 4.7 ficar sem Grok Build)

| Item | Estado | Notas |
|---|---|---|
| Label `wave24` fora do CI | **Corrigido** | `.circleci/config.yml` trocou `-LE wave24` por incluir `wave24` no `-L`, então os 35 testes da onda rodam no gate. |
| Perda da célula ao exportar ODT com tabela esparsa | **Corrigido** | `appendOuterTable` ganhou a mesma distinção entre coluna de grade e índice armazenado que `tableWml` já tinha; nova fixture `qa/wave24-docx-d4-tables-odt.json` cobre o caso. |

**Decisões:** nenhum item da onda 24 foi adiado; os dois ctests `DISABLED` de A-qa-1 (onda 23) voltaram a `TRUE`→ativos e passam. O backlog de GUI (Fase Humano) não foi tocado.

**Próximo passo sugerido:** onda 25 (RTF/EPUB/PDF).

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
