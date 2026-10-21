# Onda 20 — Formatos novos: EPUB 3 (export) e RTF nativo (import + export) — spec arquitetural

> Autor: Opus (Chief Architect), 2026-10-21. Fleet: `.cockpit/fleets/wave20-epub-rtf.yaml` (lessons
> no início, contrato de executor, notion-sync, fallback do Claude conforme ORCHESTRATION). Fase IA:
> core + MCP, sem `src/app/`.

## Diagnóstico (`main` @ 8cc29d1)

- **RTF**: `Session::openFrom` abre `.rtf` só convertendo via LibreOffice (`LokEngine::convertTo`
  → ODT). Sem `soffice` não há RTF; salvar em RTF não existe.
- **EPUB**: inexistente. `export_html` é mínimo (`blockToHtml`: parágrafo/heading/lista simples), sem
  notas, tabelas fiéis, imagens embutidas nem idioma.
- **Validador de referência disponível**: `epubcheck` 5.3.0 (W3C/DAISY) no nixpkgs fixado — mesmo
  modelo da onda 19 com o veraPDF.

## Decisões (fechadas)

| # | Decisão | Motivo |
|---|---|---|
| D1 | **EPUB 3 só export** (`document_save` com `.epub`). Pacote: `mimetype` primeiro e **sem compressão**, `META-INF/container.xml`, `OEBPS/content.opf` (dc:title/creator/language/identifier estável = `urn:uuid` derivado do id do documento, `dcterms:modified`), `nav.xhtml` (TOC dos headings + landmarks), um XHTML por **Heading 1** (documento sem H1 → um arquivo), CSS gerado dos estilos, imagens no manifest com media-type correto | Mínimo que leitores e o epubcheck exigem; ler EPUB fica fora (D7) |
| D2 | **Conteúdo EPUB**: tabelas com `<th scope>`, listas `ol/ul` aninhadas, notas como `<aside epub:type="footnote">` + `noteref`, `lang`/`xml:lang` e `dir` por bloco e span, `alt` das imagens, campos já avaliados (texto estático), `hide_if` verdadeiro omitido, quebras de página ignoradas. Formas/gráficos/QR viram PNG renderizado pelo mesmo pintor da folha, com `alt`. Metadados de acessibilidade schema.org (`accessMode`, `accessibilityFeature`, `accessibilitySummary`) | Coerente com o PDF/UA da onda 19 |
| D3 | **Escritor XHTML próprio** `src/core/Xhtml.{h,cpp}` para o EPUB; `export_html` **não muda** nesta onda (migrá-lo é Todo novo) | Evita regredir quem usa `export_html` |
| D4 | **RTF nativo**, sem `soffice`: `src/core/RtfWriter.{h,cpp}` e `src/core/RtfReader.{h,cpp}`. Subconjunto: `\fonttbl`, `\colortbl`, `\stylesheet` (headings), parágrafo (alinhamento, recuos, espaçamento, `\rtlpar`, `\lang`), caractere (b/i/ul/strike/super/sub, fonte, tamanho, cor, realce), listas via `\listtable`/`\listoverridetable`, tabelas (`\trowd`/`\cellx`/`\intbl`/`\cell`/`\row`, linha de cabeçalho `\trhdr`), `\page`, notas (`\footnote`), imagens (`\pict\pngblip`), campos (`\field` PAGE/NUMPAGES/HYPERLINK/…), marcadores (`\bkmkstart/end`), Unicode (`\uN` + `\ucN`). Revisões/comentários/formas: exportados como texto e listados em `warnings[]` | O que Word e LibreOffice usam de fato |
| D5 | **Leitor RTF seguro (L1)**: limite de profundidade de grupos, de tamanho de `\pict` e de `\bin N`; `\'hh` decodificado pelo `\ansicpg` (QStringDecoder); destinos `\*` desconhecidos ignorados; nunca crash, erro claro para RTF malformado. `.rtf` abre **só pelo nativo** (sem cair silenciosamente no `soffice`); `.doc` continua via `soffice` | Entrada externa é hostil |
| D6 | **Juízes**: `epubcheck` no `shell.nix` → ctest `LABELS epub` exige **0 erros e 0 avisos** + checagens estruturais (`qa/check_epub.py`: mimetype stored primeiro, nav, lang, notes); ctest `LABELS rtf`: ida e volta própria (aio→rtf→aio equivalente em texto/formatação/estrutura), fixtures estrangeiras escritas à mão (estilo Word e LibreOffice), e com `--interop` o LibreOffice converte nosso RTF→ODT e o RTF dele→nosso import, comparando. `scripts/qa-all.sh --formats` liga os labels; a CI roda `playbook\|roundtrip\|pdf\|epub\|rtf` | Mesmo padrão da onda 19 |
| D7 | **Fora**: importar EPUB, DOC nativo (binário caro; segue via `soffice`), revisões/comentários em RTF | Escopo honesto |
| D8 | Não regredir ondas 12–19 (ODT/DOCX, campos, revisões, PDF) | — |

## Tarefas e roteamento (contrato de executor; regras 3–5)

| Nó | Executor | Por quê | Custo | Risco | Depende |
|---|---|---|---|---|---|
| `lessons` | Sonnet | Lições do PR #7 | Baixo | — | — |
| `t1-epub` | Cursor Grok 4.7 | D1–D3: Epub + Xhtml + rasterização de formas | Alto | Médio | lessons |
| `t2-rtf-writer` | Composer 2.5 | D4 export (mecânico, extenso) | Médio | Médio | lessons |
| `t3-rtf-reader` | Cursor Grok 4.7 | D4/D5 parser seguro | Médio | **Alto** | lessons |
| `t4-formats-qa` | Composer 2.5 | D6: epubcheck no Nix, check_epub.py/check_rtf.py, fixtures, ctest, CI | Baixo | Baixo | lessons |
| `integrate-1` | Composer 2.5 | Merge pelos 4 hashes | Baixo | Médio | t1–t4 |
| gate `build-1` | comando | `fleet-verify-merged.sh 6 && qa-all.sh --gui` | 0 | — | integrate-1, lessons, t1–t4 |
| `t5-compliance` | Cursor Grok 4.7 | Leva `--formats --interop` a verde sem afrouxar | Médio | Médio | build-1 |
| gate `qa-full` → loop `review` (Sonnet; Grok Build corrige) → `qa-final` → `handoff` → `notion-sync` → humano → `open-pr` | — | — | — | — | — |

## Critério de pronto

- `epubcheck`: **0 erros, 0 avisos** em todo EPUB da suíte (headings em capítulos, tabela com
  cabeçalho, listas aninhadas, notas, imagens com alt, forma/gráfico rasterizado, parágrafo RTL, span
  em outro idioma, campo `if`, parágrafo `hide_if`).
- RTF: ida e volta própria preserva texto, formatação de caractere e parágrafo, listas, tabelas, notas,
  imagens, campos e marcadores; fixtures estilo Word e LibreOffice importam com a estrutura esperada;
  RTF malformado/aninhado demais devolve erro, não crash.
- `--interop` (só RTF; o EPUB já é julgado pelo epubcheck): o LibreOffice abre nosso RTF com texto e
  estrutura equivalentes, e o RTF gerado pelo LibreOffice importa nativamente com texto e estrutura
  equivalentes.
- `scripts/qa-all.sh --gui --interop --pdf --formats` verde local; CI verde com os labels novos.
