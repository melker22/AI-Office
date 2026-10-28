# Onda 24 — Fidelidade DOCX/ODT e QR decodificável (spec arquitetural)

> Autor: Opus (Chief Architect), 2026-10-25.
>
> - **Origem:** auditoria 2026-10 (`docs/audit/fix-plan.md`, onda 24).
> - **Fleet:** `.cockpit/fleets/wave24-docx-odt.yaml`.
> - **Escopo:** Fase IA (core + QA). Não mexe em `src/app/`.
> - **Aprovação:** push, PR e merge só com `scripts/pr-merge-gate.py` limpo e as threads resolvidas (aprovado pelo utilizador em ask_05e099babed7).

## Diagnóstico

As provas estão em `docs/audit/module-sweep.md` e em `docs/audit/coderabbit-pr11.json`.

**Import DOCX perde:**
- formatação de caractere: tamanho, fonte, cor e `w:val="0"` em b/i/u/strike;
- o texto que vem antes de uma imagem inline;
- tamanhos de página reais;
- a tabela externa quando há tabela aninhada;
- `docProps/core.xml`;
- qualquer lista que não use `numId` 1 ou 2;
- campos dentro de `w:del`.

**Export DOCX omite:**
- `gridSpan`, tabela aninhada e legenda;
- escape de caracteres de controle e de `w:color`;
- ids de bookmark únicos.

**Revisões nativas DOCX:** são descartadas quando o JSON embutido existe (CR-1-009).

**ODT:**
- a nota absorve a citação;
- página real, fonte do run, células mescladas e tipo de lista se perdem;
- formas, gráficos, QR e formulários viram parágrafos;
- atributos ficam sem escape;
- bookmarks ficam fora do parágrafo.

**QR:** o código gerado não é decodificável.

## Decisões (fechadas)

| # | Decisão |
|---|---|
| D1 | **Import de run DOCX completo:** `w:sz` (meio-ponto → `size_pt`), `w:rFonts` (`w:ascii`, senão `w:hAnsi`), `w:color` (hex; `auto` = herdado), `w:highlight`. Para `b`, `i`, `strike` e `u`, um `w:val` igual a `0`, `false` (ou `none`, no caso de `u`) desliga o atributo. `w:bidi` só liga com valor vazio, `1`, `true` ou `on` (A-docx-4). |
| D2 | **Imagem inline no meio do parágrafo:** o texto antes vira um parágrafo, a imagem vira um bloco `image` e o texto depois vira um novo parágrafo, com o mesmo estilo. Nada se perde (A-docx-5). |
| D3 | **Página real:** o import DOCX e o ODT guardam `width_mm`/`height_mm`. O nome (`A4`, `Letter`, `Legal`, `A3`, `A5`) só é usado quando a medida bate com tolerância de 1 mm; senão o nome é `custom`. A orientação vem das medidas (A-docx-6, A-odf-4). |
| D4 | **Tabelas, ida e volta completa em DOCX e ODT:** tabelas aninhadas (import com pilha; export de `w:tbl`/`table:table` dentro da célula) e células mescladas (DOCX `w:gridSpan` + `w:vMerge`; ODT `table:number-columns-spanned` + `table:number-rows-spanned` + `table:covered-table-cell`), na escrita e na leitura (A-docx-7, A-docx-8, A-odf-6). |
| D5 | **Legenda em DOCX:** é exportada como parágrafo de estilo `Caption`, logo depois da tabela ou imagem, com o campo `SEQ` nativo. No import, um parágrafo `Caption` logo depois de uma tabela ou imagem vira a legenda desse bloco (A-docx-8). O ODT já trata legendas; não mude. |
| D6 | **Metadados:** o import DOCX lê `docProps/core.xml` (`dc:title`, `dc:subject`, `cp:keywords`, `dc:creator`) quando o JSON embutido não traz o valor (A-docx-9). |
| D7 | **Campo dentro de exclusão rastreada:** no export, um campo dentro de uma revisão `delete` sai como texto apagado (`w:delText` com o texto exibido), e a identidade do campo fica no JSON embutido. No import, um campo dentro de `w:del` vai para os runs apagados, nunca para o texto vivo (A-docx-10). |
| D8 | **Listas DOCX pela numeração real:** o import lê `word/numbering.xml` (`w:num` → `w:abstractNum` → `w:lvl` com `w:numFmt`). `bullet` vira lista com marcador; `decimal`, `lowerLetter`, `upperLetter`, `lowerRoman` e `upperRoman` viram o `listKind` correspondente; o nível vem de `w:ilvl`. O export continua escrevendo um `numbering.xml` coerente com isso (A-docx-11). |
| D9 | **Escape XML:** `xmlEscape` remove os caracteres C0 proibidos pelo XML 1.0 (todos abaixo de U+0020, exceto `\t`, `\n` e `\r`). `w:color` só é escrito se for hex de 6 dígitos válido; senão é omitido. Todos os atributos ODT, inclusive nomes de estilo, passam por `xmlEscape` (A-docx-12, PR #11). |
| D10 | **Revisões DOCX:** o import nativo (`w:ins`, `w:del`, `w:rPrChange`) é sempre lido. Quando o JSON embutido também traz revisões, os dois conjuntos são mesclados; uma marca nativa é descartada quando já existe uma revisão do JSON do mesmo `block_id` e `kind` cujo intervalo `[start,end)` é igual ao dela por igualdade exata **ou** a contém. A contenção importa porque uma revisão de formatação do JSON pode virar vários `w:rPrChange` nativos — um por run —, cada um com um intervalo menor dentro do intervalo original; a igualdade exata não bastaria para descartar esses casos. O ODT deduplica pelo mesmo `text:change-id`: uma marca nativa cujo id já existe numa revisão do JSON é ignorada (mecanismo diferente do DOCX, não o mesmo). Sem JSON, só a marcação nativa vale (CR-1-009). |
| D11 | **Ids DOCX:** `w:bookmarkStart`/`w:bookmarkEnd` usam `w:id` únicos no documento inteiro. O prompt do `FILLIN` é escapado uma única vez (PR #11). |
| D12 | **Notas ODT:** `text:note-citation` é a marca e não entra no corpo da nota. Só o conteúdo de `text:note-body` é importado (A-odf-1). |
| D13 | **Run ODT:** o import lê `style:font-name` (e `fo:font-family` como alternativa) para a fonte do run (A-odf-5). |
| D14 | **Tipo de lista ODT:** vem do `text:list-style` aplicado. `list-level-style-bullet` vira marcador; `list-level-style-number` com `style:num-format` `1`, `a`, `A`, `i` ou `I` vira o `listKind` correspondente; o nível é a profundidade de aninhamento (A-odf-7). |
| D15 | **Forma, gráfico, QR e formulário no ODT:** são exportados como imagem PNG desenhada pelo mesmo pintor da página (como no EPUB), com `svg:title`/`svg:desc` = `alt`. O bloco original vai para o blob `aioffice:extra`, e o import restaura o bloco original quando o blob existe; sem blob, fica a imagem. Nunca vira parágrafo vazio (A-odf-8). |
| D16 | **Bookmarks ODT:** `text:bookmark-start`/`end` (ou `text:bookmark`) ficam dentro do `text:p`/`text:h`, no offset certo (PR #11). |
| D17 | **QR decodificável:** o símbolo segue a ISO/IEC 18004. Os módulos de função (finders, timing, separadores, área de formato e módulo escuro) são reservados antes de posicionar os dados. Os bits de formato correspondem ao nível de correção e à máscara usados de fato, e a máscara é escolhida pela penalidade padrão. Usa byte mode nas versões 1 a 10, nível M. Um payload maior que a capacidade gera erro claro, não um símbolo inválido. **Juiz:** `zbarimg` (zbar, no `shell.nix`) decodifica o PNG exportado e devolve exatamente o payload (A-odf-10). |
| D18 | **Juízes da onda:** cada decisão tem um teste que falha antes da correção, com prefixo `qa/wave24-`. Os testes de ida e volta usam fixtures estrangeiras feitas à mão: DOCX no estilo Word e ODT no estilo LibreOffice, em `qa/fixtures/wave24-*`. Com `--interop`, o LibreOffice abre o nosso export e devolve a mesma estrutura. A CI roda como root e com outra glibc (L22): nenhum teste depende disso. Testes desabilitados na onda 23 pelo motivo "onda 24" são reabilitados. |
| D19 | Não regredir as ondas 12–23. |

## Tarefas e roteamento

Todas seguem o contrato de executor e as regras 3–5 de `docs/ORCHESTRATION.md`. A Cursor está em 73% da cota mensal; os executores são o Grok Build e o Composer.

| Nó | Executor | Itens | Custo | Risco |
|---|---|---|---|---|
| `lessons` | Sonnet | PR #13 | Baixo | — |
| `t1-docx-in` | Grok Build 4.7 (alto) | D1, D2, D3 (DOCX), D4 (import), D5 (import), D6, D7 (import), D8, D10 | Alto | Alto |
| `t2-docx-out` | Grok Build 4.7 (alto) | D4 (export), D5 (export), D7 (export), D9 (DOCX + `Xml.cpp`), D11 | Médio | Médio |
| `t3-odt` | Grok Build 4.7 (alto) | D3 (ODT), D4 (ODT), D9 (ODT), D12–D16 | Alto | Médio |
| `t4-qr` | Grok Build 4.7 (médio) | D17 (`Qr.cpp`) | Médio | Médio |
| `t5-qa` | Composer 2.5 | D18: `zbar` no `shell.nix`, `qa/check_qr.py`, fixtures estrangeiras, ctest/CI, reabilitação dos testes desabilitados | Baixo | Baixo |

Depois das tarefas: `integrate-1` → gate → `t6-regress` → gate → loop de revisão (Sonnet revisa, Grok Build corrige) → adjudicação → `qa-final` → handoff → `notion-sync` → `open-pr`. O Opus roda o gate de merge e confere as threads.

## Critério de pronto

- Cada D1–D17 tem pelo menos um `expect` ou checker que falharia sem a correção.
- As fixtures estrangeiras de DOCX e ODT fazem ida e volta, no nosso import/export e no LibreOffice com `--interop`, preservando: texto, formatação de run, listas por tipo, tabelas aninhadas e mescladas, legendas, notas, página, metadados e revisões.
- `zbarimg` decodifica o QR exportado em PNG, PDF e ODT.
- `scripts/qa-all.sh --gui --interop --pdf --formats` verde no local e na CI.
