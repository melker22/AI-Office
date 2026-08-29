#include "mcp/ToolDispatch.h"

namespace aioffice {
namespace {

QJsonObject prop(const QString &type, const QString &desc = {})
{
    QJsonObject o;
    o[QStringLiteral("type")] = type;
    if (!desc.isEmpty())
        o[QStringLiteral("description")] = desc;
    return o;
}

QJsonObject tool(const QString &name, const QString &desc, const QJsonObject &properties,
                 const QJsonArray &required = {})
{
    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    schema[QStringLiteral("properties")] = properties;
    if (!required.isEmpty())
        schema[QStringLiteral("required")] = required;
    QJsonObject t;
    t[QStringLiteral("name")] = name;
    t[QStringLiteral("description")] = desc;
    t[QStringLiteral("inputSchema")] = schema;
    return t;
}

} // namespace

QJsonArray toolDefinitions()
{
    QJsonArray a;
    a.append(tool(QStringLiteral("document_new"),
                  QStringLiteral("Create a blank Writer document in the current session. Refuses if dirty unless force=true."),
                  QJsonObject{{QStringLiteral("title"), prop(QStringLiteral("string"))},
                              {QStringLiteral("force"), prop(QStringLiteral("boolean"), QStringLiteral("Discard unsaved document"))}}));
    a.append(tool(QStringLiteral("document_open"),
                  QStringLiteral("Open ODT, DOCX, DOC (needs LibreOffice), RTF, or AIO JSON from a filesystem path."),
                  QJsonObject{{QStringLiteral("path"), prop(QStringLiteral("string"), QStringLiteral("Absolute path"))}},
                  QJsonArray{QStringLiteral("path")}));
    a.append(tool(QStringLiteral("document_save"),
                  QStringLiteral("Save the current document. Format is taken from the path extension."),
                  QJsonObject{{QStringLiteral("path"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("document_export_pdf"),
                  QStringLiteral("Export PDF natively (QPainter). pdfa=true writes PDF/A-1b. LibreOffice is not required."),
                  QJsonObject{{QStringLiteral("path"), prop(QStringLiteral("string"))},
                              {QStringLiteral("pdfa"), prop(QStringLiteral("boolean"), QStringLiteral("PDF/A-1b via QPdfWriter"))}},
                  QJsonArray{QStringLiteral("path")}));
    a.append(tool(QStringLiteral("document_get"),
                  QStringLiteral("Return the full semantic JSON (stable block ids, styles, page layout)."),
                  {}));
    a.append(tool(QStringLiteral("document_get_outline"),
                  QStringLiteral("Outline: headings plus tables, images, notes, toc. Each item has id, kind, text."),
                  {}));
    a.append(tool(QStringLiteral("insert_blocks"),
                  QStringLiteral("Insert semantic blocks after after_id (start|end|<id>). Omit block ids to auto-assign."),
                  QJsonObject{{QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("blocks"), prop(QStringLiteral("array"))}},
                  QJsonArray{QStringLiteral("blocks")}));
    a.append(tool(QStringLiteral("replace_range"),
                  QStringLiteral("Replace blocks whose ids are listed with new blocks. ids[] or id."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"), QStringLiteral("Block ids; or pass id"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("Single block id (alias of ids with one entry)"))},
                              {QStringLiteral("blocks"), prop(QStringLiteral("array"))}},
                  QJsonArray{QStringLiteral("blocks")}));
    a.append(tool(QStringLiteral("delete_range"),
                  QStringLiteral("Delete blocks by id."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"), QStringLiteral("Block ids; or pass id"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("Single block id (alias of ids with one entry)"))}}));
    a.append(tool(QStringLiteral("apply_character_format"),
                  QStringLiteral("Apply bold/italic/underline/font/size/color/highlight/small_caps/caps/hidden/char_style/lang/kerning. Optional start/end."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"), QStringLiteral("Block ids; or pass id"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("Single block id (alias of ids with one entry)"))},
                              {QStringLiteral("start"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("end"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("bold"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("italic"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("underline"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("strike"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("superscript"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("subscript"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("link"), prop(QStringLiteral("string"))},
                              {QStringLiteral("font"), prop(QStringLiteral("string"))},
                              {QStringLiteral("size_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("color"), prop(QStringLiteral("string"))},
                              {QStringLiteral("highlight"), prop(QStringLiteral("string"))},
                              {QStringLiteral("small_caps"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("caps"), prop(QStringLiteral("string"), QStringLiteral("upper|lower|title"))},
                              {QStringLiteral("hidden"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("char_style"), prop(QStringLiteral("string"))},
                              {QStringLiteral("lang"), prop(QStringLiteral("string"), QStringLiteral("BCP 47, e.g. pt-PT"))},
                              {QStringLiteral("kerning"), prop(QStringLiteral("number"), QStringLiteral("Extra letter-spacing percent"))}}));
    a.append(tool(QStringLiteral("apply_paragraph_format"),
                  QStringLiteral("Set align, style, indents, spacing, line_height, keep_with_next, orphans/widows, drop_cap, tabs, border, list_restart, list_bullet, lang."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"), QStringLiteral("Block ids; or pass id"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("Single block id (alias of ids with one entry)"))},
                              {QStringLiteral("align"), prop(QStringLiteral("string"))},
                              {QStringLiteral("style"), prop(QStringLiteral("string"))},
                              {QStringLiteral("indent_left_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("indent_right_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("indent_first_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("space_before_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("space_after_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("line_height"), prop(QStringLiteral("number"))},
                              {QStringLiteral("keep_with_next"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("orphans"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("widows"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("drop_cap_lines"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("tabs"), prop(QStringLiteral("array"), QStringLiteral("[{pos_mm, align}] left|right|center|decimal"))},
                              {QStringLiteral("border_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("border_color"), prop(QStringLiteral("string"))},
                              {QStringLiteral("list_restart"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("list_bullet"), prop(QStringLiteral("string"))},
                              {QStringLiteral("lang"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("apply_style"),
                  QStringLiteral("Apply a named style. Heading N also converts the block to a heading."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"), QStringLiteral("Block ids; or pass id"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("Single block id (alias of ids with one entry)"))},
                              {QStringLiteral("style"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("style")}));
    a.append(tool(QStringLiteral("create_style"),
                  QStringLiteral("Create or replace a named style (kind=paragraph|character|list|page)."),
                  QJsonObject{{QStringLiteral("name"), prop(QStringLiteral("string"))},
                              {QStringLiteral("font"), prop(QStringLiteral("string"))},
                              {QStringLiteral("size_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("bold"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("italic"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("color"), prop(QStringLiteral("string"))},
                              {QStringLiteral("align"), prop(QStringLiteral("string"))},
                              {QStringLiteral("space_before_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("space_after_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("kind"), prop(QStringLiteral("string"), QStringLiteral("paragraph|character|list|page"))},
                              {QStringLiteral("next"), prop(QStringLiteral("string"), QStringLiteral("Style applied to the new block after split_block"))},
                              {QStringLiteral("list_kind"), prop(QStringLiteral("string"))},
                              {QStringLiteral("list_bullet"), prop(QStringLiteral("string"))},
                              {QStringLiteral("page_size"), prop(QStringLiteral("string"))},
                              {QStringLiteral("page_orientation"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("name")}));
    a.append(tool(QStringLiteral("set_page_layout"),
                  QStringLiteral("Set paper size, orientation, margins, columns, hyphenate, gutter, border, background, page numbers. style= applies a kind=page style."),
                  QJsonObject{{QStringLiteral("size"), prop(QStringLiteral("string"))},
                              {QStringLiteral("orientation"), prop(QStringLiteral("string"))},
                              {QStringLiteral("margins_mm"), prop(QStringLiteral("array"))},
                              {QStringLiteral("columns"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("hyphenate"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("gutter_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("column_gap_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("column_separator"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("border_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("border_color"), prop(QStringLiteral("string"))},
                              {QStringLiteral("background"), prop(QStringLiteral("string"), QStringLiteral("Page fill color"))},
                              {QStringLiteral("style"), prop(QStringLiteral("string"), QStringLiteral("Named kind=page style"))},
                              {QStringLiteral("number_format"), prop(QStringLiteral("string"), QStringLiteral("arabic|roman|alpha"))},
                              {QStringLiteral("number_start"), prop(QStringLiteral("integer"))}}));
    a.append(tool(QStringLiteral("insert_table"),
                  QStringLiteral("Insert a table after after_id. header_texts fill row 0 when header=true; cells fill data rows after the header."),
                  QJsonObject{{QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("rows"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("cols"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("header"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("header_texts"), prop(QStringLiteral("array"))},
                              {QStringLiteral("cells"), prop(QStringLiteral("array"))},
                              {QStringLiteral("col_widths_mm"), prop(QStringLiteral("array"))}}));
    a.append(tool(QStringLiteral("insert_image"),
                  QStringLiteral("Insert an image reference (filesystem path) after after_id."),
                  QJsonObject{{QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("path"), prop(QStringLiteral("string"))},
                              {QStringLiteral("width_mm"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("path")}));
    a.append(tool(QStringLiteral("insert_list"),
                  QStringLiteral("Insert bullet, decimal, or checkbox list items. Checkbox items may be {text, checked}."),
                  QJsonObject{{QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("kind"), prop(QStringLiteral("string"), QStringLiteral("bullet|decimal|checkbox"))},
                              {QStringLiteral("items"), prop(QStringLiteral("array"))},
                              {QStringLiteral("bullet"), prop(QStringLiteral("string"), QStringLiteral("Custom marker for bullet lists"))},
                              {QStringLiteral("restart"), prop(QStringLiteral("boolean"))}},
                  QJsonArray{QStringLiteral("items")}));
    a.append(tool(QStringLiteral("insert_break"),
                  QStringLiteral("Insert a page or column break (kind=page|column|page_restart)."),
                  QJsonObject{{QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("kind"), prop(QStringLiteral("string"))},
                              {QStringLiteral("page_restart"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("page_number"), prop(QStringLiteral("integer"))}}));
    a.append(tool(QStringLiteral("find_replace"),
                  QStringLiteral("Find/replace. regex=true uses QRegularExpression."),
                  QJsonObject{{QStringLiteral("find"), prop(QStringLiteral("string"))},
                              {QStringLiteral("replace"), prop(QStringLiteral("string"))},
                              {QStringLiteral("all"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("regex"), prop(QStringLiteral("boolean"))}},
                  QJsonArray{QStringLiteral("find")}));
    a.append(tool(QStringLiteral("undo"), QStringLiteral("Undo the last semantic edit."), {}));
    a.append(tool(QStringLiteral("redo"), QStringLiteral("Redo."), {}));
    a.append(tool(QStringLiteral("page_preview"),
                  QStringLiteral("PNG preview of a page (base64). page=n, page=all, or from/to inclusive range."),
                  QJsonObject{{QStringLiteral("page"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("from"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("to"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("zoom"), prop(QStringLiteral("number"))}}));
    a.append(tool(QStringLiteral("integrate_help"),
                  QStringLiteral("How a user AI should attach to AI Office (no API keys). Includes title, words, outline."),
                  {}));
    a.append(tool(QStringLiteral("search"),
                  QStringLiteral("Find text and/or formatting. Format-only search omits find."),
                  QJsonObject{{QStringLiteral("find"), prop(QStringLiteral("string"))},
                              {QStringLiteral("forward"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("regex"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("bold"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("italic"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("underline"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("strike"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("font"), prop(QStringLiteral("string"))},
                              {QStringLiteral("highlight"), prop(QStringLiteral("string"))},
                              {QStringLiteral("char_style"), prop(QStringLiteral("string"))},
                              {QStringLiteral("size_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("lang"), prop(QStringLiteral("string"))},
                              {QStringLiteral("kerning"), prop(QStringLiteral("number"))}}));
    a.append(tool(QStringLiteral("set_cell"),
                  QStringLiteral("Set table cell text or runs. nested_row/nested_col write into a nested table."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("row"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("col"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("runs"), prop(QStringLiteral("array"))},
                              {QStringLiteral("nested_row"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("nested_col"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("id"), QStringLiteral("row"), QStringLiteral("col")}));
    a.append(tool(QStringLiteral("insert_row"),
                  QStringLiteral("Insert a row into a table."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("row"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("id")}));
    a.append(tool(QStringLiteral("insert_col"),
                  QStringLiteral("Insert a column into a table."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("col"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("id")}));
    a.append(tool(QStringLiteral("delete_row"),
                  QStringLiteral("Delete a table row."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("row"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("id"), QStringLiteral("row")}));
    a.append(tool(QStringLiteral("delete_col"),
                  QStringLiteral("Delete a table column."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("col"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("id"), QStringLiteral("col")}));
    a.append(tool(QStringLiteral("selection_get"), QStringLiteral("Return the shared caret/selection."), {}));
    a.append(tool(QStringLiteral("set_selection"),
                  QStringLiteral("Set caret: anchor_id, anchor_off, focus_id, focus_off, optional row/col."),
                  QJsonObject{{QStringLiteral("anchor_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("anchor_off"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("focus_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("focus_off"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("row"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("col"), prop(QStringLiteral("integer"))}}));
    a.append(tool(QStringLiteral("insert_at_caret"),
                  QStringLiteral("Insert text at the caret. If a range is selected, replace that range."),
                  QJsonObject{{QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("offset"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("text")}));
    a.append(tool(QStringLiteral("insert_line_break"),
                  QStringLiteral("Insert a line break (U+000A) at the caret without splitting the block."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("offset"), prop(QStringLiteral("integer"))}}));
    a.append(tool(QStringLiteral("insert_tab"),
                  QStringLiteral("Insert a tab character at the caret. Pair with apply_paragraph_format tabs[]."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("offset"), prop(QStringLiteral("integer"))}}));
    a.append(tool(QStringLiteral("move_blocks"),
                  QStringLiteral("Move blocks after after_id. ids[] or id."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"), QStringLiteral("Block ids; or pass id"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("Single block id (alias of ids with one entry)"))},
                              {QStringLiteral("after_id"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("duplicate_blocks"),
                  QStringLiteral("Duplicate blocks after after_id. ids[] or id."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"), QStringLiteral("Block ids; or pass id"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("Single block id (alias of ids with one entry)"))},
                              {QStringLiteral("after_id"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("comment_add"),
                  QStringLiteral("Add a comment on a block. ids[] or id."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("author"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("text")}));
    a.append(tool(QStringLiteral("comment_resolve"),
                  QStringLiteral("Mark a comment resolved."),
                  QJsonObject{{QStringLiteral("comment_id"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("comment_id")}));
    a.append(tool(QStringLiteral("document_set_title"),
                  QStringLiteral("Set document title and optional author."),
                  QJsonObject{{QStringLiteral("title"), prop(QStringLiteral("string"))},
                              {QStringLiteral("author"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("set_header"),
                  QStringLiteral("Set header text and page_numbers."),
                  QJsonObject{{QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("page_numbers"), prop(QStringLiteral("boolean"))}}));
    a.append(tool(QStringLiteral("set_footer"),
                  QStringLiteral("Set footer text and page_numbers."),
                  QJsonObject{{QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("page_numbers"), prop(QStringLiteral("boolean"))}}));
    a.append(tool(QStringLiteral("document_diff"),
                  QStringLiteral("Ids added/removed/changed/moved vs last save, open, new, or template."), {}));
    a.append(tool(QStringLiteral("import_markdown"),
                  QStringLiteral("Insert markdown as semantic blocks. Supports **bold**, *italic*, [text](url), nested lists."),
                  QJsonObject{{QStringLiteral("markdown"), prop(QStringLiteral("string"))},
                              {QStringLiteral("after_id"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("markdown")}));
    a.append(tool(QStringLiteral("document_from_template"),
                  QStringLiteral("Load carta, ata, contrato, or envelope (DL landscape). Requires force=true if dirty. Optional fields {{placeholders}}."),
                  QJsonObject{{QStringLiteral("template"), prop(QStringLiteral("string"))},
                              {QStringLiteral("name"), prop(QStringLiteral("string"), QStringLiteral("Alias for template"))},
                              {QStringLiteral("force"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("fields"), prop(QStringLiteral("object"))}},
                  QJsonArray{}));
    a.append(tool(QStringLiteral("link_set"),
                  QStringLiteral("Set hyperlink url on the selection or ids/start/end."),
                  QJsonObject{{QStringLiteral("url"), prop(QStringLiteral("string"))},
                              {QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("start"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("end"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("url")}));
    a.append(tool(QStringLiteral("bookmark_add"),
                  QStringLiteral("Add a named bookmark on a block id (default: caret)."),
                  QJsonObject{{QStringLiteral("name"), prop(QStringLiteral("string"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("offset"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("name")}));
    a.append(tool(QStringLiteral("insert_crossref"),
                  QStringLiteral("Insert a cross-reference field to a block or bookmark id."),
                  QJsonObject{{QStringLiteral("target"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("target")}));
    a.append(tool(QStringLiteral("insert_field"),
                  QStringLiteral("Insert a field: page_number, page_count, date, title, author, xref, filename, word_count, custom, merge."),
                  QJsonObject{{QStringLiteral("kind"), prop(QStringLiteral("string"))},
                              {QStringLiteral("arg"), prop(QStringLiteral("string"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("offset"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("kind")}));
    a.append(tool(QStringLiteral("insert_footnote"),
                  QStringLiteral("Insert a footnote marker at the caret and a footnote block."),
                  QJsonObject{{QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("insert_endnote"),
                  QStringLiteral("Insert an endnote marker at the caret."),
                  QJsonObject{{QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("toc_update"),
                  QStringLiteral("Create or refresh a table of contents block from headings."),
                  {}));
    a.append(tool(QStringLiteral("spellcheck"),
                  QStringLiteral("Hunspell check. Returns hits [{id,start,end,word,suggestions}]."),
                  {}));
    a.append(tool(QStringLiteral("track_changes"),
                  QStringLiteral("Enable or disable change tracking. on=true|false."),
                  QJsonObject{{QStringLiteral("on"), prop(QStringLiteral("boolean"))}}));
    a.append(tool(QStringLiteral("revision_list"),
                  QStringLiteral("List tracked revisions."),
                  {}));
    a.append(tool(QStringLiteral("revision_accept"),
                  QStringLiteral("Accept a revision by id (drop the mark, keep text)."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("id")}));
    a.append(tool(QStringLiteral("revision_reject"),
                  QStringLiteral("Reject an insert revision (remove the inserted text)."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("id")}));
    a.append(tool(QStringLiteral("validate"),
                  QStringLiteral("Validate unique ids, block types, note_id and xref/bookmark refs. Returns {valid, errors[]}."),
                  {}));
    a.append(tool(QStringLiteral("batch"),
                  QStringLiteral("Run calls atomically: [{tool, args}, ...]. Rolls back the whole batch on first {ok:false}."),
                  QJsonObject{{QStringLiteral("calls"), prop(QStringLiteral("array"))}},
                  QJsonArray{QStringLiteral("calls")}));
    a.append(tool(QStringLiteral("checkpoint"),
                  QStringLiteral("Snapshot the session (distinct from caret undo). Pair with restore."),
                  {}));
    a.append(tool(QStringLiteral("restore"),
                  QStringLiteral("Restore the last checkpoint."),
                  {}));
    a.append(tool(QStringLiteral("insert_markdown"),
                  QStringLiteral("Alias of import_markdown: insert markdown after after_id as blocks with new ids."),
                  QJsonObject{{QStringLiteral("markdown"), prop(QStringLiteral("string"))},
                              {QStringLiteral("after_id"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("markdown")}));
    a.append(tool(QStringLiteral("revision_accept_all"),
                  QStringLiteral("Accept every tracked revision (drop marks, keep text)."),
                  {}));
    a.append(tool(QStringLiteral("revision_reject_all"),
                  QStringLiteral("Reject every insert revision and drop all marks."),
                  {}));
    a.append(tool(QStringLiteral("autocorrect_get"),
                  QStringLiteral("Return the document Autocorrect replacement map (MCP, no GUI)."),
                  {}));
    a.append(tool(QStringLiteral("autocorrect_set"),
                  QStringLiteral("Merge replacements {from:to}. replace=true replaces the whole map."),
                  QJsonObject{{QStringLiteral("replacements"), prop(QStringLiteral("object"))},
                              {QStringLiteral("replace"), prop(QStringLiteral("boolean"))}},
                  QJsonArray{QStringLiteral("replacements")}));
    a.append(tool(QStringLiteral("merge_cells"),
                  QStringLiteral("Set col_span/row_span on a table cell."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("row"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("col"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("col_span"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("row_span"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("id"), QStringLiteral("row"), QStringLiteral("col")}));
    a.append(tool(QStringLiteral("split_cells"),
                  QStringLiteral("Reset a table cell span to 1x1."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("row"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("col"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("id"), QStringLiteral("row"), QStringLiteral("col")}));
    a.append(tool(QStringLiteral("table_sort"),
                  QStringLiteral("Sort table data rows by column index (skips header row)."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("col"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("id")}));
    a.append(tool(QStringLiteral("set_image"),
                  QStringLiteral("Set image width_mm, alt, caption, wrap, anchor."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("width_mm"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("alt"), prop(QStringLiteral("string"))},
                              {QStringLiteral("caption"), prop(QStringLiteral("string"))},
                              {QStringLiteral("wrap"), prop(QStringLiteral("string"))},
                              {QStringLiteral("anchor"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("id")}));
    a.append(tool(QStringLiteral("insert_caption"),
                  QStringLiteral("Set caption text on an image or table block."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("text"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("id"), QStringLiteral("text")}));
    a.append(tool(QStringLiteral("nest_table"),
                  QStringLiteral("Insert a one-level nested table inside a cell (rows x cols of runs)."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("row"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("col"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("rows"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("cols"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("id"), QStringLiteral("row"), QStringLiteral("col")}));
    a.append(tool(QStringLiteral("mail_merge"),
                  QStringLiteral("Expand the document once per records[] entry. Replaces {{key}} and merge fields; page break between copies."),
                  QJsonObject{{QStringLiteral("records"), prop(QStringLiteral("array"), QStringLiteral("[{key: value}, ...]"))}},
                  QJsonArray{QStringLiteral("records")}));
    a.append(tool(QStringLiteral("clear_format"),
                  QStringLiteral("Clear character formatting on ids (optional start/end). Keeps text, fields, and notes."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("start"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("end"), prop(QStringLiteral("integer"))}}));
    a.append(tool(QStringLiteral("toggle_check"),
                  QStringLiteral("Toggle or set checked on checkbox list-items."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("checked"), prop(QStringLiteral("boolean"))}}));
    a.append(tool(QStringLiteral("insert_special"),
                  QStringLiteral("Insert nbsp, shy, emdash, endash, ellipsis, copyright, euro, or text at the caret."),
                  QJsonObject{{QStringLiteral("kind"), prop(QStringLiteral("string"))},
                              {QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("offset"), prop(QStringLiteral("integer"))}}));
    a.append(tool(QStringLiteral("paint_format"),
                  QStringLiteral("Copy character format from from_id onto ids (optional start/end)."),
                  QJsonObject{{QStringLiteral("from_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("from_offset"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("start"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("end"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("from_id")}));
    a.append(tool(QStringLiteral("goto"),
                  QStringLiteral("Move the caret to a block id or physical page."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("page"), prop(QStringLiteral("integer"))}}));
    a.append(tool(QStringLiteral("select_by_style"),
                  QStringLiteral("Return block ids that use the named paragraph style and select the first."),
                  QJsonObject{{QStringLiteral("style"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("style")}));
    a.append(tool(QStringLiteral("update_style"),
                  QStringLiteral("Rewrite a named style from the caret run (or id/offset)."),
                  QJsonObject{{QStringLiteral("name"), prop(QStringLiteral("string"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("offset"), prop(QStringLiteral("integer"))}}));
    a.append(tool(QStringLiteral("insert_labels"),
                  QStringLiteral("Insert a label sheet as a table (records fill cells left-to-right)."),
                  QJsonObject{{QStringLiteral("records"), prop(QStringLiteral("array"), QStringLiteral("Strings or {text}"))},
                              {QStringLiteral("cols"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("rows"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("after_id"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("records")}));
    a.append(tool(QStringLiteral("split_block"),
                  QStringLiteral("Split a block at offset. The new block takes the style's next field when set."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("offset"), prop(QStringLiteral("integer"))}}));
    return a;
}

QStringList toolNames()
{
    QStringList names;
    for (const auto &v : toolDefinitions())
        names << v.toObject().value(QStringLiteral("name")).toString();
    return names;
}

} // namespace aioffice
