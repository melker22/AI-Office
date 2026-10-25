# Auditoria 2026-10 — plano de correção (Opus)

Fontes (sem duplicatas entre elas):

- **Frente A:** `coderabbit-history.md`. Dos 114 achados históricos do CodeRabbit, **6 continuam VALID**.
- **Frente B:** `module-sweep.md`. **100 CONFIRMED**: 12 critical, 81 major, 7 minor. 8 deles foram reproduzidos com os playbooks de `qa/audit/`.
- **PR #11:** `coderabbit-pr11.json`, revisão completa do CodeRabbit com **29 achados**: 1 critical e 28 minor. Cerca de 9 se sobrepõem à Frente B.

O total é de cerca de 125 defeitos únicos. Cada onda é uma Fleet própria e segue o mesmo padrão: contrato de executor, gates `fleet-verify-merged` e `qa-all`, revisão do Sonnet, PR e `scripts/pr-merge-gate.py` com `cr-ack`.

Os executores desta rodada são **Grok Build 4.7** (principal) e **Composer 2.5** (tarefas mecânicas), porque a cota mensal da Cursor está em 73%. O **Sonnet** revisa.

Todo defeito corrigido ganha um teste que falha antes da correção (L9, L19).

Os 10 playbooks de `qa/audit/` (um por defeito e formato) têm `expect` com o comportamento certo e falham hoje, cada um no passo do defeito; `scripts/check-audit-playbooks.sh` (que roda `qa/audit/setup.sh` antes) exige isso. A onda que corrige o defeito move o playbook para `qa/waveNN-*`, onde ele passa, e o liga ao ctest.

## Onda 23 — Integridade, entrada hostil e transporte (todos os critical do core/MCP)

| Tarefa | Executor | Itens | Risco |
|---|---|---|---|
| T1 MCP/CLI/IPC | Grok Build (alto) | **CR-11 critical:** o `McpServer` passa a escrever JSON delimitado por linha (o transporte stdio do MCP), com teste de conformidade num cliente stdio real. Também A-mcp-1/2 (nunca cair numa sessão em branco quando o IPC falha), A-mcp-3 (soffice sobrescreve um arquivo irmão), A-mcp-4..10 e o socket IPC por usuário com acesso restrito (PR #11). | Alto (contrato externo) |
| T2 Invariantes da sessão | Grok Build (alto) | A-session-1..8 (undo e caminho do documento, batch com save, ids de revisão, offsets em split/join/find_replace, `set_selection` com dirty, limite de `insert_table`, `document_include` que perde revisões). CR-10-113 (export somente leitura altera o documento aberto). CR-10-114 junto com A-document-5/6 (ordem de `stat`/`if`, e `if` sem o número real de páginas). | Alto |
| T3 Falhas de E/S e entrada hostil | Grok Build (médio) | A-docx-1 (zip bomb), A-docx-2, A-odf-2 (XML truncado abre como ok), A-docx-3, A-odf-9, A-rtf-3 (falha de escrita ignorada; escrita atômica), A-odf-3, A-epub-1 (alocação sem limite), A-document-7 e os ids duplicados no load (PR #11), A-document-1/2/3 (legenda de tabela e cabeçalho perdidos), A-epub-6. | Médio |
| T4 QA, CI e Fleet | Composer 2.5 | A-qa-1..6. Do PR #11: `qa-all` com filtro de label vazio, `CMakePresets`, artefatos da CircleCI e `fleet-verify-merged.sh` lendo a saída multilinha (afeta os gates de toda Fleet). CR-3-032 (`success_output` na adjudicação das Fleets antigas). CR-9 (marcador `\u` do RTF que nunca falha). | Baixo |

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
