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

`header` / `footer`: `{ "text", "page_numbers" }`. Block `comments[]`: `{ "id", "author", "text", "resolved" }`.

### Text run fields

`text`, `bold`, `italic`, `underline`, `strike`, `superscript`, `subscript`, `font` (optional), `size_pt` (0 = inherit), `color`, `highlight`, `link`, `field` (`page_number` | `page_count` | `date` | `title` | `author` | `xref`), `field_arg`, `note_id`.

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

Args: `{ "ids": ["p1"], "start"?: int, "end"?: int, "bold"?: bool, "italic"?: bool, "underline"?: bool, "strike"?: bool, "superscript"?: bool, "subscript"?: bool, "font"?: string, "size_pt"?: number, "color"?: string, "highlight"?: string, "link"?: string }`  
With `start`/`end`, applies to that range of the block’s plain text. Otherwise all runs.

### `apply_paragraph_format`

Args: `{ "ids": ["p1"], "align"?: "left"|"center"|"right"|"justify", "style"?: string, "indent_left_mm"?: number, "indent_right_mm"?: number, "indent_first_mm"?: number, "space_before_pt"?: number, "space_after_pt"?: number, "line_height"?: number, "keep_with_next"?: bool, "orphans"?: int, "widows"?: int, "drop_cap_lines"?: int, "tabs"?: [{"pos_mm": number, "align"?: "left"|"right"|"center"|"decimal"}], "border_pt"?: number, "border_color"?: string }`  
`drop_cap_lines` ≥ 2 paints a visual drop cap in `page_preview`. Tab stops pair with `insert_tab` / `\t` in run text.

### `apply_style`

Args: `{ "ids": ["p1"], "style": "Heading 1" }`  
`Heading N` sets `type=heading` and `level`. `Title` becomes a heading (level 1) with style Title.

### `create_style`

Args: `{ "name": string, "font"?: string, "size_pt"?: number, "bold"?: bool, "italic"?: bool, "color"?: string, "align"?: string, "space_before_pt"?: number, "space_after_pt"?: number }`

### `set_page_layout`

Args: `{ "size"?: "A4"|"Letter"|"Legal"|"A3"|"A5"|"A6"|"Tabloid", "orientation"?: "portrait"|"landscape", "margins_mm"?: [t, r, b, l], "columns"?: 1|2|3, "hyphenate"?: bool, "gutter_mm"?: number, "column_gap_mm"?: number, "column_separator"?: bool, "border_pt"?: number, "border_color"?: string }`

### `insert_table`

Args: `{ "after_id": "end", "rows": 4, "cols": 3, "header": true, "header_texts"?: ["A","B"], "cells"?: ["..."], "col_widths_mm"?: [40, 40, 40] }`

If `header` is true, `header_texts` fill **row 0**. `cells` fill from **row 1**. Row 0 is painted as a header band.

### `insert_image`

Args: `{ "after_id": "end", "path": string, "width_mm"?: number }`  
Reads the file into `image_b64`.

### `insert_list`

Args: `{ "after_id": "end", "kind": "bullet"|"decimal"|"number", "items": ["a", "b"] }`

### `insert_break`

Args: `{ "after_id": "end", "kind"?: "page"|"column" }`

### `insert_line_break` / `insert_tab`

Insert `\n` or `\t` at the caret (`id`/`offset` optional). Does not split the block. Tab stops come from `apply_paragraph_format.tabs`.

### `find_replace`

Args: `{ "find": string, "replace": string, "all"?: false, "regex"?: false }`  
`all` defaults to **false** (first match). `regex` uses `QRegularExpression`. Returns `{ "ok", "count" }`.

### `search`

Args: `{ "find": string, "forward"?: true, "regex"?: false }`  
Find without replacing. Returns `{ hits: [{id, start, end}] }`.

### `set_cell` / `insert_row` / `insert_col` / `delete_row` / `delete_col`

Table surgery on a table `id` (the selected table in the GUI). `set_cell`: `{id, row, col, text|runs, nested_row?, nested_col?}`. `nested_*` writes into a nested table created by `nest_table`.

### `selection_get` / `set_selection` / `insert_at_caret`

Shared caret: `{anchor_id, anchor_off, focus_id, focus_off, row?, col?}`. `insert_at_caret` types at the caret. If the selection is a range (offsets differ on the same block), the range is **replaced**.

### `move_blocks` / `duplicate_blocks`

Args: `{ "ids": [...], "after_id": "end" }`

### `comment_add` / `comment_resolve`

Args: `{ "id": "p1", "text": "...", "author"?: "IA" }` / `{ "comment_id": "..." }`

### `document_set_title` / `set_header` / `set_footer`

Title/author; header/footer `{text, page_numbers}`.

### `document_diff`

Ids `added` / `removed` / `changed` / `moved` vs the document at last **save**, **open**, **new**, or **template** (not vs the last undo frame).

### `import_markdown` / `document_from_template`

Markdown → blocks. Inline: `**bold**`, `*italic*`, `[text](url)`. Nested lists by indent. Templates: `carta` | `ata` | `contrato` (`template` or `name`). If the session is dirty, pass `force=true`. Optional `fields` replaces `{{placeholders}}` (cidade, data, nome, …). Title is human (`Carta`, not `carta`). Path is cleared.

### `link_set` / `bookmark_add` / `insert_crossref`

Hiperlink na seleção (`url`). Marcador nomeado no bloco (`name`, `id` opcional). Referência cruzada (`target` = id de bloco ou bookmark) como campo `xref`.

### `insert_field`

Args: `{ "kind": "page_number"|"page_count"|"date"|"title"|"author"|"xref"|"filename"|"word_count"|"custom"|"merge", "arg"?: string }`  
Insere no caret. `xref`/`custom`/`merge` usam `arg`. Campo `merge` mostra `«arg»` até `mail_merge`.

### `insert_footnote` / `insert_endnote`

Args: `{ "text"?: string }`  
Marca sobrescrita no caret + bloco `footnote`/`endnote` com id `fn*`/`en*`.

### `toc_update`

Cria ou regenera o bloco `toc` a partir dos headings. `toc_targets` guarda os ids.

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

### `set_image` / `insert_caption`

Alt, caption, wrap, âncora, `width_mm` no bloco imagem (ou caption em tabela).

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
