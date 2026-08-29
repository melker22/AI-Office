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

**Commits:** nenhum (usuário não pediu)

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
