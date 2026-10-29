# Onda 26 — Layout e preview (spec arquitetural)

> Autor: Opus (Chief Architect), 2026-10-29.
>
> - **Origem:** auditoria 2026-10 (`docs/audit/fix-plan.md`, onda 26).
> - **Fleet:** `.cockpit/fleets/wave26-layout-preview.yaml`.
> - **Escopo:** Fase IA (core + QA). Não mexe em `src/app/` (QML/GUI ficam na Fase Humano).
> - **Aprovação:** o utilizador aprovou as ondas 23–26 com push e PR (ask_05e099babed7). O merge só sai com `scripts/pr-merge-gate.py` limpo e as threads resolvidas.

## Diagnóstico

As provas estão em `docs/audit/module-sweep.md` (A-preview-1..7, A-document-4) e em `docs/audit/coderabbit-history.md` (CR-3-035). As linhas citadas na auditoria são de antes das ondas 23–25: cada executor **confirma o defeito no código atual antes de corrigir**. Um item que já não se reproduz ganha mesmo assim o teste que o prova e é registrado como "já corrigido em <commit>".

O defeito comum é este: o texto pintado e os intervalos de formatação (`QTextLayout::FormatRange`) são montados em dois laços diferentes, que divergem.

- **A-preview-1:** o prefixo sintético (número de capítulo ou de nota) entra no texto, mas os intervalos de formatação não são deslocados. Negrito e cor aparecem fora de lugar.
- **A-preview-2:** a hifenização roda duas vezes, uma com o idioma do bloco (texto) e outra com o idioma de cada run (intervalos). Os comprimentos não batem.
- **A-preview-3:** com `note_restart`, o número da nota por página só existe nos intervalos, nunca no texto pintado.
- **CR-3-035:** `modelToDisplay` é construído em O(M·D), em todo caminho quente (medida, pintura, caret, hit-test).
- **A-preview-4:** um trecho (slice) de parágrafo é desenhado com a fonte e a cor do primeiro run, sem os intervalos por run.
- **A-preview-5:** a seleção é um único retângulo proporcional, não segue as linhas.
- **A-preview-6:** células de tabela são pintadas concatenando o texto dos runs, com fonte e cor fixas. `hidden`, `show_if`, `redact`, cor, tamanho e fonte do run são ignorados.
- **A-preview-7:** `gutter_mirror` desloca só a pintura das páginas pares. `hit_test` e `caret_geometry` não aplicam o mesmo deslocamento.
- **A-document-4:** `forEachFieldRun` não desce em `cell.nested`. Um `seq` numa tabela aninhada fica sem número.

## Decisões (fechadas)

| # | Decisão |
|---|---|
| D1 | **Uma única montagem de texto e formatação** em `blockTextLayout`. Um só laço produz, para cada pedaço exibido (prefixo sintético, run, número de nota, campo), o texto e o seu `FormatRange` com o início e o comprimento **no texto final**. O prefixo sintético tem o formato do primeiro run e desloca todos os intervalos seguintes. O número de nota com `note_restart` (ordinal por página) é decidido nesse mesmo laço, então o texto pintado mostra o ordinal da página (A-preview-1, A-preview-3). |
| D2 | **Hifenização por run, uma vez.** Cada run é hifenizado com o próprio idioma (`run.lang`, senão `block.lang`) dentro do laço da D1. A chamada no nível do bloco sai. Os hífens suaves inseridos fazem parte do texto exibido; `displayToModel`/`modelToDisplay` mapeiam esses caracteres para o offset de modelo do caractere seguinte (A-preview-2). |
| D3 | **`modelToDisplay` em tempo linear:** uma varredura com dois ponteiros sobre `displayToModel` (que é monotônico). O resultado é idêntico ao atual para toda entrada; nenhum caminho quente fica quadrático (CR-3-035). |
| D4 | **Trecho de parágrafo desenhado com os intervalos por run.** Quando uma marca pinta só parte do layout (`textFrom >= 0`, parágrafo que quebra entre páginas, TOC), o desenho usa os glyph runs do próprio `QTextLayout` para aquele intervalo (cada um com a sua fonte), e cor, sublinhado, tachado e realce vêm do intervalo de formatação, não do primeiro run (A-preview-4). |
| D5 | **Seleção por linha:** para cada `QTextLine` que intersecta `[selStart, selEnd)`, um retângulo de `cursorToX(max(start, line.start))` a `cursorToX(min(end, line.end))`, na altura da linha. Vale para parágrafo inteiro, trecho entre páginas e célula de tabela (A-preview-5). |
| D6 | **Células de tabela pelo mesmo caminho dos parágrafos:** o texto da célula é montado por `blockTextLayout` (D1), com a largura da célula. Valem fonte, tamanho, cor, negrito/itálico, `hidden`, `show_if`, `redact`, realce e campos. Sem formatação explícita, a célula mantém o padrão atual (Liberation Serif 10 pt, `#1A1F24`). A medida da altura da linha usa o mesmo layout da pintura (A-preview-6). |
| D7 | **Um só deslocamento de página:** uma função (ex.: `pageContentOffsetMm(doc, physicalPage)`) devolve o deslocamento horizontal do conteúdo da página, incluindo `gutter_mirror`. A pintura, o PDF, `DocumentLayout::hitTest`, `caret_geometry`, `hit_test` e os retângulos de seleção usam essa função. Numa página par espelhada, `hit_test` no ponto pintado de uma palavra devolve essa palavra (A-preview-7). |
| D8 | **Campos em tabelas aninhadas:** `forEachFieldRun` desce em `cell.nested` — um único nível, porque `NestedCell` guarda runs e não outra tabela — na ordem do documento. `seq`, `xref`, `stat` e a atualização de campos passam a ver esses runs (A-document-4). |
| D8b | **`filepath` nos formatos nativos:** o export DOCX escreve hoje a instrução crua `filepath`, que o Word não reconhece. DOCX e RTF passam a escrever `FILENAME \p`, e o import de `FILENAME \p` volta como `filepath` (sem `\p`, continua `filename`). ODT: `text:file-name text:display="full"`. Achado da revisão local do CodeRabbit no PR #17. |
| D9 | **Juízes:** cada decisão tem um teste que falha antes da correção, com prefixo `qa/wave26-`. Para a pintura, o teste gera `page_preview` em PNG e procura a cor esperada numa região (com tolerância a antialiasing), nunca por pixel exato. O PNG é decodificado só com a biblioteca padrão do Python (`zlib` + `struct`), num helper compartilhado `qa/png_probe.py`, sem dependência nova no `shell.nix`. A geometria usa `hit_test`/`caret_geometry`. A D3 tem um teste de tempo com folga larga (parágrafo de 100 000 caracteres: `page_preview` em menos de 10 s; hoje é quadrático). O PDF passa pelo mesmo pintor: o veraPDF PDF/UA-1 e o PDF/A-1b continuam conformes, e a extração de texto do PDF continua igual. A CI roda como root e com outra glibc (L22): nenhum teste depende disso. |
| D10 | Não regredir as ondas 12–25. |

## Regras de execução (lições L22, L25–L27)

- **CI:** a CI roda como root e com outra glibc. Nenhum teste depende de privilégio nem de `LD_PRELOAD` herdado.
- **Nós Claude:** não usam skills nem subagentes e nunca submetem uma saída provisória.
- **Build:** nenhum nó de agente roda build completo do zero. O QA longo fica nos gates de comando.
- **Git:** ninguém usa `git stash`, porque ele é compartilhado entre os worktrees.

## Tarefas e roteamento

O executor é o Grok Build 4.7. A Cursor (94%) não é usada. O Sonnet revisa.

| Nó | Executor | Itens | Arquivos |
|---|---|---|---|
| `lessons` | Sonnet | PRs #16–#17 | `docs/LESSONS.md`, `.cursor/rules/lessons.mdc` |
| `t1-text` | Grok Build 4.7 (alto) | D1, D2, D3 | `blockTextLayout` e auxiliares em `Preview.cpp` |
| `t2-paint` | Grok Build 4.7 (alto) | D4, D5, D6 | pintura de marcas, seleção e células em `Preview.cpp` |
| `t3-geometry` | Grok Build 4.7 (alto) | D7, D8, D8b | deslocamento de página em `Preview.cpp`, `DocumentLayout::hitTest`, caret/hit-test em `Session.cpp`, `forEachFieldRun` em `Document.cpp`; campo `filepath` em `Docx.cpp`, `RtfWriter.cpp`, `RtfReader.cpp` e `Odf.cpp` |

As três tarefas mexem em regiões diferentes de `Preview.cpp`. A T2 usa a API de `blockTextLayout` como ela é hoje; a integração junta as três.

Depois das tarefas, a sequência é:
1. `integrate-1` (Grok Build, effort medium);
2. gate de build e QA;
3. `t4-regress`;
4. gate;
5. loop de revisão: Sonnet revisa e Grok Build corrige;
6. adjudicação;
7. `qa-final`;
8. handoff;
9. `notion-sync`;
10. `open-pr`.

## Critério de pronto

- Cada decisão D1–D8b tem um teste `qa/wave26-*` que falharia sem ela.
- Um run em negrito vermelho depois de um prefixo de capítulo, um número de nota com `note_restart`, um run com outro idioma hifenizado, um trecho de parágrafo na página seguinte e uma célula com cor aparecem com a formatação certa no `page_preview`.
- Em página par com `gutter_mirror`, `hit_test` e `caret_geometry` batem com o que é pintado.
- Um `seq` numa tabela aninhada recebe o número certo.
- O veraPDF UA-1 e o A-1b continuam conformes.
- `scripts/qa-all.sh --gui --interop --pdf --formats` passa no local e na CI.
