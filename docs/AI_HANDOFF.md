# AI Handoff — AI Office Writer

> **Propósito:** log operacional entre sessões de IA. Leia este arquivo **antes** de qualquer tarefa; atualize ao **concluir** (ou ao parar com trabalho parcial).
>
> **Não duplicar:** documentação de produto fica em `DOCUMENTACAO_COMPLETA.md` e `DESIGN_CHANGES.md`.
>
> **Manutenção:** manter no máximo ~20 entradas aqui; mover as mais antigas para `AI_HANDOFF_ARCHIVE.md`.

---

## Template (copiar para o topo)

```
## YYYY-MM-DD — Cursor — <título curto>

**Objective:**

**Done:**

**Decisions:**

**Next suggested step:**

**Commits:** nenhum (usuário não pediu) | `<hash>` <mensagem>
```

---

## 2026-08-30 — Cursor — Fase IA wave 10: objectos, página, revisão

**Objective:** Continuar a Fase IA (JSON+IDs + MCP + `page_preview`). Não descartar formas/QR/ruby/livreto por serem “grandes”. Sem QML novo.

**Done:**
- Modelo: `shape`/`chart`/`qr`/`equation`/`form`; flip/crop/sombra; ruby; `show_if`; redact; bibliografia JSON.
- Preview: QR, barras, formas, conectores, z-order, numeração de capítulos/linhas, tom/grelha, endnotes de secção, PDF `from`/`to`/`booklet`.
- MCP: insert_*, stats, compare, a11y, autotext, snapshots, repeat_last, thesaurus, user.dic, grammar, align, cite, figure_index, revision_view.
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave10-ia-continue.json` (`valid: true`, 3 páginas).
- Notion: **175 Done / 20 Partial / 46 Todo**. CSL completo, tagged PDF, órfãs reais = Partial.

**Decisions:** QR é preview byte-mode curto (não ISO-scanner). Sem chrome QML. Sem push. Testes golden só quando o utilizador pedir.

**Next suggested step:** Onda 11 no que ainda for IA no board (galeria, track format, reinício vivo de notas) ou Fase Humano no chrome. Testes à parte.

**Commits:** `b045fd1` feat: add Writer objects, page extras, and review MCP tools

---

## 2026-08-30 — Cursor — Fase IA fechada (onda final JSON+MCP)

**Objective:** Implementar o que faltava da Fase IA (JSON + MCP + `page_preview`). Parar **antes** dos testes — o utilizador dá instruções à parte.

**Done:**
- Colunas desiguais (`column_widths`), capítulo no header, watermark, papéis extra (B4/B5/Executive/Folio/Statement).
- Vars / read-only / subject / keywords; campos `chapter` / `var` / `input`.
- Fórmulas de célula (`sum|avg|count`), estilos de tabela, `table_auto_width`, header de tabela repetido entre páginas.
- `export_html`, `convert_note`, `toc_update` níveis, `insert_index`, imagem no header, `rotate`/`opacity`.
- Alinhados no board itens já no motor (split/join, accept-all, caps, entrelinha, quebra de coluna, mail_merge, page PNG, wrap, filename, word_count).
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave9-ia-close.json` (`valid: true`, fórmula 174, 3 páginas).
- Notion: **126 Done / 17 Partial / 98 Todo**. Chrome = Humano; packaging/CRDT = Infra.

**Decisions:** Fase IA = o que a IA usa via MCP. Sem QML novo. Tagged PDF, CSL, CJK, charts, CRDT ficam fora. **Não correr a bateria de testes** até o utilizador pedir.

**Next suggested step:** Esperar instruções do utilizador para testes (golden PNG, round-trip, etc.). Depois Fase Humano no chrome Tinta & Papel.

**Commits:** `b46b66f` feat: close Writer IA phase with columns, fields, and table tools

---

## 2026-08-30 — Cursor — Fase IA wave 8: células, break-before, recuos, gutter

**Objective:** Continuar JSON+MCP+`page_preview` após wave 7. Sem chrome QML. Sem push.

**Done:**
- `break_before` no bloco + `apply_paragraph_format`; layout começa página nova.
- `keep_with_next` (já no motor) alinhado no board; recuos left/right/first no preview.
- Célula: `border_pt`/`border_color`/`background`/`v_align`/`padding_mm` via `set_cell`.
- `gutter_mm` (margem esquerda) alinhado no board.
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave8-cells-breaks.json`.

**Decisions:** Gutter sem mirror ímpar/par. Órfãs/viúvas ficam (parágrafos não partem entre páginas). Sem QML.

**Next suggested step:** repeat header de tabela, estilos de tabela, secções ímpar/par, ou começar Fase Humano no chrome Tinta & Papel.

**Commits:** `ed9f9cc` feat: add cell format, break-before, and paragraph indents

---

## 2026-08-30 — Cursor — Fase IA wave 7: promote, listas, tabela, export MD/txt

**Objective:** Continuar JSON+MCP+`page_preview` após wave 6. Sem chrome QML. Sem push.

**Done:**
- `promote_heading` / `demote_heading` (parágrafo → Heading 1; H2 ↔ H1).
- `wrap_list` / `unwrap_list`.
- `text_to_table`, `table_to_text`, `split_table` (e `table_sort` já existia — alinhado no board).
- `export_markdown`, `export_text`, `copy_markdown`.
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave7-structure-export.json`.

**Decisions:** Export MD/txt não mudam o canvas; `page_preview` cobre promote/listas/tabelas. Sem QML. PDF tagged continua Partial.

**Next suggested step:** keep-with-next/break-before visíveis se faltar, secções ímpar/par, bordas de célula, ou começar Fase Humano no chrome Tinta & Papel.

**Commits:** `258d661` feat: add heading promote, list wrap, and Markdown export

---

## 2026-08-30 — Cursor — Fase IA wave 6: HTML, molduras, ímpar/par, AutoFormat

**Objective:** Continuar waves 3–6 (JSON+MCP+`page_preview`). Sem chrome QML. Sem push.

**Done:**
- Cabeçalho/rodapé ímpar, par e primeira página (`set_header`/`set_footer` `even`/`first`).
- `insert_html` (HTML sanitizado via QTextDocument) e `insert_plain`.
- `insert_frame` + `create_style kind=frame`; `autoformat` (aspas, `--`, fracções, URL).
- `join_blocks`; `load_styles {path}`.
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave6-html-frames.json`.

**Decisions:** PDF tagged continua Partial (QPainter não gera árvore de estrutura). AutoFormat é MCP de documento, não GUI. Sem QML.

**Next suggested step:** PDF tagged (se houver API Qt), secções ímpar/par, promote heading, wrap_list, ou começar Fase Humano no chrome Tinta & Papel.

**Commits:** `fbf879d` feat: add HTML paste, text frames, and odd/even headers

---

## 2026-08-29 — Cursor — Fase IA wave 5: kerning, idioma, find, estilos, etiquetas

**Objective:** Continuar waves 3–6 (JSON+MCP+`page_preview`). Sem chrome QML. Sem push.

**Done:**
- Kerning/tracking (`run.kerning`) no preview; idioma em run/bloco.
- `search` por formato (bold/italic/font/highlight/lang/…); `goto`; `select_by_style`.
- Listas: `list_restart`, `list_bullet`, `create_style kind=list`.
- Página: `kind=page`, `number_format`/`number_start`, `insert_break page_restart`.
- `StyleDef.next` + `split_block`; `update_style`; data com formato Qt; `insert_labels`.
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave5-styles.json`.

**Decisions:** Ligaduras ficam no HarfBuzz default (Qt 6.11 sem `QFont::PreferLigatures`). Nº impresso ≠ página física. Sem QML. PDF tagged continua Partial.

**Next suggested step:** Waves 3–6 restantes (PDF tagged, moldura, ímpar/par, colar HTML, AutoFormat). Fase Humano só quando a IA estiver plena.

**Commits:** `c52aa48` feat: add kerning, find-by-format, list/page styles, and labels

---

## 2026-08-29 — Cursor — Fase IA wave 4: runs, checkbox, carácter, envelope

**Objective:** Continuar waves 3–6 (JSON+MCP+`page_preview`). Sem chrome QML. Sem push.

**Done:**
- Fonte/tamanho/cor e realce pintados no preview; `small_caps` / `caps`; `hidden`; `char_style`.
- Checkbox: `insert_list kind=checkbox`, `toggle_check`.
- `clear_format`, `paint_format`, `insert_special` (nbsp/shy/emdash/…).
- Fundo da página (`background`); papéis DL/C5/C6; template `envelope`.
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave4-runs.json`.

**Decisions:** Texto oculto some no preview mas fica no JSON. Envelope = DL landscape; etiquetas ficam Partial. Sem QML.

**Next suggested step:** Waves 3–6 restantes (kerning, idioma, find por formato, estilos de página/lista, etiquetas, PDF tagged). Fase Humano só quando a IA estiver plena.

**Commits:** `ae64b4b` feat: add run styles, checkbox lists, and envelope template

---

## 2026-08-29 — Cursor — Fase IA wave 3: tabs, drop cap, nested, merge, PDF/A

**Objective:** Continuar waves 3–6 (JSON+MCP+`page_preview` apenas). Sem chrome QML. Sem push.

**Done:**
- Tabulações: `tabs[]` no bloco, `apply_paragraph_format`, `insert_tab`; preview com `QTextOption` tab stops.
- Letra capitular visual: `drop_cap_lines` ≥ 2 pinta a primeira letra no `page_preview`.
- Tabelas aninhadas (um nível): `cell.nested`, tools `nest_table` e `set_cell` `nested_row`/`nested_col`.
- Mala direta: `mail_merge {records}` substitui `{{chave}}` e campo `merge`; quebra de página entre cópias.
- Bordas de parágrafo e da página; `insert_line_break`.
- PDF/A-1b: `document_export_pdf {pdfa:true}` via `QPdfWriter::PdfVersion_A1b` (tagged PDF ainda não).
- Playbook: `AIOFFICE_HEADLESS=1 aioffice play qa/wave3-layout-merge.json`.

**Decisions:** Nested table é um nível (runs, sem recursão). `mail_merge` clona o corpo e reatribui IDs (undo restaura o modelo). PDF etiquetado fica Partial no Notion.

**Next suggested step:** Waves 3–6 restantes (fonte/tamanho/cor de run, realce, checkbox, estilos de carácter, envelopes, PDF tagged). Fase Humano só quando o critério IA estiver pleno no catálogo restante.

**Commits:** `8d41845` feat: add tabs, drop cap, nested tables, mail merge, and PDF/A

---

## 2026-08-29 — Cursor — Fase IA: DoD + fundação MCP + fatia Writer

**Objective:** Executar o programa Fase IA (depois Humano): Wave 0 auditoria, Wave 1 fundação MCP, Wave 2 round-trip A+B, fatia JSON+MCP+preview das waves 3–6. Sem chrome QML novo. Sem push.

**Done:**
- **DoD IA:** dado no `.aio.json` com IDs; tool MCP/`aioffice call` com `{ok:false,error}`; `page_preview` reflecte; `docs/mcp-tools.md` actualizado. ODT/DOCX não bloqueia a subfase (SoT = JSON).
- Wave 0: coluna Notion **Fase** (`IA`/`Humano`/`Infra`/`Fora`); board alinhado ao `Session::call`.
- Wave 1: `validate`, `batch` atómico, `id`/`ids[]`, `checkpoint`/`restore`, `aioffice play`, JSON canónico, `page_preview` `from`/`to`, `insert_markdown`, outline rico. Teste: `AIOFFICE_HEADLESS=1 aioffice play qa/wave1-foundation.json`.
- Wave 2: round-trip ODT/DOCX de notas, campos, TOC, bookmarks, revisões; notas no layout por página; quebra de coluna + separador; track `format` + accept/reject all; Hunspell `DICPATH` com `:` + dicts no `shell.nix`; Autocorrect JSON MCP.
- Waves 3–6 (fatia, não o catálogo inteiro): recuos/`keep_with_next`/papéis extra/gutter; `merge_cells`/`split_cells`/`table_sort`/`set_image`/`insert_caption`; export `md`/`txt`/`html`; campos `filename`/`word_count`/`custom`.
- Fase Humano **não** começou (UX Pilot + `Ui*` só depois da IA plena).

**Decisions:** `aioffice play` é sequencial (macros); atomicidade = tool `batch`. Checkpoint fora de `batch`. Commits locais por unidade; nunca `git push` neste programa até pedido.

**Next suggested step:** Continuar waves 3–6 no Notion (tabulações, drop cap visual, nested table, mala direta, PDF/A). Fase Humano só quando o critério IA estiver pleno no catálogo restante.

**Commits:** `cc141e4` feat: add MCP session foundation and Writer IA extras

---


## 2026-08-29 — Cursor — Backlog longo no Roadmap (o que um agente pode fazer)

**Objective:** Acrescentar no Notion tudo o que, mesmo demorando anos, um agente pode implementar no AI Office *do nosso jeito* (JSON+IDs, MCP/CLI, Tinta & Papel).

**Done:**
- +150 linhas Todo no Roadmap (formato, chrome, tabela, MCP, a11y, testes, etc.).
- Board: 22 Done, 16 Partial, **203 Todo** (241 no total).
- Hub callout + `docs/WRITER_FEATURE_CATALOG.md` actualizados.

**Decisions:** Se cabe no nosso modelo, entra. Continua **fora**: Calc, Impress, Draw-app, Base, StarBasic, Gallery/UI/ícones LO, WPS/FreeOffice, chat Copilot, Share. Gráficos/fórmulas = renderer nosso. CRDT local está no board como horizonte longo.

**Next suggested step:** Round-trip ODT/DOCX das ondas A+B; tools MCP de lote/IDs.

**Commits:** nenhum (usuário não pediu)

---

## 2026-08-29 — Cursor — Catálogo Writer Guide no Roadmap Notion

**Objective:** Encher o Roadmap para Produção (hub AI Office) com o catálogo de ideias do Writer Guide 26.2 — Done / Partial / Todo — sem copiar código/UI do LO e sem Calc/Impress/Base.

**Done:**
- Colunas `Fonte` e `Camada` no data source `collection://f3cc11ed-9e5e-4389-b49a-ca98f98dda62`.
- Callout do hub: sync 2026-08-29 + `docs/ATTRIBUTION.md` + este catálogo.
- Round-trip ODT/DOCX → **Partial** (notas/campos/TOC/revisões ainda não).
- 75 linhas novas (12 Done + 13 Partial + 50 Todo Writer/Formats). Board: 22 Done, 16 Partial, 53 Todo.
- Espelho: `docs/WRITER_FEATURE_CATALOG.md`.

**Decisions:** Ideias TDF só. Macros = MCP/CLI. Formas no documento de texto = cap. 11 no nosso canvas. Planilha/PPT continua Todo / não começar.

**Next suggested step:** Round-trip ODT/DOCX das ondas A+B; mesclar células / âncora de imagem. Não implementar features neste passo.

**Commits:** `aae080d` docs: catalog Writer Guide features for the Notion roadmap

---

## 2026-08-29 — Cursor — Writer ondas A+B (ideias LO, código nosso)

**Objective:** Implementar no Writer (JSON + MCP + GUI Tinta & Papel) as ideias de documento estruturado e edição de texto do LibreOffice Writer Guide, sem copiar código.

**Done:**
- `docs/ATTRIBUTION.md` (TDF / Writer Guide; WPS e FreeOffice fora).
- Runs: tachado, super/sub, campos, `note_id`.
- Bookmarks, revisões, `page.columns`, `hyphenate`, blocos footnote/endnote/toc.
- Tools: link_set, bookmark_add, insert_crossref/field/footnote/endnote, toc_update, spellcheck, track_changes, revision_*.
- Find `regex`; Hunspell opcional; Autocorrect no caret; hífen no layout.
- CommandBar + Sidebar + FindBar.

**Decisions:** Código nosso, GPL. LO só como referência de ideias. Ortografia só se Hunspell estiver no build.

**Next suggested step:** Empacotar Linux; Windows v1. Mesclar células / âncora de imagem depois.

**Commits:** `bd131dd` feat: add Writer Guide ideas in JSON, MCP, and GUI

---

## 2026-08-29 — Cursor — Relicense to GPL-3.0-or-later

**Objective:** Trocar MPL-2.0 por GPL para o software e os derivados continuarem livres, com o nome do autor citado.

**Done:**
- `LICENSE` = texto oficial GNU GPL v3 (29 June 2007).
- Copyright (C) 2026 Melker Halberd Pereira Alves no README, `docs/architecture.md` e `docs/DOCUMENTACAO_COMPLETA.md`.

**Decisions:** GPL-3.0-or-later (copyleft: quem reutilizar tem de manter livre). Attribution pelo aviso de copyright, que a GPL obriga a preservar. LO continua MPL, processo separado.

**Next suggested step:** Empacotar Linux (PATH/Nix); Windows v1. Planilha e PPT = apps separados.

**Commits:** `0582976` docs: relicense AI Office to GPL-3.0-or-later

---

## 2026-08-29 — Cursor — MPL-2.0 + initial git commit

**Objective:** Inicializar o repositório git, publicar no GitHub, e alinhar a licença com a do LibreOffice (MPL 2.0).

**Done:**
- `LICENSE` trocado de MIT para o texto oficial da Mozilla Public License 2.0.
- README, `docs/architecture.md` e `docs/DOCUMENTACAO_COMPLETA.md` atualizados.
- Removida instrumentação de debug (`agent log`) de `Session.cpp` e `Preview.cpp`.
- `.gitignore`: `.cursor/settings.json` e `.cursor/debug-*.log`.
- `git init` + commit inicial + push.

**Decisions:** Mesma licença do LibreOffice (MPL-2.0), não dual MPL/LGPL. LO continua processo separado em runtime.

**Next suggested step:** Empacotar Linux (PATH/Nix); Windows v1. Planilha e PPT = apps separados.

**Commits:** `97a4551` feat: initial AI Office Writer under MPL-2.0

---

## 2026-08-29 — Cursor — QA P1/P2 fixes from ResultadoDosTestes

**Objective:** Corrigir bugs P1/P2 e features v0.2 listadas em `qa/ResultadoDosTestes.md`.

**Done:**
- `insert_table`: `header_texts` na linha 0; `cells` a partir da linha 1; `header` no JSON; `col_widths_mm`.
- Markdown: `**bold**`, `*italic*`, `[text](url)`, listas aninhadas por indentação.
- Templates: alias `name`, `force` se dirty, título humano, limpa `path`, `fields`/`{{placeholders}}`.
- `insert_at_caret` substitui o range selecionado.
- `document_diff` vs último save/open/new/template (`added`/`moved`).
- Paint por run (negrito só no trecho); comentário = círculo cobre na margem.
- Mini-barra: botão H liga header; excluir tabela é `⊟`; combo de estilos usa `styleLabels`.
- ODT: `svg:width` → `width_mm`; não cria parágrafo vazio em volta da imagem.
- DOCX: twips 1440/25.4; quebra de página sem parágrafo fantasma.

**Decisions:** Diff contra baseline de arquivo (não último undo). Merge de células fica para depois. Planilha/PPT fora.

**Next suggested step:** Reiniciar a GUI no eDP-1 e conferir comentário no papel + botão H. Depois empacotar PATH/Nix.

**Commits:** `97a4551` feat: initial AI Office Writer under MPL-2.0

---

## 2026-08-28 — Cursor — Writer v0.2 QA + Tinta & Papel

**Objective:** Corrigir QA P0–P2 e completar o Writer (layout, ODT/DOCX, canvas, tools, GUI a partir dos frames UX Pilot).

**Done:**
- Hub Notion + Roadmap para Produção + catálogo MCP (irmão do Rose Faxina).
- 16 frames novos na página `bOAKXwabCJcNcRA1DigB` (job `wIOu49yS2pyteKKuI9cH`); mapa em `docs/design/html/`.
- Layout único (fonte×zoom, listas 1.2.3, tabela wrap, imagem raster, header/footer, hit-test, PDF nativo).
- IPC lê JSON até newline (PNG em base64).
- ODT/DOCX: table/list/image/break/page-layout, T-styles únicos, numbering.xml, Pictures/media.
- Tools: `after_id` falha, format por range, Title, tabela, search, selection, comments, header/footer, markdown, templates, diff, PDF nativo.
- Canvas digitável (caret cobre, seleção, IME, células) ligado à Session.
- QML: nav de página, FindBar next/prev, styleNames, TableMiniBar real, tabs, UnsavedSheet, dica LO, chip PDF nativo.
- Wrapper `scripts/aioffice`.

**Decisions:** Design HTML manda; sem Compartilhar; ODT/DOCX nativos (o frame do aviso LO falava o contrário — produto vence). PDF nativo no caminho feliz. Tabs por snapshot JSON, uma Session viva.

**Next suggested step:** Empacotar Linux (PATH/Nix); Windows v1. Planilha e PPT = apps separados da suite, nunca neste Writer.

**Commits:** nenhum (usuário não pediu)

---

## 2026-08-28 — Cursor — Continuity + Writer v0.2 in progress

**Objective:** Instalar o método de agentes (handoff, regras, wiki) e completar o Writer segundo o QA em `qa/ResultadoDosTestes.md`.

**Done:**
- Regras Cursor (`ai-continuity`, `ai-commit-protocol`, `project-context`, `commit-language-english`) + `design-overrides-qt` já existente.
- Stubs de `DOCUMENTACAO_COMPLETA.md`, `DESIGN_CHANGES.md`, `QA_REPORT.md`, `docs/design/`.

**Decisions:** Git = o que mudou; handoff = por quê; Notion = wiki + Roadmap para Produção. Design Tinta & Papel manda sobre Qt. Planilha e PPT são apps separados (suite), não features deste Writer.

**Next suggested step:** Hub Notion + gerar frames UX Pilot que faltam; depois layout, ODT/DOCX, canvas.

**Commits:** (esta sessão)
