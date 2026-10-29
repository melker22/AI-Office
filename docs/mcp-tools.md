# Semantic document + MCP/CLI contract

All tools are available as MCP `tools/call` and as `aioffice call <name> '<json args>'`. Playbooks: `aioffice play script.json` (JSON array of `{tool, args}`). Play runs each step in order and stops at the first failing expect. It keeps the earlier steps; rolling several edits back together is the `batch` tool. A call entry may add `"expect": {"dot.path": value, ...}` to assert a field of that call's own JSON result (dot-path, numeric segments index arrays, e.g. `document.body.0.runs.1.bold`); a mismatch fails the playbook the same way `ok:false` does, printing what was expected vs. what came back. For numeric fields, `expect` also accepts a one-key object operator: `{"$gt"|"$lt"|"$gte"|"$lte"|"$ne": number}` (compared to the actual number at that path).

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
| `paragraph` | `style`, `align`, `dir` (`auto`\|`ltr`\|`rtl`, default `auto`, omitted when `auto`), `runs` |
| `heading` | `level` 1–6, `style`, `align`, `dir`, `runs` |
| `list-item` | `level`, `list_kind` (`bullet` \| `decimal` \| `lowerLetter` \| `upperLetter` \| `lowerRoman` \| `upperRoman` \| `checkbox` \| `task`; from ODT import also maps the first six), `checked` (true only, omitted when false), `runs` |
| `table` | `rows`: array of rows (each cell `{ "runs", "col_span"?, "row_span"?, "nested"? }`), or integers `rows` (1–200) and `cols` (1–64) with optional `header`, `header_texts`, `cells` — a non-integer or out-of-range pair is `{ok:false,error}` |
| `image` | `path`, `image_b64`, `width_mm`, `rotate`, `opacity`, `flip_h`/`flip_v`, `crop` [L,T,R,B], `shadow_pt`, `z_index` |
| `break` | `break_kind`: `page` \| `column` \| `section`; `hidden` esconde a secção seguinte |
| `footnote` / `endnote` | nota; âncora no run via `note_id`; endnotes no fim da secção. No ODT, `text:note-citation` é só a marca e não entra no corpo; importa-se o texto de `text:note-body` |
| `toc` | sumário; `toc_targets` = ids dos headings |
| `shape` | `shape` rect\|ellipse\|line, `fill`, `stroke`, `width_mm`, `height_mm`, `connect_to` |
| `chart` | `data`: `[{label,value}]` |
| `qr` | `qr` texto (preview byte-mode curto) |
| `equation` | texto + `numbered` |
| `form` | `form` text\|checkbox\|dropdown |

### Meta extras

`header` / `footer`: `{ "text", "page_numbers", "chapter"?, "image"? }`. `meta.read_only`, `meta.watermark`, `meta.vars`, `meta.subject`, `meta.keywords`, `meta.bibliography`, `meta.autotext`, `meta.thesaurus`, `meta.user_dic`. Block `comments[]`: `{ "id", "author", "text", "resolved", "parent_id"? }`. Page `column_widths`, `width_mm`/`height_mm`, `tone`, `grid_mm`, `line_numbers`, `chapter_numbers`, `booklet`, `gutter_mirror`, `hyphen_zone_mm`. Cell `formula` (`sum:0` | `avg:1` | `count:0`). Image `rotate`, `opacity`, `flip_h`, `crop`.

### Text run fields

`text`, `bold`, `italic`, `underline`, `strike`, `superscript`, `subscript`, `font` (optional), `size_pt` (0 = inherit), `color`, `highlight`, `link`, `field` (`page_number` | `page_count` | `date` | `time` | `title` | `author` | `xref` | `filename` | `word_count` | `custom` | `merge` | `chapter` | `var` | `input` | `cite` | `seq`), `field_arg`, `note_id`, `ruby`, `redact`, `show_if`. `seq` stores `field_arg` as compact JSON `{"format":"arabic|roman|alpha","name":"…"}`.

### ID prefixes

Assigned by the editor: `h` headings, `p` paragraphs, `l` list items, `t` tables, `img` images, `br` breaks, `sh` shapes, `ch` charts, `qr` QR, `eq` equations, `fm` forms. Never invent colliding IDs; omit `id` on insert to let the editor assign one.

## Tools

Every tool returns JSON: `{ "ok": true, ... }` or `{ "ok": false, "error": "..." }`.

### `document_new`

Args: `{ "title"?: string, "force"?: bool }`  
Clears the session into a blank document with one empty `p1`. If the document is dirty, pass `force: true` or you get `{ok:false,error}`.

### `document_open`

Args: `{ "path": string }`  
Opens ODT, DOCX, DOC (DOC needs LibreOffice), AIO JSON, or RTF (native reader). A `body` item that is not an object is `{ok:false,error:"body item is not an object"}`. An explicit duplicate block id is `{ok:false,error:"duplicate block id: …"}`. Ids that are missing are assigned only after the whole body is loaded, and they do not reuse an explicit id. Malformed DOCX/ODT XML fails the open (`document.xml: … at line N`, `content.xml: … at line N`) instead of returning a partial document. Each zip entry may produce at most 256 MiB uncompressed and the archive at most 1 GiB, counted from the bytes inflation actually writes. A central-directory size of 0 is not treated as empty: that entry still counts its real output, and several such entries share the 1 GiB total. A declared size above either limit fails the same way. Above either limit the error is `archive entry too large`. An ODT `text:s` whose `text:c` is above 10000 fails with `text:s text:c exceeds 10000`. ODT import stores `width_mm` and `height_mm` from the page layout (`mm`, `cm`, `in`, or `pt`). The size name is `A4`, `Letter`, `Legal`, `A3`, or `A5` only when both sides match that size within 1 mm; otherwise the name is `custom`. Orientation comes from the measures (wider than tall is landscape). A run font is `style:font-name`, or `fo:font-family` when the font name is absent. List kind comes from the applied `text:list-style`: `list-level-style-bullet` is `bullet`, and `style:num-format` `1`, `a`, `A`, `i`, `I` is `decimal`, `lowerLetter`, `upperLetter`, `lowerRoman`, `upperRoman`. The level is the nesting depth. A nested `table:table` is `cell.nested`. `table:number-columns-spanned` and `table:number-rows-spanned` are `col_span` and `row_span`; `table:covered-table-cell` fills the covered slots. `text:bookmark` and `text:bookmark-start` are read inside the paragraph or heading at that character offset. A drawing without an `aioffice:extra` figure blob stays an image (`svg:title` / `svg:desc` become `alt`), including a shape that has no image bytes, so it does not become an empty paragraph.

Native RTF import maps `\lang` through the shared table in `RtfLang` (`rtfLcidForTag` / `rtfTagForLcid`): pt-BR 1046, pt-PT 2070, en-US 1033, en-GB 2057, es-ES 3082, es-MX 2058, fr-FR 1036, de-DE 1031, it-IT 1040, ar 1025, he 1037, ru 1049, zh-CN 2052, zh-TW 1028, ja 1041, ko 1042. A tag with no region uses the principal variant (`pt` is 1046, `en` is 1033, `es` is 3082, `fr` is 1036, `de` is 1031, `it` is 1040, `zh` is 2052). LCID 1024 is ignored. LCIDs outside the shared table that the reader already accepted stay on their tags: 1026 bg-BG, 1027 ca-ES, 1029 cs-CZ, 1030 da-DK, 1032 el-GR, 1034 es-ES, 1035 fi-FI, 1038 hu-HU, 1043 nl-NL, 1044 nb-NO, 1045 pl-PL, 1048 ro-RO, 1050 hr-HR, 1053 sv-SE, 1055 tr-TR, 1057 id-ID, 1058 uk-UA, 1066 vi-VN, 1081 hi-IN. Every other LCID leaves `lang` empty. `{\upr}` keeps `{\*\ud}` and, when `\ud` is absent, the ANSI fallback. `{\*\aiolistkind checkbox|task}` on the list definition or on the item, and `{\*\aiocheck 0|1}` on the item, restore `list_kind` and `checked`. An image in a table cell becomes the run `[imagem: <alt or name>]` (`wzDescription`, else `wzName`, else `png` or `jpeg`) and is not dropped. `loadRtfFile` can append `RTF import: image in table cell replaced: [imagem: …]` to an optional warnings list; `document_open` does not return that list. `REF` becomes `xref`. `PAGEREF` becomes `xref` with `xref_kind` `page`. `SEQ`, `MERGEFIELD`, `DOCVARIABLE`, `FILLIN`, and `NUMWORDS` become `seq`, `merge`, `var`, `input`, and `word_count`. Quoted `\fldinst` arguments unescape `\\`, `\{`, `\}`, and `\"`, and control characters are removed. `\footnote\ftnalt` is an `endnote` (including `{\footnote\ftnalt{\pard …\par}}`) and the note body keeps character formatting. `\column` is a column break and does not add an empty paragraph.

DOCX import reads run formatting from `w:rPr`: `w:sz` is half-points (`size_pt`), `w:rFonts` uses `w:ascii` or else `w:hAnsi`, `w:color` is `#RRGGBB` (`auto` stays inherited), and `w:highlight` maps the Word name to hex (`yellow` is `#FFFF00`, `green` is `#00FF00`, plus cyan, magenta, blue, red, and the dark/light gray names). `w:b`, `w:i`, `w:strike`, and `w:u` turn off when `w:val` is `0`, `false`, or `off` (`w:u` also when `none`). `w:bidi` sets `dir` to `rtl` only when `w:val` is empty, `1`, `true`, or `on`; `0` and `false` are `ltr`. An inline image splits the paragraph into the text before it, an `image` block, and the text after it, with the same paragraph style. `w:pgSz` stores `width_mm` and `height_mm` (millimetres, two decimal places). The page name is `A4`, `Letter`, `Legal`, `A3`, or `A5` when both sides match within 1 mm, otherwise `custom`. Orientation follows the measurements (`width_mm` > `height_mm` is landscape). A nested `w:tbl` stays inside `cell.nested` and does not replace the outer table. `w:gridSpan` is `col_span`. `w:vMerge` with `restart` plus each following `continue` sets `row_span` on the restart cell; continuation cells are not separate cells. A paragraph whose style is `Caption` immediately after a table or image becomes that block's `caption_runs` (a `SEQ` field stays a `seq` run) and is not its own block. `docProps/core.xml` supplies `dc:title`, `dc:subject`, `cp:keywords`, and `dc:creator` (`author`) when `word/aioffice.json` does not already set that value. A field inside `w:del` is stored on the delete revision's `deleted_runs`, not in the live paragraph. Lists come from `word/numbering.xml` (`w:num` → `w:abstractNum` → `w:lvl` / `w:numFmt`). `bullet`, `decimal`, `lowerLetter`, `upperLetter`, `lowerRoman`, and `upperRoman` become `list_kind`; `w:ilvl` is the 1-based `level`. When `word/numbering.xml` is absent, `numId` 1 is still `decimal` and `numId` 2 is still `bullet`. Native `w:ins`, `w:del`, and `w:rPrChange` are always imported. Revisions already in `word/aioffice.json` are loaded first. A native mark is dropped when a JSON revision with the same `block_id` and `kind` already covers its range (`start`/`end`), including the per-run `w:rPrChange` slices of one format revision.

### `document_save`

Args: `{ "path"?: string, "title"?: string }`  
Saves to `path` or the last opened/saved path. Format from extension. Every format is written with `QSaveFile` (the previous file stays if the write or commit fails). A target that exists and is not a regular file (a device, a fifo, or a symlink to one) fails the same way. On failure the result is `{ok:false,error}` and the working path and dirty flag stay as they were. Native session JSON (`.aio.json`, `.json`, `.aio`), `.odt`, and `.docx` become the working file (the working path is updated, dirty cleared) and refresh fields on the live document only after a successful write. `.epub`, `.pdf`, `.md`, `.txt`, `.html`, `.rtf`, and `.doc` are exports: fields are refreshed on a copy used only for the write, then the live document is restored, so path, dirty, and cached field text stay unchanged (unless `title` is a new value, which still dirties the session); the file is written and the working path stays unchanged. The dirty flag stays unchanged unless `title` is a new value. `.doc` is export-only and is written by LibreOffice as Word 97 (`doc:MS Word 97`); it does not become the working file. `title`, when set to a new value, updates the session title before the write; on an export path the document stays dirty until a later native JSON (`.aio.json`, `.json`, `.aio`), `.odt`, or `.docx` save. RTF export warnings are returned in `warnings`. **RTF** (`.rtf`) is written natively
(`RtfWriter`: `\fonttbl`, `\colortbl`, `\stylesheet`, paragraph/character formatting, `\listtable` with nine
levels and `\ilvl`, tables with `\trhdr` and `col_widths_mm` as cumulative `\cellx` twips — equal `9000/cols`
widths only when `col_widths_mm` is absent — `\page`, `\footnote` and `\ftnalt` for endnotes, with the note
body's character formatting and `\chftn` inside the note, `\pict\pngblip`, `\field`, `\bkmkstart`/`\bkmkend` at
`Bookmark.offset`, `\u`/`\uc`, `\rtlpar`, `\lang` from the shared LCID table). Heading spacing `\sb240`/`\sa120`
is written only on a side that has no explicit spacing, and an explicit 0 wins. Checkbox and task lists are
bullet lists (other readers see ☐/☑) plus `{\*\aiolistkind checkbox}` or `task` and `{\*\aiocheck 0|1}` on each
item. Field instructions `REF`, `SEQ`, `MERGEFIELD`, `DOCVARIABLE`, `FILLIN`, and `NUMWORDS` (`word_count`) quote
their arguments and escape `\`, `{`, `}`, and `"`; control characters are stripped. An image whose `anchor` is
`tableId:row:col` (0-based) is not a `\pict`: the cell gains `[imagem: alt or file name]` and `warnings` names
the image. An anchor in that shape that matches no cell is still written as `\pict` and warned, never dropped.
`hide_if` / `section_hidden` text is written with `\v` and a warning that includes the block id; that is not
redaction. An unknown language is written with no `\lang` and a warning. Each paragraph starts with `\plain`
(after `\pard\intbl` in a cell), so a missing or unknown language does not keep the previous paragraph's `\lang`.
A region that is not in the table (`he-IL`, `pt-AO`) is unknown and does not fall back to the language prefix.
A tag with no region uses the principal variant (`pt` is pt-BR / 1046, `en` is en-US / 1033, `es` is es-ES / 3082,
`fr` is fr-FR / 1036, `de` is de-DE / 1031, `it` is it-IT / 1040, `zh` is zh-CN / 2052). Tracked revisions and shapes/forms are flattened to plain
text. Block comments, including comments on a table or a break, are flattened to plain text and warned (full
fidelity stays in `.aio.json`/ODT/DOCX).

`.epub` writes EPUB 3 and does not import it (`document_open` on `.epub` returns `{ok:false,error}`: "EPUB import is not supported"). The package is `mimetype` first and stored (no compression), `META-INF/container.xml`, and `OEBPS/content.opf` with `dc:title`, `dc:creator` when the document has an author, `dc:language`, a stable `dc:identifier` (`urn:uuid` version 5 of the title and author), `dcterms:modified`, and schema.org accessibility metadata (`accessMode`, `accessibilityFeature`, `accessibilityHazard`, `accessibilitySummary`). `nav.xhtml` lists headings and landmarks. Each Heading 1 starts an XHTML file; a document with no Heading 1 is one file; content before the first Heading 1 is front matter. CSS comes from the named paragraph and character styles. A run's `charStyle` is a CSS class on its span, generated the same way as a paragraph style. Two style names that sanitize to the same class get a stable numeric suffix in style-list order (`-2`, `-3`, …); the first name keeps the base class. Images are package resources with their media type (PNG, JPEG, or GIF; anything else is re-encoded as PNG, rejected above 32MB, when the path is not a regular file, or when it is not a bitmap). Block ids are sanitized to XML ids; two ids that sanitize to the same string get a numeric suffix (`-2`, `-3`, …) so the result stays unique. Tables use `th scope="col"` for the header row. Lists are nested `ol`/`ul`. A custom `list_bullet` is `list-style-type: "<bullet> "` on that `li` (one trailing space inside the CSS string; `\` and `"` are escaped and control characters are dropped). Notes are `aside epub:type="footnote"` (or `endnote`) plus `epub:type="noteref"`. The aside role is `doc-footnote`, or `note` for an endnote. The aside carries the note block's `lang`, `xml:lang`, and `dir`. A note with no in-flow reference is still written, and the warning names its id. A note whose references are all hidden (`hidden` or a false `show_if`) is omitted together with those noterefs; a mix keeps the note and only the visible noterefs. `xml:lang`/`lang` follow the block and, when a run's language differs, the span. Every block gets `dir`: an explicit `ltr` or `rtl` wins, and `auto` (stored empty) follows the first strong character, the same rule as layout. Fields are the evaluated text, except `page_number` and `page_count`, which are empty because EPUB has no pages, and an `xref` of kind `page`, which is a link to the target's title or caption. Each of those substitutions is listed in `warnings`. A block whose `hide_if` is true is omitted. Page breaks are omitted. Shapes, charts, and QR codes are PNG painted by the same figure painter as the page, with `alt`. On success the result adds `format` (`"epub"`), `chapters`, `images`, and `identifier`, plus `warnings` when an orphan note or a page-field substitution was published.

### `document_export_pdf`

Args: `{ "path": string, "pdfa"?: bool, "ua"?: bool, "from"?: int, "to"?: int, "booklet"?: bool, "copies"?: int, "comments"?: bool }`  
Native `QPdfWriter` (no LibreOffice). Every export embeds document XMP (`dc:title` / `dc:creator` / `dc:description` as RDF values, `pdf:Keywords` / `pdf:Producer`, `xmp:CreateDate` / `ModifyDate` / `CreatorTool`) and the same fields in `/Info` (plus `Creator`). Document language is catalog `/Lang`, not `dc:language`. No `pdf:Trapped` in XMP or `/Info`. `pdfa=true` requests PDF/A-1b (`pdfaid:part=1`, `conformance=B`); translucent watermark, image opacity, and drop shadows are painted opaque and listed in `warnings[]`. `ua=true` adds `pdfuaid:part=1` (with the PDF/A extension schema when combined with `pdfa`), sets `/ViewerPreferences/DisplayDocTitle true`, and fails with `{ok:false,error}` if any image lacks `alt`; without `ua`, missing alt is only a `warnings[]` entry. `from`/`to` are 1-based inclusive. `booklet` imprints 2-up in booklet order. `copies` (default 1) repeats the exported page sequence. Each repetition has its own structure elements, keyed by that copy and the block id, so two copies of one paragraph are two elements and each one points at a single page. Tagged PDF (wave 19) emits `/L`/`/LI`/`/Lbl`/`/LBody`, `/Table`/`/TR`/`/TH`/`/TD` (the header row is repeated on each page fragment). A list item whose `level` is deeper than the item before it is an inner `/L` child of that parent `/LI`. A merged cell writes `/A << /O /Table /ColSpan n /RowSpan m >>` on its `/TD` or `/TH` when the span is greater than 1; a cell covered by that span is an `/Artifact`, not a second cell. UTF-16 literal strings (`/Alt`, annotation `/Contents` and `/T`, `/Info`) escape `(`, `)`, `\`, CR, LF, TAB, BS, and FF. The same tree also emits `/Figure` with `/Alt`, `/Note`, `/TOC`/`/TOCI`, `/Annot` when `comments` is set, `/Artifact` for page chrome, catalog `/Lang`, and `/Span` `/Lang` when a run's language differs. `cell.nested` (a list or table inside a cell) stays inside the parent `/TD` as running text and does not get its own `/L` or `/Table`. When the content-stream clips do not match the marker list, that page falls back to one `/Part`. `comments:true` writes real PDF sticky-note annotations. A path that exists and is not a regular file (a fifo, a device, or a symlink to one) fails with `{ok:false,error:"Cannot write <path>"}` before that path is opened. The PDF is rendered in a private temporary file and published with `QSaveFile` only after rendering and post-processing finish, so a failed export leaves an existing file unchanged. Falls back to soffice only if the native renderer fails. A destination that cannot be published returns the write error and does not call soffice.

### `document_get`

Args: `{}`  
Returns `{ "ok", "document": { ...full JSON... }, "words" }`. `words` is the same visible count as `document_stats.words` (a block hidden by `hide_if` or `section_hidden` is omitted). Before the JSON is built, `if`, `seq`, and xref `category_number` are refreshed to one fixed point using the real layout page total (`page` in the field text stays 1). On a word-count cycle the lexicographic minimum is kept, so two `document_get` calls with no edit return the same text. `stat` is rewritten only when the document is dirty; a clean read leaves an imported stat cache alone. `document_stats` lays out the cached text and does not refresh fields.

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
With `start`/`end`, applies to that range of the block’s plain text. Otherwise all runs. `highlight` is painted in `page_preview`. `hidden` omits the run from preview. `char_style` names a `create_style` with `kind=character`. `kerning` is extra letter-spacing percent (preview + ligatures). `lang` is stored on the run (BCP 47). `show_if`: a single variable name keeps the old rule (the run is hidden when `doc.vars[name]` is empty, `0`, or `false`). Any other value is an expression (`field_evaluate`); a syntax error displays `#ERR`.

### `apply_paragraph_format`

Args: `{ "ids": ["p1"], "align"?: "left"|"center"|"right"|"justify"|"start"|"end", "dir"?: "auto"|"ltr"|"rtl", "style"?: string, "indent_left_mm"?: number, "indent_right_mm"?: number, "indent_first_mm"?: number, "space_before_pt"?: number, "space_after_pt"?: number, "line_height"?: number, "keep_with_next"?: bool, "break_before"?: bool, "orphans"?: int, "widows"?: int, "drop_cap_lines"?: int, "tabs"?: [{"pos_mm": number, "align"?: "left"|"right"|"center"|"decimal"}], "border_pt"?: number, "border_color"?: string, "list_restart"?: bool, "list_bullet"?: string, "lang"?: string, "writing_mode"?: string, "hide_if"?: string }`  
`drop_cap_lines` ≥ 2 paints a visual drop cap in `page_preview`. Tab stops pair with `insert_tab` / `\t` in run text. `list_restart` resets decimal numbering; `list_bullet` is the custom marker. `keep_with_next` holds the next block on the same page; `break_before` starts a new page. `hide_if` is an expression (same language as `field_evaluate`). It is not redaction. When it is true the block is omitted from layout, `page_preview`, PDF, EPUB, and from `document_stats` / `stat` counts (except `stat` `pages`, which stays the real page total). The block stays in the JSON and in `search`. ODT and DOCX keep the block in the package. ODT writes a paragraph, heading, or list item into `content.xml` and, when `hide_if` is non-empty, inserts `<text:hidden-paragraph text:condition="ooow:…"/>`, plus `text:is-hidden="true"` when the expression is true. An ODT image that has bytes is still stored under `Pictures/`; that image paragraph is not given `text:hidden-paragraph`. ODT tables, breaks, and the table of contents are written without `text:hidden-paragraph` as well. DOCX writes the paragraph, including a table, a break, or an image. When the expression is true the paragraph mark is `w:vanish` (`w:pPr/w:rPr/w:vanish`); runs of an ordinary paragraph and of table cells are vanished too. The image part is still added under `word/media/`. A DOCX table of contents is written without `w:vanish`. When JSON is embedded, the expression (true or false) is stored in `word/aioffice.json` `conditionals`. It is evaluated **once before pagination**, always with `page=1` and `pages=1` (same rule as layout and PDF). Expressions that reference `page` or `pages` (in any letter case, e.g. LibreOffice's `Page`), and any other invalid expression, are rejected by `insert_blocks`, `replace_range`, `apply_paragraph_format`, `document_include`, and native JSON open (`.aio.json`, `.json`, `.aio`). The call returns `{ok:false, error}` and does not change the document, the dirty flag, or the undo stack. The page/pages message is `hide_if cannot depend on page or pages (evaluated once, before pagination)`; a syntax error is the parser error (usually `invalid expression`). `validate` reports `invalid_expression` with the block id. On ODT and DOCX import, a `hide_if` that is invalid or that references `page` or `pages` is dropped, the block stays visible, and the file still opens. Pass `""` to clear it.

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

Args: `{ "size"?: "A4"|…, "orientation"?: "portrait"|"landscape", "margins_mm"?: [t, r, b, l], "columns"?: 1|2|3, "column_widths"?: [2, 1], "hyphenate"?: bool, "hyphen_zone_mm"?: number, "gutter_mm"?: number, "gutter_mirror"?: bool, "column_gap_mm"?: number, "column_separator"?: bool, "border_pt"?: number, "border_color"?: string, "background"?: string, "style"?: string, "number_format"?: "arabic"|"roman"|"alpha", "number_start"?: int, "width_mm"?: number, "height_mm"?: number, "tone"?: "sepia"|"night", "grid_mm"?: number, "line_numbers"?: bool, "chapter_numbers"?: bool, "booklet"?: bool, "writing_mode"?: "vertical", "note_restart"?: bool }`  
`style` copies `page_size` / `page_orientation` from a `kind=page` style. `number_format` / `number_start` change the printed page label; `insert_break {page_restart:true, page_number?:n}` restarts the counter. `column_widths` are relative weights when `columns` > 1. `width_mm`+`height_mm` override named sizes. ODT export writes those millimetres; reopening names the page as above. Alignment written into ODT is one of `left`, `right`, `center`, `justify`, `start`, `end`, and page orientation is `portrait` or `landscape`. Style names and other ODT attribute values are XML-escaped.

### `insert_table`

Args: `{ "after_id": "end", "rows": 4, "cols": 3, "header": true, "header_texts"?: ["A","B"], "cells"?: ["..."], "col_widths_mm"?: [40, 40, 40] }`

If `header` is true, `header_texts` fill **row 0**. `cells` fill from **row 1**. Row 0 is painted as a header band. `rows` must be 1..200 and `cols` 1..64 (omitted values stay 3). Anything else fails with `{ok:false, error:"table rows/cols out of range"}` before the grid is built. When `header` is true and `rows` is 1, the row count becomes 2 after that check. `nest_table` uses the same cap (omitted `rows`/`cols` stay 2).

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
`all` defaults to **false** (first match). `regex` uses `QRegularExpression`. Returns `{ "ok", "count" }`. Tracked revisions on body text move with each replacement. A replacement that would cut a revision in half fails the whole call with `{ok:false, error:"replacement would split a revision"}` and leaves the document unchanged. Table-cell matches do not move revisions.

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
Splits at the caret if `id` is omitted. The new block takes `create_style.next` when set. Revisions at or after the cut move onto the new block (offsets rebased by the cut). A range that crosses the cut is split into two revisions. `join_blocks` and a backspace that joins blocks rebase the absorbed block's revisions onto the survivor.

### `insert_labels`

Args: `{ "records": ["A", "B"] | [{"text":"A"}], "cols"?: 2, "rows"?: int, "after_id"?: "end" }`  
Builds a table (no header) filled left-to-right, top-to-bottom.

### `set_cell` / `insert_row` / `insert_col` / `delete_row` / `delete_col`

Table surgery on a table `id` (the selected table in the GUI). `set_cell`: `{id, row, col, text|runs, nested_row?, nested_col?, border_pt?, border_color?, background?, v_align?: top|middle|bottom, padding_mm?, formula?: "sum:0"|"avg:1"|"count:0"}`. `nested_*` writes into a nested table created by `nest_table`. Omit `text`/`runs` to change only cell format. `table_formulas {id?}` evaluates formulas (skips other formula cells) and writes the result into the cell text. `table_auto_width {id}` sets `col_widths_mm` from text length. `create_style {kind:table, color}` + `apply_style` fills the header row. Tables taller than a page split across pages and **repeat row 0** when `header` is set. No PDF etiquetado, o conteúdo de `cell.nested` (lista ou tabela aninhada) fica dentro do `/TD` da célula, como texto corrido: não ganha `/L`/`LI`/`LBody` nem um `/Table` próprio. `insert_footnote` não ancora numa célula — `TableCell` não tem id.

### `selection_get` / `set_selection` / `insert_at_caret`

Shared caret: `{anchor_id, anchor_off, focus_id, focus_off, row?, col?}`. `set_selection` does not mark the document dirty; `dirty` in the result is the flag already stored. `insert_at_caret` types at the caret. If the selection is a range (offsets differ on the same block), the range is **replaced**.

### `move_blocks` / `duplicate_blocks`

Args: `{ "ids": [...], "after_id": "end" }`

### `comment_add` / `comment_resolve`

Args: `{ "id": "p1", "text": "...", "author"?: "IA" }` / `{ "comment_id": "..." }`

### `document_set_title` / `set_header` / `set_footer`

Title/author/subject/keywords/watermark; header/footer `{text, page_numbers, chapter?: bool, image?: path, even?: bool, first?: bool}`. A header or footer that has only `chapter` or `image` set is still stored in the document JSON. `chapter=true` appends the last Heading 1 on or before the page (also `{chapter}` in `text`). `set_vars {vars:{k:v}}` or `{name,value}`. `insert_field {kind:var, arg}` reads vars. `set_read_only {on}` blocks mutations except reads, export, `document_new`/`open`, and itself.

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

`export_markdown {path?, ids?}` returns `{markdown}` and optionally writes UTF-8. Whole document if `ids` omitted. With `path`, the file is written atomically; a write failure returns `{ok:false, error}`.  
`export_text` same with `{text}` (tables as tab-separated rows).  
`export_html` same with `{html}` (simple semantic HTML).  
`copy_markdown` is the selection (or `ids`) without a file.

### `document_diff`

Ids `added` / `removed` / `changed` / `moved` vs the document at last **save**, **open**, **new**, or **template** (not vs the last undo frame).

### `import_markdown` / `document_from_template`

Markdown → blocks. Inline: `**bold**`, `*italic*`, `[text](url)`. Nested lists by indent. Templates: `carta` | `ata` | `contrato` | `envelope` (`template` or `name`). Envelope uses paper **DL** landscape. If the session is dirty, pass `force=true`. Optional `fields` replaces `{{placeholders}}` (cidade, data, nome, …). Title is human (`Carta`, not `carta`). Path is cleared.

### `link_set` / `bookmark_add` / `insert_crossref`

Hiperlink na seleção (`url`). Marcador nomeado no bloco (`name`, `id` opcional). Referência cruzada (`target` = id de bloco ou bookmark) como campo `xref`.

`insert_crossref` args: `{ "target": string, "kind"?: "text"|"page"|"heading_number"|"above_below"|"category_number"|"footnote_number"|"endnote_number", "id"?, "offset"? }`. `text` (default) insere o texto/legenda do alvo — comportamento de antes. `page` mostra o **número de página vivo** do alvo (recalculado a cada `page_preview`/export; sem layout — ex. round-trip ODT/DOCX — cai de volta no texto do alvo). `heading_number` mostra o número de contorno do heading alvo (`1.2`). `category_number` mostra o número do campo **seq** da legenda numerada do alvo (`insert_caption` com `seq`); se não houver seq, cai no ordinal entre blocos do mesmo tipo (image/table/…). `footnote_number`/`endnote_number` mostram a ordem da nota entre as notas referenciadas. `above_below` é resolvido **uma vez**, no momento do insert (compara a posição do alvo com `id`/caret atual) e vira texto "acima"/"abaixo" fixo — não é um campo vivo (simplificação documentada, `Session::insertCrossref`).

### `insert_field`

Args: `{ "kind": "page_number"|"page_count"|"date"|"title"|"author"|"xref"|"filename"|"word_count"|"custom"|"merge"|"if"|"doc_info"|"stat"|"sender"|"seq"|"input_list", "arg"?: string|object, "xref_kind"?: string }`  
Insere no caret. `xref`/`custom`/`merge` usam `arg`. Campo `merge` mostra `«arg»` até `mail_merge`. `date` usa `arg` como formato Qt (`dd/MM/yyyy` por omissão; `yyyy-MM-dd hh:mm` para data/hora). `if` takes `arg` as `{ "cond": string, "then": string, "else": string }` (object or JSON string). `cond` is an expression; the field shows `then` or `else`. `document_get` and a working-document save re-evaluate it with the real page total (`pages`) and `page` 1. A syntax error shows `#ERR` and `validate` reports `invalid_expression`.

### `field_evaluate`

Args: `{ "expr": string }`  
Evaluates `expr` against the open document. Literals: numbers (`1`, `2.5`, `-3`) and strings (`"..."` or `'...'`). Names other than the numeric fields below read `doc.vars` as strings. Numeric fields: `page`, `pages`, `words`, `chars`, `paragraphs`, `tables`, `images`. Operators, tightest first: parentheses, `!`, comparisons (`== != < > <= >=`), `&&`, `||`. A comparison is numeric only when both sides are numbers; otherwise both sides are compared as strings (`2 < "10"` is false). Returns `{ "value", "type": "number"|"string"|"bool" }`. Syntax errors return `{ "ok": false, "error": "invalid expression" }`. `words`, `chars`, `paragraphs`, `tables` and `images` are the visible counts of the `stat` field and `document_stats` (blocks hidden by `hide_if` or `section_hidden` are left out). Inside a `hide_if` expression they are the totals of all blocks instead, so hiding cannot depend on itself. The `word_count` field shows the same visible word count.

**Onda 18 — tipos novos:** `doc_info` com `arg` = `subject`|`keywords`|`created`|`modified`|`revision` (metadados do documento; `created`/`modified` podem vir de `doc.vars["doc.created"]` / `doc.modified`). `stat` com `arg` = `pages`|`words`|`chars`|`paragraphs`|`tables`|`images` (contagem de blocos visíveis; `pages` usa o layout quando disponível). `sender` com `arg` = `name`|`email`|`company`|`phone` lê `doc.vars["sender.<arg>"]`. `seq` com `arg` JSON `{name, format?}` (`format`: `arabic`|`roman`|`alpha`; contagem por `name` na ordem do documento). `input_list` com `arg` JSON `{options[], selected?}` mostra `selected` ou a primeira opção.

**Fidelidade de round-trip ODT/DOCX (onda 12):** `page_number, page_count, date, title, author, filename, word_count, xref` mapeiam para elementos de campo nativos nos dois formatos e são lidos de volta ao reabrir. `merge, var, custom, input, time, cite` agora também mapeiam para instruções DOCX reais (`MERGEFIELD`, `DOCVARIABLE`, `FILLIN`, `TIME`, instr próprio para `cite`) e são lidos de volta; em ODT continuam via `text:user-field-get` genérico (nome do campo preservado, funcional mas não um elemento ODF dedicado). `chapter` vira `STYLEREF` no DOCX (aproximação — Word não tem equivalente exato).

### `insert_footnote` / `insert_endnote`

Args: `{ "text"?: string }`  
Marca sobrescrita no caret + bloco `footnote`/`endnote` com id `fn*`/`en*`. A âncora tem de ser um bloco com id (parágrafo, heading, item de lista). Uma célula de tabela não tem id, então a nota não gruda no conteúdo da célula.

### `toc_update`

Cria ou regenera o bloco `toc` a partir dos headings. Args: `{ min_level?: 1, max_level?: 6, styles?: ["Title"] }`. `toc_targets` guarda os ids.

### `convert_note`

Args: `{ "id": "fn1" }`  
Alterna o bloco `footnote` ↔ `endnote`.

### `insert_index`

Args: `{ "terms": ["Alfa", "Beta"], "title"?: "Índice remissivo", "after_id"?: "end" }`  
Insere um Heading 1 e um parágrafo por termo com números de página (`concordance` é alias).

### Wave 10 — objectos, página, revisão

- `insert_shape` `{shape: rect|ellipse|line, fill?, stroke?, width_mm?, height_mm?, connect_to?}`
- `insert_chart` `{data: [{label, value}]}`
- `insert_qr` `{qr: "texto"}` — símbolo QR ISO/IEC 18004, byte mode, versões 1–10, nível M. O texto é UTF-8. Acima de 213 bytes devolve `{ok:false, error:"QR payload exceeds version 10 level M capacity"}` e não grava um símbolo inválido.
- `insert_equation` `{text, numbered?}`
- `insert_form` `{kind: text|checkbox|dropdown, text?}`
- ODT writes `shape`, `chart`, `qr`, and `form` as a PNG from the same block painter as the page (`svg:title` and `svg:desc` are `alt`). The original block is stored in `aioffice:extra` and restored on import when that blob is present. Without the blob the frame stays an image.
- `bibliography_set` / `insert_citation` — citação mínima (não é processador CSL)
- `figure_index` — índice de ilustrações (legendas)
- `autotext_set` / `autotext_insert`
- `document_stats`, `document_compare {path}`, `document_a11y`, `document_links`, `grammar_check`
- `redact`, `repeat_last`, `snapshot_save` / `list` / `restore`
- `set_user_dic`, `thesaurus_get` / `thesaurus_set`
- `align_objects {align: left|center|right|distribute}`
- `revision_view {view: original|final}`
- `set_page_layout` extra: `width_mm`, `height_mm`, `tone`, `grid_mm`, `line_numbers`, `chapter_numbers`, `gutter_mirror`, `hyphen_zone_mm`, `booklet`, `writing_mode`
- `document_export_pdf` extra: `from`, `to`, `booklet` (2-up), `copies`, `comments` (onda 13: anotações PDF reais, ver abaixo — página extra só no modo `booklet`)
- `set_image` extra: `flip_h`, `flip_v`, `crop`, `shadow_pt`, `z_index`
- `comment_add` extra: `parent_id`
- `insert_field` extra: `time`, `cite`, `filepath`

### Wave 11 — biblioteca, órfãs, notas, mestre

- `image_library_add` `{path}` / `image_library_list` / `image_library_insert` `{index|path, after_id?}`
- `insert_bibliography` `{style: apa|abnt, title?, after_id?}` — lista APA/ABNT-lite a partir de `bibliography_set` (não é CSL)
- `document_include` `{path, after_id?}` — insere outro `.aio.json` com IDs de bloco novos. Revisões e marcadores são copiados; `block_id` segue o mapa. Um id que não colide é mantido (mesmo não numérico). Colisão ou id vazio aloca `revN` / `bmN` pelo maior número já usado + 1.
- `find_replace` extra: `format` aplicado aos runs substituídos
- `toc_update` cria estilos nomeados `TOC 1`…`TOC 6` e aplica `char_style` por nível
- `track_changes` + `apply_character_format` grava `Revision.payload` (JSON do formato)
- `set_page_layout` `note_restart` numera notas por página no `page_preview`
- Órfãs/viúvas: o parágrafo parte entre páginas (default 2 linhas)

### Wave 15 — tagged PDF real de parágrafo/heading (cumprindo o compromisso da onda 13)

Todo `document_export_pdf` nativo agora sai com tagging real de granularidade de **parágrafo/heading**, não mais só de página. Como `QPainter`/`QPdfWriter` não dão nenhum gancho para injetar `BDC`/`EMC`+`MCID` durante o desenho, a solução foi recuperar os limites de cada bloco de volta do content stream que o `QPdfWriter` já escreveu: cada bloco de texto é pintado através de exatamente uma chamada `p->setClipRect(lb.rect...)` (`Preview.cpp`), que o motor de PDF do Qt sempre traduz numa sequência de operadores `x y m ... l ... l ... l h W* n` distintiva — verificada empiricamente contra a saída real antes de confiar nela — que não aparece em mais nenhum lugar (tabelas/imagens/formas usam outros operadores). Cada `BT...ET` (objeto de texto) entre um desses limites e o próximo pertence exatamente a um bloco, na mesma ordem em que foi pintado.

- Cada `BT...ET` é embrulhado com `/<Tag> <</MCID n>> BDC ... EMC`; parágrafos que quebram em várias linhas (várias `BT...ET`) viram **um único** elemento de estrutura com várias entradas em `/K` — testado e confirmado via `qpdf --show-object`.
- Elementos de estrutura reais: `/H1`–`/H6` para heading (pelo nível), `/P` para parágrafo/item de lista, com `/Pg` e `/ParentTree` corretos (MCID → elemento).
- **Segurança em primeiro lugar**: por página, só prossegue com o tagging fino se a contagem de limites de bloco encontrados no content stream bater exatamente com a contagem de blocos esperados (calculada do nosso próprio `lay.blocks`) — qualquer coisa fora do esperado (tipo de bloco não coberto, mudança futura na renderização) cai de volta pro `/Part` de página inteira, sem nunca arriscar uma tag errada/desalinhada.
- **Cobertura (onda 19)**: a técnica do clip passou a cobrir `/L`/`/LI`/`/Lbl`/`/LBody`, `/Table`/`/TR`/`/TH`/`/TD` (cabeçalho repetido em cada fragmento de página), `/Figure` com `/Alt`, `/Note`, `/TOC`/`/TOCI`, `/Annot`, artifacts de chrome e `/Lang` (catálogo e `/Span`). `cell.nested` continua texto corrido dentro do `/TD` pai, sem `/L` nem `/Table` próprios. Itens de lista com `level` maior entram numa `/L` interna do `/LI` pai. `copies` maior que 1 não funde os elementos das cópias. Célula com `col_span` ou `row_span` grava `/ColSpan` e `/RowSpan` em `/A` com `/O /Table`; a célula coberta sai como artifact. Strings literais UTF-16 escapam CR, LF, TAB, BS e FF, além de `(`, `)` e `\`. O fallback `/Part` fica só quando a contagem de clips não bate com os marcadores.
- Validado com `qpdf --check` e `qpdf --show-object` (biblioteca PDF independente, não é código nosso) em vários cenários: parágrafo simples, parágrafo com quebra de linha (múltiplas linhas → um elemento com `/K` de vários MCIDs), documento misto com tabela.
- `src/core/PdfPostProcess.cpp` ganhou um tokenizador de content stream PDF (`tokenize`, `findClipBoundaries`, `findTextSpans`) e compressão FlateDecode própria (`flateCompress`/`inflateObjStream`, via zlib direto) — reaproveitado o mesmo mecanismo de atualização incremental da onda 13.

### Wave 14 — formatador CSL real (APA/ABNT/MLA/Chicago)

Pesquisei antes de começar: não existe biblioteca CSL C/C++ embutível com licença compatível (`citeproc-rs`, do Zotero, é Rust e não tem FFI-C viável; `citeproc-java` é JVM; os outros processadores conhecidos são Haskell/Emacs Lisp). A solução foi escrever um formatador real (`src/core/Csl.h/.cpp`), não um interpretador genérico de arquivo `.csl` — cobre 4 estilos × 4 tipos de referência com regras verificadas contra os manuais de cada estilo (não é "Autor (Ano). Título." fixo como antes).

- **`bibliography_set`**: `BiblioEntry` ganhou campos reais — `type` (book\|article-journal\|webpage\|chapter), `container_title`, `publisher`, `city`, `volume`, `issue`, `pages`, `edition`, `editor`, `doi`, `url`, `accessed` — além de `author`/`title`/`year` que já existiam. `author` aceita múltiplos autores separados por `;` (`"Silva, João; Souza, Maria"`); heurística de parsing de nome documentada (com vírgula = `Last, First` direto; sem vírgula = último espaço separa sobrenome). `cite_style` (apa\|abnt\|mla\|chicago) é uma propriedade do documento, não por citação — muda tudo de uma vez, inclusive citações já inseridas (recalculadas a cada leitura, como `page_number`).
- **`insert_citation`**: o campo `cite` agora é formatado de verdade por `csl::inText` conforme `doc.cite_style` — `(Silva & Souza, 2020)` em APA, `(SILVA; SOUZA, 2020)` em ABNT, `(Silva 23)` (autor+página, sem ano) em MLA. Note: isto **muda** o texto exibido de citações antigas (antes era sempre `Autor (Ano)`, sem parênteses externos nem vírgula — o formato antigo não era realmente correto para nenhum estilo real).
- **`insert_bibliography`**: cada entrada agora sai em **múltiplos runs** (não um texto plano só) — título em itálico real onde o estilo pede, recuo hanging (`indent_left_mm`/`indent_first_mm`) como uma lista de referências de verdade. Ordenada por autor então ano. Regras verificadas por tipo e estilo, ex. APA artigo: `Jones, R. (2019). Deep Learning Advances. *Journal of AI*, 12(3), 45-60.`; MLA: `Jones, Robert. "Deep Learning Advances." *Journal of AI*, vol. 12, no. 3, 2019, pp. 45-60.`; ABNT: `JONES, Robert. Deep Learning Advances. *Journal of AI*, v. 12, n. 3, p. 45-60, 2019.`
- **Fora de escopo, documentado**: arquivo `.csl` arbitrário (estilos além dos 4 embutidos) — exigiria um interpretador CSL-XML genérico, sem biblioteca C/C++ pronta disponível; ampliar pra mais estilos/tipos fica para quando o utilizador pedir.

### Wave 13 — fidelidade de PDF (anotações reais + tagged PDF de página)

`QPdfWriter` (Qt 6.11, confirmado via documentação oficial: sem `/StructTreeRoot`, sem API de anotação) não dá nenhum gancho pra isso — a solução é um pós-processador próprio (`src/core/PdfPostProcess.h/.cpp`) que faz uma **atualização incremental de PDF** no arquivo já escrito: acrescenta objetos novos + uma seção xref nova + trailer no fim do ficheiro, sem tocar nos bytes originais (mecanismo padrão do formato PDF, não é um hack nosso). Validado com `qpdf --check` e `qpdf --show-object` (resolve corretamente através da cadeia `/Prev`).

- **`document_export_pdf {comments:true}`** agora produz **anotações PDF reais** (`/Subtype /Text`, sticky note clicável, com `/Contents` e `/T` do autor) posicionadas na página e no bloco certos — `QPdfWriter` já aloca um `/Annots` vazio por página; só redefinimos esse array. Substitui a página extra "Comentários" de texto simples. Funciona com `copies` (a mesma anotação é repetida em cada cópia física) e com intervalos `from`/`to`. **Não funciona em `booklet`** (duas páginas lógicas por folha física tornam a posição ambígua) — nesse caso cai de volta pra página extra de texto, como antes.
- **Tagged PDF de granularidade de página (fallback).** Quando o tagging fino da onda 15 não consegue correlacionar o content stream (tabela, imagem, mismatch), a página cai neste fallback: `/MarkInfo <</Marked true>>` + `/StructTreeRoot` (`/Document` → um `/StructElem /Part` por página, ligado via `/Pg`). Não é o caminho principal — ver onda 15 acima.

### Wave 12 — fidelidade de round-trip, hiperlinks, campos, crossref, hifenação real

- **Bookmarks e revisões (insert) → marcação real em ODT e DOCX**, não só o blob `aioffice:extra`/`word/aioffice.json` (que continua a ser gravado e existe como *fallback* de merge, preferindo a marcação nativa quando presente). No import DOCX, `w:ins`, `w:del` e `w:rPrChange` são sempre lidos; quando o JSON também traz revisões, uma marca nativa é descartada se uma revisão JSON do mesmo bloco e do mesmo tipo já cobre o intervalo (`start`/`end`). Um campo dentro de `w:del` volta em `deleted_runs`, não no texto vivo. ODT: `<text:bookmark text:name="…"/>` inside `text:p` / `text:h` at the bookmark offset (a `text:bookmark-start`/`end` pair in that same place is read the same way); `<text:change-start/end>` + `<text:tracked-changes>` para revisões `insert`. DOCX: `<w:bookmarkStart/End>` with a `w:id` unique across the document; `<w:ins w:author w:date>` for `insert`. A `delete` is a `w:del` built from `deletedRuns` at the point where the text was removed. A field inside that deletion is `w:delText` of the displayed text, not a live `w:fldSimple`; the field kind and argument stay on the revision in `word/aioffice.json`. Format revisions are `w:rPrChange`. When the prior formatting had `bold`/`italic`/`underline`/`strike` on and the live run no longer does, the current run spells that off explicitly (`w:val="0"` / `w:val="none"` for `w:u`) instead of just omitting the tag, so a native-only reimport (no `word/aioffice.json` sidecar) can tell "turned off" apart from "never set" and rebuild the same revision payload. A `FILLIN` prompt is field-quoted and XML-escaped once. `w:color` is written only for six hex digits (an optional leading `#` is stripped); any other value is omitted. XML 1.0 C0 controls other than tab, LF, and CR are dropped on export. **Revisões `delete` no ODT continuam só no blob JSON** quando o modelo de edição remove o texto apagado imediatamente de `runs` (só fica um registo em `Revision.text`), então não há span vivo para embrulhar num `<text:change>` real; recriar isso exigiria reinserir `rev.text` sinteticamente na exportação ODT, deixado para uma onda futura. Revisões `format` sem marca nativa também continuam só no blob quando não há mapeamento limpo em ODF/OOXML.
- **Hiperlink corrigido nos dois sentidos e nos dois formatos.** Antes: DOCX escrevia `w:anchor` para toda âncora (inclusive URL externa, que não funciona assim) e nenhum dos dois formatos lia `text:a`/`w:hyperlink` de volta — todo hiperlink se perdia ao reabrir. Agora: link interno (`#id`) usa `w:anchor`/`xlink:href` local; link externo aloca uma relationship (`r:id` + `_rels/document.xml.rels`, `TargetMode="External"`) no DOCX. Leitura implementada nos dois formatos.
- `insert_at_caret` agora grava `Revision` (insert/delete) quando `track_changes` está ligado — antes só a digitação ao vivo no canvas (IPC) e `apply_character_format` gravavam revisão; a tool MCP mais usada para editar texto não gravava nada.
- Hifenação real Knuth-Liang (`src/core/Hyphenation.h/.cpp`, dicionários `.dic` em `src/core/hyphen/`, proveniência e licenças em `hyphen/NOTICE.md`) substitui a heurística de sufixos. `hyph_en_US.dic` (GPL-2.0/LGPL-2.1/MPL-1.1 tri-license, usado sob LGPL-2.1) e `hyph_pt_BR.dic` (LGPLv3/MPL) — ambos compatíveis com GPL-3.0-or-later. Idioma por `TextRun.lang`/`Block.lang` (prefixo `en` = inglês, resto = português).
- Correção de doc desatualizada: `Revision.payload` (formato JSON de track changes) e `document_export_pdf` `from`/`to`/`copies` já estavam implementados desde a onda 9/11 — só o catálogo estava desatualizado.

### `spellcheck`

Hunspell se o binário foi compilado com a lib e existir dicionário (`DICPATH` ou `/usr/share/hunspell`). `{ available, hits: [{id,start,end,word,suggestions}] }`.

### `track_changes` / `revision_list` / `revision_accept` / `revision_reject`

`track_changes { on }` grava insert/delete/format. `revision_accept_all` / `revision_reject_all`. Aceitar remove a marca; rejeitar um `insert` desfaz o texto.

### `autocorrect_get` / `autocorrect_set`

Mapa `{from: to}` no JSON do documento (sem GUI). O caret aplica a lista + defaults (`teh`→`the`, …).

### `merge_cells` / `split_cells` / `table_sort` / `nest_table`

`merge_cells {id, row, col, col_span?, row_span?}`. `table_sort {id, col}` ordena linhas de dados.  
`nest_table {id, row, col, rows?, cols?}` cria uma tabela de um nível dentro da célula (`cell.nested` no JSON). No PDF etiquetado esse miolo continua no `/TD` pai, sem `/Table` aninhado. ODT writes `table:number-columns-spanned`, `table:number-rows-spanned`, `table:covered-table-cell`, and a nested `table:table` inside the cell, and reads that markup back. DOCX export writes `w:gridSpan` for `col_span` and `w:vMerge` (`restart` on the anchor, `continue` on the cells below) for `row_span`; cells covered on the same row are omitted. A nested table is a `w:tbl` inside the cell.

### `mail_merge`

Args: `{ "records": [ {"nome": "Ana"}, {"nome": "Bia"} ] }`  
Clona o corpo por registo, substitui `{{chave}}` e campos `merge`, e insere quebra de página entre cópias. IDs novos. Undo restaura o modelo.

### `clear_format` / `paint_format` / `insert_special` / `toggle_check`

`clear_format {ids, start?, end?}` tira formato de carácter (mantém texto).  
`paint_format {from_id, ids, start?, end?}` copia o formato do run em `from_id`.  
`insert_special {kind: nbsp|shy|emdash|endash|ellipsis|copyright|euro}` no caret.  
`toggle_check {id, checked?}` em itens `checkbox`.

### `set_image` / `insert_caption`

Alt, caption, wrap, âncora, `width_mm`, `rotate` (graus), `opacity` (0–1) no bloco imagem (ou caption em tabela). `caption` and `caption_runs` are stored on every block that has them, not only images. `plainText()` is the run text used by the caret, layout, diff, outline, and the DOCX fingerprint. Word and character counts add the caption through `statText()`. `insert_caption` args: `{ id, text?, seq?: { name, format?: arabic|roman|alpha } }` — com `seq`, grava `caption_runs` (`"<name> "` + campo seq + texto) para numeração viva e xref `category_number`. DOCX export writes that caption as the next paragraph after the table or image, style `Caption`. A `seq` in `caption_runs` is a native `SEQ` field.

### `undo` / `redo`

Args: `{}`  
Each frame is the document, its path, and the dirty flag. Undo and redo restore all three. They do not force dirty.

### `page_preview`

Args: `{ "page"?: 1, "zoom"?: 1.15 }` or `{ "page": "all", "zoom"?: 1.15 }` or `{ "from": 1, "to": 3, "zoom"?: 1.15 }`  
Default zoom **1.15**. `from`/`to` are inclusive page numbers. `page:"all"` returns `{previews:[{page,png_base64,ids}]}`. A single page also returns `ids`: the block ids laid out on that page. A block whose `hide_if` is true is absent.

### `caret_geometry`

Args: `{ "id": string, "offset": int, "row"?: int, "col"?: int }`  
`offset` is a model offset (`0` … length) in the block's plain text. When `row` and `col` select a table cell, the range is that cell's plain text, not the concatenation of every cell. Unknown id, missing args, or an offset outside that range → `{ok:false,error}`. A block that is not on any page (hidden section) → `{ok:false,error:"block is not laid out"}`.

Returns `{ page, x_mm, y_mm, line, rtl }` plus the usual `ok` envelope. `line` is the 0-based line in that block. `rtl` is true when the glyph at the caret is right-to-left. `x_mm`/`y_mm` are millimetres from the top-left of the physical page, measured at layout zoom 1 (96 dpi) — the same space `hit_test` reads. Optional `row`/`col` select a table cell; without them a table keeps the previous cell-wide placement.

### `hit_test`

Args: `{ "page": int, "x_mm": number, "y_mm": number }`  
Missing args → `{ok:false,error}`. `page` outside `1 … pages` → `{ok:false,error:"page out of range"}`.

Returns `{ id, offset }` for the block under that point. Body text uses the line's real glyph positions (`QTextLine::xToCursor`); table cells stay on the previous linear placement and also return `row` and `col`. An empty document → `{ok:false,error:"no block"}`.

### `caret_move`

Args: `{ "id": string, "offset": int, "move": "left"|"right"|"next"|"prev" }`  
`offset` is a model offset in the block's plain text (`0` … length). Unknown id, missing args, an offset outside that range, or a `move` other than those four → `{ok:false,error}`. A block that is not on any page, or not a text block (table, image, shape), → `{ok:false,error:"block is not laid out"}`.

Returns `{ offset }` plus the usual `ok` envelope. `left` and `right` are visual (`QTextLayout::leftCursorPosition` / `rightCursorPosition`) and follow the glyph order, including RTL. `next` and `prev` walk one grapheme cluster (`nextCursorPosition` / `previousCursorPosition` with `SkipCharacters`), so a surrogate pair or a combining mark counts as one step. At either end of the text the offset stays put. The canvas keys are not wired here.

### `validate`

Args: `{}`  
Returns `{ ok, valid, errors: [{code,id?,message}], blocks }`. Checks unique ids, known block types, `note_id`, xref targets, bookmarks.

### `batch`

Args: `{ "calls": [ { "tool": "apply_style", "args": { ... } }, ... ] }`  
Runs every call in order. First `{ok:false}` **rolls back** the whole batch. Before any step, `document_save`, `document_open`, `document_new`, `set_read_only` / `set_readonly`, export tools (`document_export_pdf`, `export_markdown`, `export_text`, `export_html`, and their `document_export_*` / `export_txt` aliases), and `batch` / `undo` / `redo` / `checkpoint` / `restore` / `restore_checkpoint` are rejected with `{ok:false, error:"<tool> is not allowed inside batch"}`. One undo frame on success.

### `checkpoint` / `restore`

Args: `{}`  
Snapshot / restore the session (document, path, dirty flag, and selection). Distinct from caret `undo`. `restore` does not force dirty. `restore` fails with `{ok:false,error:"no checkpoint"}`.

### `insert_markdown`

Alias of `import_markdown`. Args: `{ "markdown": string, "after_id"?: "end" }`.

### `integrate_help`

Args: `{}`  
Returns the human/AI integration blurb plus `title`, `words`, `outline`. `words` is that same visible count. No secrets.

## MCP server

- Name: `ai-office`
- Stdio: `aioffice mcp`
- Protocol: JSON-RPC 2.0, MCP `2024-11-05` (`initialize`, `notifications/initialized`, `tools/list`, `tools/call`)
- Transport: one compact JSON object per line, terminated by a newline. A legacy `Content-Length` header is accepted on input only and is never written.

## Live vs headless

`AIOFFICE_HEADLESS=1` forces a private session. Otherwise the live GUI is used when the per-user socket (`aioffice-<uid>`) accepts a connection. A missing socket or a refused connect means there is no GUI, so the CLI and MCP run headless. A failure after connect (timeout, partial frame, invalid response) returns `{ok:false, error:"IPC failure: …"}` and does not edit a blank document. MCP chooses GUI or headless once, on the first `tools/call`, and keeps that choice. `initialize` and `tools/list` do not lock the mode. `aioffice new` honours `AIOFFICE_HEADLESS`. `aioffice play` runs steps in order and stops at the first failing expect.
