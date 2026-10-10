# Onda 18 — Campos dinâmicos e texto condicional — spec arquitetural

> Autor: Opus (Chief Architect), 2026-10-10. Fleet: `.cockpit/fleets/wave18-fields.yaml` (primeira com o
> contrato de executor do PR #5 e com sincronização automática do Notion).
> Fase IA: só core + MCP; nada em `src/app/`.

## Diagnóstico (`main` @ 5029513)

- `TextRun.field` tem ~17 tipos avaliados em `runDisplayText` (`Document.cpp`): page_number, page_count,
  date, time, title, author, filename/filepath, word_count, chapter, var/custom, input, merge, cite, xref.
- Já existe um condicional **primitivo**: `TextRun.showIf` esconde o run se `doc.vars[showIf]` estiver
  vazio/"0"/"false". Não há expressões, nem "se/então/senão", nem parágrafo oculto por condição.
- Não há campo de **sequência** → "Legenda de figura" é Partial (sem numeração automática) e o xref
  `category_number` não tem uma fonte real de número.
- Faltam os campos de informação do documento e de estatística do Guide (cap. 17): assunto, palavras-chave,
  criação/modificação, nº de caracteres/parágrafos/tabelas/imagens; remetente; lista de entrada.
- Export: DOCX usa `w:fldSimple` com instruções reais para alguns tipos (onda 12); ODT tem `text:page-number`
  e `text:user-field-get`. Nenhum mapeamento para IF/SEQ/condicional.

## Decisões (fechadas)

| # | Decisão | Motivo |
|---|---|---|
| D1 | **Avaliador de expressões próprio** em `src/core/FieldExpr.{h,cpp}`: descida recursiva sem scripting. Literais número/string, variáveis (`doc.vars`), referências a campos numéricos (`page`, `pages`, `words`, `chars`, `paragraphs`, `tables`, `images`), `== != < > <= >=`, `&& \|\| !`, parênteses. Comparação numérica quando os dois lados são números; senão string. Erro de sintaxe → o campo exibe `#ERR` e `validate` aponta `invalid_expression` com o id do bloco | Seguro (sem eval), testável, cobre o que Word/LibreOffice expressam |
| D2 | **Novos tipos de campo** (todos via `insert_field`, args validados): `if` (`arg` = JSON `{cond, then, else}`), `doc_info` (`subject\|keywords\|created\|modified\|revision`), `stat` (`pages\|words\|chars\|paragraphs\|tables\|images`), `sender` (`name\|email\|company\|phone`, lidos de `doc.vars["sender.<x>"]`), `seq` (`arg` = JSON `{name, format}` — arabic/roman/alpha; contagem por nome na ordem do documento), `input_list` (`arg` = JSON `{options[], selected}`) | Fecha o Partial "Campos dinâmicos" com os tipos que o Guide mais usa |
| D3 | **Texto condicional**: `showIf` passa a aceitar expressão (D1); um nome de variável sozinho continua com a semântica antiga (compatível). Novo `Block.hideIf` (expressão): parágrafo oculto quando verdadeiro — some do layout, do PDF e da contagem `stat`, continua no JSON e na busca | Fecha o Todo "Texto condicional" |
| D4 | **Legenda numerada**: `insert_caption` ganha `seq` (`{name}`): insere `"<Nome> " + campo seq` antes do texto. `insert_crossref kind=category_number` passa a resolver o número do `seq` do alvo | Fecha o Partial "Legenda de figura" sem modelo novo |
| D5 | **DOCX**: `if` com UMA comparação → `IF <a> <op> <b> "then" "else"` real; expressão composta → exporta o resultado avaliado como texto (JSON embutido continua com a expressão — D6 da onda 17). `seq` → `SEQ <nome> \* ARABIC\|ROMAN\|ALPHABETIC`; `stat` → `NUMPAGES`/`NUMWORDS`/`NUMCHARS`; `doc_info` → `DOCPROPERTY`/`CREATEDATE`/`SAVEDATE`/`REVNUM`; `hideIf` → sem equivalente nativo: parágrafo exportado como texto oculto (`w:vanish`) quando a condição é verdadeira no momento do export. Import simétrico para o que existe nativo | É o que o Word lê |
| D6 | **ODT**: `if` → `text:conditional-text` (`text:condition="ooow:…"`, `string-value-if-true/false`); `seq` → `text:sequence` (+ `text:sequence-decls`); `stat` → `text:page-count`/`word-count`/`character-count`/`paragraph-count`/`table-count`/`image-count`; `doc_info` → `text:subject`/`keywords`/`creation-date`/`modification-date`/`editing-cycles`; `sender` → `text:sender-*`; `hideIf` → `text:hidden-paragraph` com a condição; `input_list` → `text:drop-down`. Import simétrico | Formato que o LibreOffice escreve/lê |
| D7 | Testes: playbooks com `expect` por tipo de campo e por condição; fixtures estrangeiras DOCX/ODT escritas à mão (padrão `qa/fixtures/make_fixtures.py` da onda 17); round-trip pelo caminho estrangeiro e pelo LibreOffice (`--interop`) | Mesmo padrão comprovado na onda 17 |
| D8 | Não regredir ondas 12 (campos/xref nativos), 14 (CSL), 15 (PDF tag), 16 (CTL), 17 (revisões) | — |

## Tarefas e roteamento (contrato de executor obrigatório — `docs/ORCHESTRATION.md`)

| Nó | Executor | Por quê | Custo | Risco | Depende |
|---|---|---|---|---|---|
| `t1-expr` | Cursor Grok 4.7 | D1 + D3: avaliador, showIf/hideIf, validate, tool `field_evaluate` | Médio | **Alto** (semântica nova) | — |
| `t2-kinds` | Composer 2.5 | D2 + D4: tipos novos, seq, legenda numerada (mecânico) | Baixo | Médio | — (paralelo; `if` usa um stub até o merge) |
| `integrate-1` | Composer 2.5 | Merge pelos hashes reportados | Baixo | Baixo | t1, t2 |
| gate `build-1` | comando | `fleet-verify-merged.sh 3 && qa-all.sh --gui` | 0 | — | integrate-1, t1, t2 |
| `t3-docx` | Cursor Grok 4.7 | D5 + fixtures DOCX | Médio | Médio | build-1 |
| `t4-odt` | Cursor Grok 4.7 | D6 + fixtures ODT + interop | Médio | Médio | build-1 (paralelo) |
| `integrate-2` | Composer 2.5 | Merge pelos hashes | Baixo | Baixo | t3, t4 |
| gate `qa-full` | comando | `fleet-verify-merged.sh 3 && qa-all.sh --gui --interop` | 0 | — | integrate-2, t3, t4 |
| loop `review` | Sonnet revisa · Grok Build corrige (≤3; `until` verifica o commit do fix-pass) | QA contra a spec | Médio | — | qa-full |
| gate `qa-final` | comando | branch final | 0 | — | review, integrate-2 |
| `handoff` | Composer 2.5 | AI_HANDOFF + catálogo | Baixo | — | qa-final |
| `notion-sync` | Sonnet (low) | Board/hub/snapshot do Notion | Baixo | — | handoff |
| humano `approve-pr` → `open-pr` | Sonnet | push/PR | Baixo | — | notion-sync |

## Critério de pronto

- `field_evaluate` cobre precedência, comparação número×string e erro (`#ERR` + `validate` com `invalid_expression`).
- `if` mostra then/else conforme `doc.vars` muda (playbook: set_vars → document_get/page_preview).
- `hideIf` verdadeiro: o parágrafo some do PDF/preview e de `stat paragraphs`; falso: volta.
- `seq`: três legendas "Figura" numeradas 1, 2, 3; inserir uma no meio renumera; xref `category_number` acompanha.
- DOCX/ODT: cada tipo novo sobrevive ao caminho estrangeiro (sem JSON nativo) e à ida e volta pelo LibreOffice, ou está documentado aqui como exportado-como-texto (D5).
- `scripts/qa-all.sh --gui --interop` verde; CI `ctest` verde.
