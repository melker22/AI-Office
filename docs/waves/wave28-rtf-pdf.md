# Onda 28 — RTF fiel (revisões, comentários e formas) e subconjunto de fontes no PDF (spec arquitetural)

> Autor: Opus (Chief Architect), 2026-11-09.
>
> - **Origem:** `docs/WRITER_FEATURE_CATALOG.md`, Todo IA (Writer): "Revisões e comentários em RTF (marcação fiel, não só
>   texto)" e "Subconjunto de fontes no PDF"; linha "RTF nativo": "revisões, comentários e formas ainda saem como texto
>   plano com aviso".
> - **Fleet:** `.cockpit/fleets/wave28-rtf-pdf.yaml`.
> - **Escopo:** Fase IA (core + QA). Não mexe em `src/app/`.
> - **Executores:** Grok da Cursor (`grok-4.7-high`) nas tarefas e correções, porque o Grok Build está sem cota confirmada;
>   Composer na integração e no handoff; Sonnet na revisão. Reserva: DeepSeek V4.1 Flash.
> - **Aprovação:** push e PR pré-aprovados. O merge só sai com `scripts/pr-merge-gate.py` limpo e as threads resolvidas.

## Diagnóstico

Medido em 2026-11-09 no build do `main` e com LibreOffice.

- **RTF, saída:** `src/core/RtfWriter.cpp` transforma comentário em `[autor: texto]` no corpo, ignora as revisões com um
  aviso global e escreve forma como texto plano.
- **RTF, entrada:** `src/core/RtfReader.cpp` descarta `\annotation`, `\shp`, `\shpinst` e `\revtbl`. O texto de `\deleted`
  entra no corpo como se fosse texto vivo.
- **Marcação nativa confirmada no LibreOffice:** `\revised`/`\revauth`/`\revdttm` vira `text:insertion`, `\deleted` vira
  `text:deletion`, `\atrfstart`…`\annotation` vira `office:annotation` com autor e iniciais, e `\shp` com `shapeType` 1, 3
  e 20 vira retângulo, elipse e linha. `\crauth` é aceito, mas o LibreOffice não cria `text:format-change`.
- **PDF:** o catálogo está errado. O `QPdfWriter` **já embute só os glifos usados**: um PDF com "Hello" tem 8 KB, e árabe mais
  CJK têm 18 KB, com a Noto CJK de 16 MB na origem. O que falta é a marcação: o nome da fonte não traz o prefixo `ABCDEF+`
  que a norma exige (ISO 32000-1, 9.6.4), e o `pdffonts` reporta `sub: no`.
- **PDF/A-1:** assim que a fonte é marcada como subconjunto, a norma exige um `/CIDSet` no descritor (ISO 19005-1, 6.3.5). O
  Qt não escreve esse stream.

## Decisões (fechadas)

### RTF, saída (`RtfWriter`)

| # | Decisão |
|---|---|
| D1 | **Tabela de autores.** `{\*\revtbl{Unknown;}{Autor A;}…}` no cabeçalho, só quando há revisões, com o índice 0 reservado e os autores em ordem de primeira aparição. Autor vazio usa o índice 0. O nome passa pelo mesmo escape de texto do corpo (não ASCII como `\uN`), e um `;` literal no nome sai como `\'3b`, porque o `;` cru termina a entrada. |
| D2 | **Revisões nativas.** Mesmo invariante da onda 17 D1. `insert`: `{\revised\revauthN\revdttmM …}` sobre `[start,end)`. `delete`: no ponto `start`, `{\deleted\revauthdelN\revdttmdelM …}` com uma fatia por item de `deletedRuns`, cada uma com a própria formatação. `format`: cada run da faixa recebe `\crauthN\crdateM`, e a primeira recebe `{\*\aiorevfmt <JSON>}` com `payload` e `priorRuns`. A ordem no mesmo offset é a do export DOCX. Data: DTTM em UTC, precisão de minuto, omitida quando `timestamp` é 0. Valem para os blocos escritos por `emitMarkedRuns` e para as notas. Revisão em outro tipo de bloco gera um aviso que cita o id da revisão. O aviso global "tracked revisions exported as plain text" sai. |
| D3 | **Comentários nativos.** O comentário é do bloco inteiro. Para cada um, `{\*\atrfstart N}` antes do primeiro run, `{\*\atrfend N}` depois do último e, em seguida, `{\*\atnid INICIAIS}{\*\atnauthor AUTOR}\chatn{\*\annotation{\*\atnref N}{\*\aiocid ID}{\*\aiocparent ID}{\*\aiocresolved 1}\pard\plain TEXTO}`. `N` é único no documento, as iniciais vêm do autor, e `aiocparent`/`aiocresolved` só saem quando têm valor. `\n` no texto vira `\par`. O `[autor: texto]` no corpo acaba. Tabela, quebra e imagem não têm onde ancorar: o comentário sai num parágrafo portador logo depois do bloco, marcado com `{\*\aiocarrier}`. |
| D4 | **Formas nativas.** `shapeKind` `rect`, `ellipse` e `line` viram `{\shp{\*\shpinst\shpleftA\shptopB\shprightC\shpbottomD\shpfhdr0\shpbxcolumn\shpbypara\shpwr1\shpfblwtxt0\shpzZ\shplidL…}}` num parágrafo próprio, com `shapeType` 1, 3 e 20. Tamanho de `widthMm` e `heightMm` em twips, com o mesmo padrão que o preview usa quando são 0. `fillColor` e `lineColor` em COLORREF decimal (BGR). Sem `fill`, `fFilled` 0; sem `stroke`, `fLine` 0. Texto da forma em `{\shptxt …}`. `shplid` único por documento. `groupId` ou `connectTo` preenchidos geram aviso, porque não são escritos. QR e dropdown continuam em texto plano, e o aviso passa a citar só esses dois. |

### RTF, entrada (`RtfReader`)

| # | Decisão |
|---|---|
| D5 | **Revisões.** Lê `\revtbl` (sem dividir em `\'3b`), `\revised`, `\deleted`, `\crauth`/`\crdate` e os DTTM. `\deleted` **não entra em `runs`**: vira `delete` de ponto com `deletedRuns`. Fatias contíguas de mesmo tipo, autor e data formam uma revisão só. `{\*\aiorevfmt}` restaura `payload` e `priorRuns`; sem ele (RTF de terceiros), `priorRuns` fica vazio e o reject devolve o erro da onda 17 D2. Ids vêm de `nextPrefixedId`. Deleção que atravessa parágrafos gera uma revisão por bloco. Em célula de tabela o texto fica com a marcação de revisão descartada e aviso, e o texto de `\deleted` não entra na célula. O documento importado passa em `validate`. |
| D6 | **Comentários.** `\annotation` deixa de ser ignorado e nunca entra no corpo. O comentário fica no bloco onde está o `\chatn` (em tabela, no bloco da tabela). Autor vem de `\atnauthor`, senão de `\atnid`. `\par` interno vira `\n`. `aiocid`, `aiocparent` e `aiocresolved` restauram id, resposta e estado; sem eles, o id vem de um contador que varre os ids em uso. `parentId` que não existe vira comentário de raiz, com aviso. Um parágrafo com `{\*\aiocarrier}` entrega os comentários ao bloco anterior e não vira bloco. |
| D7 | **Formas.** `\shp` com `shapeType` 1, 3 ou 20 vira bloco `shape` com tamanho, `zIndex`, `fill`, `stroke` e texto de `\shptxt`. Outro `shapeType` e `\shpgrp` geram um aviso por tipo e são ignorados. `\shprslt` é ignorado quando a forma foi lida. `\pict` dentro de `\shp` continua como hoje. |
| D8 | **Entrada hostil.** Valem `kMaxDepth`, `kMaxBlocks` e `kMaxFile`. Índice de autor fora da tabela usa "Unknown", com aviso. DTTM inválido vira `timestamp` 0. JSON de `aiorevfmt` inválido é ignorado com aviso. Tamanho de forma negativo ou acima de 10.000 mm é limitado com aviso. Nada disso derruba o import nem deixa o documento inválido em `validate`. |

### Privacidade

| # | Decisão |
|---|---|
| D9 | **Marcação nova não abre caminho para conteúdo oculto.** Bloco que hoje sai com `\v` continua com `\v`, inclusive em `\revised` e `\deleted`. Texto de um run com `redact` não aparece em nenhuma marcação nova: `deletedRuns`, `priorRuns`, `aiorevfmt` e comentários. O executor confere o que `Session::redactRuns` faz com revisões sobrepostas e cobre com teste. |

### PDF

| # | Decisão |
|---|---|
| D10 | **Marcar o subconjunto.** Toda fonte com `/FontFile2` recebe um prefixo de 6 letras maiúsculas e `+` em três lugares, sempre iguais entre si: `/BaseFont` do Type0, `/BaseFont` do CIDFont e `/FontName` do descritor. A marcação é um passo novo em `PdfPostProcess`, em toda exportação (com ou sem `pdfa`/`ua`), e roda **depois** de `pdfPostProcessFixToUnicode`, que casa pelo nome da face. Fonte sem programa embutido fica sem prefixo. |
| D11 | **Prefixo determinístico.** Vem de um hash do nome original da fonte com o programa de fonte embutido. Mesma entrada gera o mesmo prefixo; conjuntos de glifos diferentes geram prefixos diferentes; duas fontes do mesmo PDF nunca compartilham prefixo. |
| D12 | **`/CIDSet` em PDF/A.** Com `pdfa: true`, cada descritor marcado recebe `/CIDSet`: stream com um bit por CID (MSB primeiro), ligados de 0 até `numGlyphs - 1` do `maxp` do programa embutido. Sem `pdfa`, não é escrito. Se o veraPDF recusar o resultado, o nó para e reporta `PRECISA OPUS`, sem marcar a fonte de outro jeito. |
| D13 | **Sem regressão de texto.** `pdftotext` devolve o mesmo texto de antes em latim, árabe e CJK; `pdffonts` reporta `emb yes`, `sub yes` e `uni yes`; `qpdf --check` passa. |

### Juízes e documentação

| # | Decisão |
|---|---|
| D14 | **Juízes `qa/wave28-*`.** Cada decisão tem um teste que falha antes da correção. RTF: ida e volta com os três tipos de revisão, comentários com resposta e resolvido, as três formas; um RTF de terceiro escrito à mão no teste (sem binário opaco); um RTF hostil por item da D8. Interop (`--interop`): o LibreOffice abre o nosso RTF e o ODT resultante tem `text:insertion`, `text:deletion`, `office:annotation` com autor e as três formas. PDF: `qa/check_pdf.py` ganha `pdffonts.all_subset`, `fonts.same_name_everywhere`, `fonts.max_program_bytes` e a verificação do `/CIDSet`; veraPDF PDF/A-1b e PDF/UA-1 continuam conformes. A CI pode rodar como root e com outra glibc: nenhum teste depende disso. |
| D15 | **Documentação.** A linha "RTF nativo" e "Incorporar fontes no PDF" do catálogo e `docs/mcp-tools.md` dizem o que passa a existir e o que continua de fora: QR e dropdown em texto plano, revisão fora de corpo de texto, grupos de formas e `text:format-change` no LibreOffice. |
| D16 | Não regredir as ondas 12–27. |

## Regras de execução

- **Nós de agente:** fazem tudo no turno, sem skills nem subagentes, e nunca submetem saída provisória.
- **Build:** só o alvo do CLI no worktree isolado, um nó por vez (`max_parallel: 1`); o QA completo é do gate.
- **Git:** ninguém usa `git stash`.
- **CI:** nenhum teste depende de privilégio nem de `LD_PRELOAD` herdado.

## Tarefas e roteamento

| Nó | Executor | Itens | Arquivos exclusivos |
|---|---|---|---|
| `lessons` | Sonnet | PRs desde a última processada | `docs/LESSONS.md`, `.cursor/rules/lessons.mdc` |
| `t1-rtf-out` | Grok da Cursor | D1–D4, D9 (escrita) | `src/core/RtfWriter.{h,cpp}` |
| `t2-rtf-in` | Grok da Cursor | D5–D8, D9 (leitura) | `src/core/RtfReader.{h,cpp}` |
| `t3-pdf` | Grok da Cursor | D10–D13 | `src/core/PdfPostProcess.{h,cpp}`, a chamada em `src/core/Preview.cpp`, `qa/check_pdf.py` |

`CMakeLists.txt`, `docs/mcp-tools.md` e os arquivos novos em `qa/` são comuns: todas podem editar, e a integração junta. O
teste de ida e volta RTF completo depende de T1 e T2 juntas e fecha em `t4-regress`. Antes disso, a T1 valida pelo
LibreOffice e a T2 por RTF escrito à mão.

Depois das tarefas: `integrate-1` (Composer) → gate de build e QA → `t4-regress` (Grok da Cursor) → gate → loop de revisão
(Sonnet revisa, Grok da Cursor corrige) → adjudicação (Sonnet) → `qa-final` → handoff (Composer) → `notion-sync` → `open-pr`.

## Critério de pronto

- Cada decisão D1–D13 tem um teste `qa/wave28-*` que falharia sem ela.
- Revisões, comentários e formas fazem ida e volta pelo RTF, e o LibreOffice lê a marcação nativa.
- Um RTF hostil falha ou importa com aviso, sem derrubar o processo.
- `pdffonts` reporta `sub yes` em todas as fontes embutidas; veraPDF PDF/UA-1 e PDF/A-1b conformes.
- `scripts/qa-all.sh --gui --interop --pdf --formats` passa no local e na CI.
