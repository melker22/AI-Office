# Semantic document + MCP/CLI contract

All tools are available as MCP `tools/call` and as `aioffice call <name> '<json args>'`. Playbooks: `aioffice play script.json` (JSON array of `{tool, args}`, runs `batch`).

The document is a JSON object with **stable block IDs**. Agents must address content by `id`, never by pixel or mouse.

## Document JSON (`schema`: 1)

```json
{
  "schema": 1,
  "meta": {
    "title": "Untitled",
    "page": {
      "size": "A4",
      "orientation": "portrait",
      "margins_mm": [25, 20, 25, 20]
    }
  },
  "styles": [
    {
      "name": "Heading 1",
      "font": "Liberation Serif",
      "size_pt": 22,
      "bold": true,
      "italic": false,
      "color": "#1A1F24",
      "align": "left",
      "space_before_pt": 16,
      "space_after_pt": 8
    }
  ],
  "body": [
    {
      "id": "h1",
      "type": "heading",
      "level": 1,
      "style": "Heading 1",
      "align": "left",
      "runs": [{ "text": "Title", "bold": false }]
    },
    {
      "id": "p1",
      "type": "paragraph",
      "style": "Normal",
      "align": "justify",
      "runs": [
        { "text": "Hello ", "bold": false },
        { "text": "world", "bold": true, "italic": false, "underline": false }
      ]
    }
  ]
}
```

### Block types

| `type` | Extra fields |
| --- | --- |
| `paragraph` | `style`, `align`, `runs` |
| `heading` | `level` 1–6, `style`, `align`, `runs` |
| `list-item` | `level`, `list_kind` (`bullet` \| `decimal`), `runs` |
| `table` | `rows`: array of rows; each cell `{ "runs": [...] }` |
| `image` | `path`, `image_b64`, `width_mm` (bytes are the source of truth) |
| `break` | page break (`break_kind`: `page`) |
| `footnote` / `endnote` | nota; âncora no run via `note_id` |
| `toc` | sumário; `toc_targets` = ids dos headings |

### Meta extras

`header` / `footer`: `{ "text", "page_numbers", "chapter"?, "image"? }`. `meta.read_only`, `meta.watermark`, `meta.vars`, `meta.subject`, `meta.keywords`. Block `comments[]`: `{ "id", "author", "text", "resolved" }`. Page `column_widths` (relative weights). Cell `formula` (`sum:0` | `avg:1` | `count:0`). Image `rotate`, `opacity`.

### Text run fields

`text`, `bold`, `italic`, `underline`, `strike`, `superscript`, `subscript`, `font` (optional), `size_pt` (0 = inherit), `color`, `highlight`, `link`, `field` (`page_number` | `page_count` | `date` | `title` | `author` | `xref` | `filename` | `word_count` | `custom` | `merge` | `chapter` | `var` | `input`), `field_arg`, `note_id`.

### ID prefixes

Assigned by the editor: `h` headings, `p` paragraphs, `l` list items, `t` tables, `img` images, `br` breaks. Never invent colliding IDs; omit `id` on insert to let the editor assign one.

## Tools

Every tool returns JSON: `{ "ok": true, ... }` or `{ "ok": false, "error": "..." }`.

### `document_new`

Args: `{ "title"?: string, "force"?: bool }`  
Clears the session into a blank document with one empty `p1`. If the document is dirty, pass `force: true` or you get `{ok:false,error}`.

### `document_open`

Args: `{ "path": string }`  
Opens ODT, DOCX, DOC (DOC needs LibreOffice), AIO JSON, RTF (via LibreOffice when present).

### `document_save`

Args: `{ "path"?: string }`  
Saves to `path` or the last opened/saved path. Format from extension.

### `document_export_pdf`

Args: `{ "path": string, "pdfa"?: bool }`  
Native `QPdfWriter` (no LibreOffice). `pdfa=true` requests PDF/A-1b. Tagged PDF is not produced. Falls back to soffice only if native write fails.

### `document_get`

Args: `{}`  
Returns `{ "ok", "document": { ...full JSON... } }`.

### `document_get_outline`

Args: `{}`  
Returns `{ "ok", "outline": [ { "id", "kind", "type", "level"?, "text" } ] }`. Headings plus tables, images, footnotes, endnotes, toc.

### `insert_blocks`

Args:

```json
{
  "after_id": "p1",
  "blocks": [
    { "type": "paragraph", "align": "justify", "runs": [{ "text": "..." }] }
  ]
}
```

- `after_id`: insert after this block. Use `""` or `"start"` for the beginning, `"end"` for the end. **Unknown id → `{ok:false,error}`** (does not append).

### `replace_range`

Args: `{ "ids": ["p1", "p2"], "blocks": [ ... ] }`  
Replace those blocks (contiguous recommended) with `blocks`.

### `delete_range`

Args: `{ "ids": ["p1"] }`

### `apply_character_format`

Args: `{ "ids": ["p1"], "start"?: int, "end"?: int, "bold"?: bool, "italic"?: bool, "underline"?: bool, "strike"?: bool, "superscript"?: bool, "subscript"?: bool, "font"?: string, "size_pt"?: number, "color"?: string, "highlight"?: string, "link"?: string, "small_caps"?: bool, "caps"?: "upper"|"lower"|"title", "hidden"?: bool, "char_style"?: string, "lang"?: string, "kerning"?: number }`  
With `start`/`end`, applies to that range of the block’s plain text. Otherwise all runs. `highlight` is painted in `page_preview`. `hidden` omits the run from preview. `char_style` names a `create_style` with `kind=character`. `kerning` is extra letter-spacing percent (preview + ligatures). `lang` is stored on the run (BCP 47).

### `apply_paragraph_format`

Args: `{ "ids": ["p1"], "align"?: "left"|"center"|"right"|"justify", "style"?: string, "indent_left_mm"?: number, "indent_right_mm"?: number, "indent_first_mm"?: number, "space_before_pt"?: number, "space_after_pt"?: number, "line_height"?: number, "keep_with_next"?: bool, "break_before"?: bool, "orphans"?: int, "widows"?: int, "drop_cap_lines"?: int, "tabs"?: [{"pos_mm": number, "align"?: "left"|"right"|"center"|"decimal"}], "border_pt"?: number, "border_color"?: string, "list_restart"?: bool, "list_bullet"?: string, "lang"?: string }`  
`drop_cap_lines` ≥ 2 paints a visual drop cap in `page_preview`. Tab stops pair with `insert_tab` / `\t` in run text. `list_restart` resets decimal numbering; `list_bullet` is the custom marker. `keep_with_next` holds the next block on the same page; `break_before` starts a new page.

### `apply_style`

Args: `{ "ids": ["p1"], "style": "Heading 1" }`  
`Heading N` sets `type=heading` and `level`. `Title` becomes a heading (level 1) with style Title. A `kind=list` style also sets `list_kind` / `list_bullet`.

### `create_style`

Args: `{ "name": string, "font"?: string, "size_pt"?: number, "bold"?: bool, "italic"?: bool, "color"?: string, "align"?: string, "space_before_pt"?: number, "space_after_pt"?: number, "kind"?: "paragraph"|"character"|"list"|"page", "next"?: string, "list_kind"?: string, "list_bullet"?: string, "page_size"?: string, "page_orientation"?: string }`  
`next` is the style applied to the new block after `split_block`.

### `update_style`

Args: `{ "name"?: string, "id"?: string, "offset"?: int }`  
Copies font/size/bold/italic/color (and list attrs) from the caret run into the named style.

### `set_page_layout`

Args: `{ "size"?: "A4"|"Letter"|"Legal"|"A3"|"A5"|"A6"|"Tabloid"|"DL"|"C5"|"C6"|"B4"|"B5"|"Executive"|"Folio"|"Statement", "orientation"?: "portrait"|"landscape", "margins_mm"?: [t, r, b, l], "columns"?: 1|2|3, "column_widths"?: [2, 1], "hyphenate"?: bool, "gutter_mm"?: number, "column_gap_mm"?: number, "column_separator"?: bool, "border_pt"?: number, "border_color"?: string, "background"?: string, "style"?: string, "number_format"?: "arabic"|"roman"|"alpha", "number_start"?: int }`  
`style` copies `page_size` / `page_orientation` from a `kind=page` style. `number_format` / `number_start` change the printed page label; `insert_break {page_restart:true, page_number?:n}` restarts the counter. `column_widths` are relative weights when `columns` > 1.

### `insert_table`

Args: `{ "after_id": "end", "rows": 4, "cols": 3, "header": true, "header_texts"?: ["A","B"], "cells"?: ["..."], "col_widths_mm"?: [40, 40, 40] }`

If `header` is true, `header_texts` fill **row 0**. `cells` fill from **row 1**. Row 0 is painted as a header band.

### `insert_image`

Args: `{ "after_id": "end", "path": string, "width_mm"?: number }`  
Reads the file into `image_b64`.

### `insert_list`

Args: `{ "after_id": "end", "kind": "bullet"|"decimal"|"number"|"checkbox", "items": ["a", "b"], "bullet"?: string, "restart"?: bool }`  
`checkbox` items may be `{ "text": "…", "checked": true }`. `toggle_check { id }` flips the box. `bullet` is a custom marker; `restart` sets `list_restart` on the first item.

### `insert_break`

Args: `{ "after_id": "end", "kind"?: "page"|"column"|"page_restart", "page_restart"?: bool, "page_number"?: int }`

### `insert_line_break` / `insert_tab`

Insert `\n` or `\t` at the caret (`id`/`offset` optional). Does not split the block. Tab stops come from `apply_paragraph_format.tabs`.

### `find_replace`

Args: `{ "find": string, "replace": string, "all"?: false, "regex"?: false }`  
`all` defaults to **false** (first match). `regex` uses `QRegularExpression`. Returns `{ "ok", "count" }`.

### `search`

Args: `{ "find"?: string, "forward"?: true, "regex"?: false, "bold"?: bool, "italic"?: bool, "font"?: string, "highlight"?: string, "lang"?: string, "char_style"?: string, "size_pt"?: number, "kerning"?: number }`  
Find without replacing. `find` may be omitted when a format filter is present. Returns `{ hits: [{id, start, end}] }`.

### `goto`

Args: `{ "id"?: string, "page"?: int }`  
Moves the caret. Returns `{ id, page, display_page }`.

### `select_by_style`

Args: `{ "style": "Heading 1" }`  
Returns `{ ids: [...] }` and selects the first match.

### `split_block`

Args: `{ "id"?: string, "offset"?: int }`  
Splits at the caret if `id` is omitted. The new block takes `create_style.next` when set.

### `insert_labels`

Args: `{ "records": ["A", "B"] | [{"text":"A"}], "cols"?: 2, "rows"?: int, "after_id"?: "end" }`  
Builds a table (no header) filled left-to-right, top-to-bottom.

### `set_cell` / `insert_row` / `insert_col` / `delete_row` / `delete_col`

Table surgery on a table `id` (the selected table in the GUI). `set_cell`: `{id, row, col, text|runs, nested_row?, nested_col?, border_pt?, border_color?, background?, v_align?: top|middle|bottom, padding_mm?, formula?: "sum:0"|"avg:1"|"count:0"}`. `nested_*` writes into a nested table created by `nest_table`. Omit `text`/`runs` to change only cell format. `table_formulas {id?}` evaluates formulas (skips other formula cells) and writes the result into the cell text. `table_auto_width {id}` sets `col_widths_mm` from text length. `create_style {kind:table, color}` + `apply_style` fills the header row. Tables taller than a page split across pages and **repeat row 0** when `header` is set.

### `selection_get` / `set_selection` / `insert_at_caret`

Shared caret: `{anchor_id, anchor_off, focus_id, focus_off, row?, col?}`. `insert_at_caret` types at the caret. If the selection is a range (offsets differ on the same block), the range is **replaced**.

### `move_blocks` / `duplicate_blocks`

Args: `{ "ids": [...], "after_id": "end" }`

### `comment_add` / `comment_resolve`

Args: `{ "id": "p1", "text": "...", "author"?: "IA" }` / `{ "comment_id": "..." }`

### `document_set_title` / `set_header` / `set_footer`

Title/author/subject/keywords/watermark; header/footer `{text, page_numbers, chapter?: bool, image?: path, even?: bool, first?: bool}`. `chapter=true` appends the last Heading 1 on or before the page (also `{chapter}` in `text`). `set_vars {vars:{k:v}}` or `{name,value}`. `insert_field {kind:var, arg}` reads vars. `set_read_only {on}` blocks mutations except reads, export, `document_new`/`open`, and itself.

### `insert_html` / `insert_plain`

`insert_html {html, after_id?}` parses a sanitized HTML fragment (`QTextDocument`: p, h1–h3, lists, bold/italic/underline, links). Scripts and unknown tags are dropped.  
`insert_plain {text, after_id?}` inserts one paragraph per line with no character format.

### `insert_frame`

Args: `{ "text"?: string, "width_mm"?: 80, "border_pt"?: 0.75, "border_color"?: "#B5651D", "style"?: string, "after_id"?: "end" }`  
Block `type=frame` (id `fr*`). `create_style kind=frame` + `apply_style` also converts a block into a frame.

### `autoformat`

Args: `{ "lang"?: "pt-PT"|"en" }`  
`--` → emdash, `...` → ellipsis, `1/2` `1/4` `3/4` → fractions, straight quotes → «» (pt) or “” (en). Bare `http(s)://` runs become links.

### `join_blocks` / `load_styles`

`join_blocks {id}` merges with the previous block. `load_styles {path}` copies named styles from another `.aio.json`.

### `promote_heading` / `demote_heading`

Args: `{ "id"?: string, "ids"?: [...], "delta"?: -1 }`  
`promote_heading` turns a paragraph into Heading 1, or H2→H1 (delta default **-1**). `demote_heading` uses delta **+1** (H1→H2; demote past 1 becomes Normal). Caret if `id` omitted.

### `wrap_list` / `unwrap_list`

`wrap_list {ids, kind?: "bullet"|"decimal"|"checkbox"}` converts paragraphs into `list-item`. `unwrap_list` restores Normal paragraphs.

### `text_to_table` / `table_to_text` / `split_table`

`text_to_table {ids, separator?: "\\t"|"," , header?: false}` — each line is a row.  
`table_to_text {id, separator?}` — one paragraph per row.  
`split_table {id, row}` — second table starts at `row` (not 0); header row is copied when `headerRow` is set.

### `export_markdown` / `export_text` / `copy_markdown`

`export_markdown {path?, ids?}` returns `{markdown}` and optionally writes UTF-8. Whole document if `ids` omitted.  
`export_text` same with `{text}` (tables as tab-separated rows).  
`export_html` same with `{html}` (simple semantic HTML).  
`copy_markdown` is the selection (or `ids`) without a file.

### `document_diff`

Ids `added` / `removed` / `changed` / `moved` vs the document at last **save**, **open**, **new**, or **template** (not vs the last undo frame).

### `import_markdown` / `document_from_template`

Markdown → blocks. Inline: `**bold**`, `*italic*`, `[text](url)`. Nested lists by indent. Templates: `carta` | `ata` | `contrato` | `envelope` (`template` or `name`). Envelope uses paper **DL** landscape. If the session is dirty, pass `force=true`. Optional `fields` replaces `{{placeholders}}` (cidade, data, nome, …). Title is human (`Carta`, not `carta`). Path is cleared.

### `link_set` / `bookmark_add` / `insert_crossref`

Hiperlink na seleção (`url`). Marcador nomeado no bloco (`name`, `id` opcional). Referência cruzada (`target` = id de bloco ou bookmark) como campo `xref`.

### `insert_field`

Args: `{ "kind": "page_number"|"page_count"|"date"|"title"|"author"|"xref"|"filename"|"word_count"|"custom"|"merge", "arg"?: string }`  
Insere no caret. `xref`/`custom`/`merge` usam `arg`. Campo `merge` mostra `«arg»` até `mail_merge`. `date` usa `arg` como formato Qt (`dd/MM/yyyy` por omissão; `yyyy-MM-dd hh:mm` para data/hora).

### `insert_footnote` / `insert_endnote`

Args: `{ "text"?: string }`  
Marca sobrescrita no caret + bloco `footnote`/`endnote` com id `fn*`/`en*`.

### `toc_update`

Cria ou regenera o bloco `toc` a partir dos headings. Args: `{ min_level?: 1, max_level?: 6, styles?: ["Title"] }`. `toc_targets` guarda os ids.

### `convert_note`

Args: `{ "id": "fn1" }`  
Alterna o bloco `footnote` ↔ `endnote`.

### `insert_index`

Args: `{ "terms": ["Alfa", "Beta"], "title"?: "Índice remissivo", "after_id"?: "end" }`  
Insere um Heading 1 e um parágrafo por termo com números de página (`concordance` é alias).

### `spellcheck`

Hunspell se o binário foi compilado com a lib e existir dicionário (`DICPATH` ou `/usr/share/hunspell`). `{ available, hits: [{id,start,end,word,suggestions}] }`.

### `track_changes` / `revision_list` / `revision_accept` / `revision_reject`

`track_changes { on }` grava insert/delete/format. `revision_accept_all` / `revision_reject_all`. Aceitar remove a marca; rejeitar um `insert` desfaz o texto.

### `autocorrect_get` / `autocorrect_set`

Mapa `{from: to}` no JSON do documento (sem GUI). O caret aplica a lista + defaults (`teh`→`the`, …).

### `merge_cells` / `split_cells` / `table_sort` / `nest_table`

`merge_cells {id, row, col, col_span?, row_span?}`. `table_sort {id, col}` ordena linhas de dados.  
`nest_table {id, row, col, rows?, cols?}` cria uma tabela de um nível dentro da célula (`cell.nested` no JSON).

### `mail_merge`

Args: `{ "records": [ {"nome": "Ana"}, {"nome": "Bia"} ] }`  
Clona o corpo por registo, substitui `{{chave}}` e campos `merge`, e insere quebra de página entre cópias. IDs novos. Undo restaura o modelo.

### `clear_format` / `paint_format` / `insert_special` / `toggle_check`

`clear_format {ids, start?, end?}` tira formato de carácter (mantém texto).  
`paint_format {from_id, ids, start?, end?}` copia o formato do run em `from_id`.  
`insert_special {kind: nbsp|shy|emdash|endash|ellipsis|copyright|euro}` no caret.  
`toggle_check {id, checked?}` em itens `checkbox`.

### `set_image` / `insert_caption`

Alt, caption, wrap, âncora, `width_mm`, `rotate` (graus), `opacity` (0–1) no bloco imagem (ou caption em tabela).

### `undo` / `redo`

Args: `{}`

### `page_preview`

Args: `{ "page"?: 1, "zoom"?: 1.15 }` or `{ "page": "all", "zoom"?: 1.15 }` or `{ "from": 1, "to": 3, "zoom"?: 1.15 }`  
Default zoom **1.15**. `from`/`to` are inclusive page numbers. `page:"all"` returns `{previews:[{page,png_base64}]}`.

### `validate`

Args: `{}`  
Returns `{ ok, valid, errors: [{code,id?,message}], blocks }`. Checks unique ids, known block types, `note_id`, xref targets, bookmarks.

### `batch`

Args: `{ "calls": [ { "tool": "apply_style", "args": { ... } }, ... ] }`  
Runs every call in order. First `{ok:false}` **rolls back** the whole batch (no nested `batch`/`undo`/`checkpoint`). One undo frame on success.

### `checkpoint` / `restore`

Args: `{}`  
Snapshot / restore the session. Distinct from caret `undo`. `restore` fails with `{ok:false,error:"no checkpoint"}`.

### `insert_markdown`

Alias of `import_markdown`. Args: `{ "markdown": string, "after_id"?: "end" }`.

### `integrate_help`

Args: `{}`  
Returns the human/AI integration blurb plus `title`, `words`, `outline`. No secrets.

## MCP server

- Name: `ai-office`
- Stdio: `aioffice mcp`
- Protocol: JSON-RPC 2.0, MCP `2024-11-05` (`initialize`, `notifications/initialized`, `tools/list`, `tools/call`)
- Transport: `Content-Length` frames **or** newline-delimited JSON

## Live vs headless

If the GUI is running, `document_open` / edits target the **live** document. Headless MCP keeps its own session until `document_open` / `document_save`.
