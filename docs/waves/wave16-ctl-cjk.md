# Onda 16 — CTL/CJK (bidi + métricas) — spec arquitetural

> Autor: Opus (Chief Architect), 2026-09-25. Executado pela Fleet `.cockpit/fleets/wave16-ctl-cjk.yaml`.
> Roteamento de modelos: [`docs/ORCHESTRATION.md`](../ORCHESTRATION.md).

## Diagnóstico (estado real do código, não do catálogo)

1. **Shaping e bidi já existem de graça.** `Preview.cpp` desenha cada bloco com `QTextLayout` (HarfBuzz + UAX #9 bidi + UAX #14 quebra de linha). Árabe/hebraico/CJK já são *formados* corretamente. Nenhuma lib nova (ICU/FriBidi/raqm) é necessária.
2. **Caret e hit-test são interpolação linear** (`DocumentLayout::caret` / `hitTest`): `x = offset/len * largura`. Ignoram glifos reais **e quebra de linha** — estão errados até para parágrafos LTR multilinha. Para RTL são inutilizáveis. **Este é o núcleo da onda.**
3. **Sem direção de parágrafo.** Nenhum `QTextOption::setTextDirection`; não existe `Block.dir`. Um parágrafo árabe alinha/ordena como LTR.
4. **Setas no GUI são lógicas e por code unit** (`DocumentCanvas` ±1): quebram pares substitutos/emoji/marcas combinantes e andam "ao contrário" em RTL.
5. **Ruby** é posicionado por `horizontalAdvance` acumulado por run: errado com wrap e RTL.
6. **Kashida está marcada Done no catálogo mas não há uma linha de código.** Qt (`QTextEngine::justify`) insere kashida ao justificar árabe — precisa ser **verificado**, não implementado à mão.
7. `writing_mode: vertical` hoje = bloco rodado 90°. Vertical CJK real (glifos em pé, tate-chu-yoko) **fica fora** desta onda (ver D6).

## Decisões (fechadas — executores não reabrem)

| # | Decisão | Motivo |
|---|---------|--------|
| D1 | Continuar com `QTextLayout`; zero dependências novas | Já faz shaping/bidi/line-break; Nix e Windows ficam simples |
| D2 | **Uma** função constrói o layout de texto de um bloco (`BlockTextLayout`), usada por pintura, medição, caret, hit-test e ruby | Hoje há 2 construções duplicadas (medir ~l.231 e pintar ~l.1345) + interpolação; a divergência é a origem dos bugs |
| D3 | `BlockTextLayout` carrega um **mapa offset-modelo ↔ offset-display** | O texto exibido ≠ `plainText()` (soft hyphens da hifenação, campos renderizados, `skip` de blocos partidos entre páginas) |
| D4 | Caret = `QTextLine::cursorToX`; hit = `QTextLine::xToCursor`; movimento = `QTextLayout::leftCursorPosition/rightCursorPosition` (visual) e `next/previousCursorPosition` (grafema), expostos no **core** (`DocumentLayout`) e por MCP. **Ligar isso às teclas do `DocumentCanvas` é Fase Humano** (item "Navegação só de teclado no canvas"), não desta onda | APIs oficiais do Qt; respeitam bidi e clusters. Onda 16 = Fase IA: só core + MCP |
| D5 | Modelo: `Block.dir` = `auto`\|`ltr`\|`rtl` (default `auto`, omitido no JSON quando `auto`) + `align` aceita `start`/`end` (lógicos). `left`/`right` continuam físicos. **Sem direção por run** — o algoritmo bidi resolve | Compatível com arquivos existentes; espelha `w:bidi` e `style:writing-mode` |
| D6 | Vertical CJK real: **fora** (continua Partial). Rotação 90° atual fica como está | `QTextLayout` não faz vertical; exigiria motor próprio — decisão futura do utilizador |
| D7 | Round-trip: DOCX `w:pPr/w:bidi` ↔ `dir=rtl`, `w:jc start/end`; ODT `style:writing-mode="rl-tb"\|"lr-tb"` ↔ `dir`, `fo:text-align start/end`. `w:rtl` em run: só leitura, ignorado | Mínimo que o Word/LibreOffice precisam para abrir certo |
| D8 | Testes headless: novas tools MCP `caret_geometry {id, offset}` → `{page, x_mm, y_mm, line, rtl}`, `hit_test {page, x_mm, y_mm}` → `{id, offset}` e `caret_move {id, offset, move: left\|right\|next\|prev}` → `{offset}`. `play` ganha operadores em `expect`: `{"$gt"|"$lt"|"$gte"|"$lte"|"$ne": n}` | Sem isso a CI não prova nada sobre geometria; igualdade exata em pixels é frágil |
| D9 | Fontes de teste determinísticas: `noto-fonts` + `noto-fonts-cjk-sans` no `shell.nix` e `FONTCONFIG_FILE` apontando para elas | Sem fonte árabe/CJK a CI mede glifos `.notdef` e passa por acaso |
| D10 | O tagging de PDF da onda 15 **não pode regredir**: a ordem de pintura (`orderedBlocksForPage`) e o único `setClipRect` por bloco ficam intactos | `wave15-pdf-tagging.json` é o guarda |

## Tarefas e roteamento

| Nó da Fleet | Executor | Por quê | Custo | Risco | Depende de |
|---|---|---|---|---|---|
| `t1-layout-core` | Cursor Grok (`cursor-grok-4.6-high`) | Refatoração central em `Preview.cpp` (1,6k linhas) + novas tools; raciocínio de código médio-alto | Alto | **Alto** (toca pintura + PDF) | — |
| `t3-model-dir` | Composer 2.5 | Campo novo + JSON + schema MCP + operadores do `play`: mecânico | Baixo | Baixo | — (paralelo a t1) |
| `integrate-1` | Composer 2.5 | Merge de 2 branches com áreas quase disjuntas | Baixo | Baixo | t1, t3 |
| gate `build-1` | comando | `scripts/qa-all.sh` | 0 tokens | — | integrate-1 |
| `t4-wire-rtl` | Cursor Grok | Liga `dir` ao layout, ruby, movimento visual/grafema no core + `caret_move`, verificação de kashida (sem GUI) | Médio | Médio | build-1 |
| `t5-roundtrip` | Composer 2.5 | Mapear 4 atributos em Docx/Odf + `check_roundtrip.py` | Baixo | Baixo | build-1 (paralelo a t4) |
| `integrate-2` | Composer 2.5 | Merge disjunto | Baixo | Baixo | t4, t5 |
| `t6-qa` | Composer 2.5 | Playbooks `qa/wave16-ctl-*.json` + fontes Nix | Baixo | Baixo | integrate-2 |
| gate `qa-full` | comando | `scripts/qa-all.sh --gui` | 0 tokens | — | t6 |
| loop `review` | Sonnet revisa · Grok Build (`grok-4.7`) corrige, até 3 passadas | QA contra esta spec; Grok Build = executor de continuidade | Médio | — | qa-full |
| `opus-audit` (opcional, `--arg opus_audit=true`) | Opus | Só se o diff de `Preview.cpp`/`PdfPostProcess.cpp` for grande | Alto | — | review |
| `handoff` | Composer 2.5 | `AI_HANDOFF.md`, catálogo (corrigir kashida), sem Notion | Baixo | — | review |
| gate humano `approve-pr` → `open-pr` | Sonnet | Push + PR é externamente visível | Baixo | — | handoff |

## Critério de pronto

- `scripts/qa-all.sh --gui` verde (todos os `qa/wave*.json`, incluindo os novos e o `wave15-pdf-tagging`).
- Parágrafo LTR multilinha: caret em offset do meio da 2ª linha tem `line == 1` (hoje daria linha 0).
- Parágrafo `dir: rtl` árabe: `caret_geometry` no offset 0 tem `x_mm` > metade da largura útil; offset final < metade.
- Texto misto (`abc אבג def`): `hit_test(caret_geometry(o)) == o` para todo offset de fronteira de grafema.
- DOCX/ODT exportados reabrem com `dir` e `align start/end` preservados.
- Árabe justificado: largura da linha não-final == largura útil (±0,5 mm) — prova de que o Qt justificou (com kashida ou espaço); o relatório diz qual.
