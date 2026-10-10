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
                              {QStringLiteral("pdfa"), prop(QStringLiteral("boolean"), QStringLiteral("PDF/A-1b via QPdfWriter"))},
                              {QStringLiteral("from"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("to"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("booklet"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("copies"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("comments"), prop(QStringLiteral("boolean"), QStringLiteral("Append a comments page (not PDF annotations)"))}},
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
                              {QStringLiteral("kerning"), prop(QStringLiteral("number"), QStringLiteral("Extra letter-spacing percent"))},
                              {QStringLiteral("ruby"), prop(QStringLiteral("string"))},
                              {QStringLiteral("redact"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("show_if"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("apply_paragraph_format"),
                  QStringLiteral("Set align, dir, style, indents, spacing, line_height, keep_with_next, break_before, orphans/widows, drop_cap, tabs, border, list_restart, list_bullet, lang."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"), QStringLiteral("Block ids; or pass id"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("Single block id (alias of ids with one entry)"))},
                              {QStringLiteral("align"), prop(QStringLiteral("string"), QStringLiteral("left|center|right|justify|start|end"))},
                              {QStringLiteral("dir"), prop(QStringLiteral("string"), QStringLiteral("auto|ltr|rtl"))},
                              {QStringLiteral("style"), prop(QStringLiteral("string"))},
                              {QStringLiteral("indent_left_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("indent_right_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("indent_first_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("space_before_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("space_after_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("line_height"), prop(QStringLiteral("number"))},
                              {QStringLiteral("keep_with_next"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("break_before"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("orphans"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("widows"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("drop_cap_lines"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("tabs"), prop(QStringLiteral("array"), QStringLiteral("[{pos_mm, align}] left|right|center|decimal"))},
                              {QStringLiteral("border_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("border_color"), prop(QStringLiteral("string"))},
                              {QStringLiteral("list_restart"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("list_bullet"), prop(QStringLiteral("string"))},
                              {QStringLiteral("lang"), prop(QStringLiteral("string"))},
                              {QStringLiteral("section_hidden"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("hide_if"), prop(QStringLiteral("string"), QStringLiteral("Expression; paragraph is omitted from layout, preview, and PDF when true"))},
                              {QStringLiteral("writing_mode"), prop(QStringLiteral("string"))},
                              {QStringLiteral("z_index"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("connect_to"), prop(QStringLiteral("string"))},
                              {QStringLiteral("group_id"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("apply_style"),
                  QStringLiteral("Apply a named style. Heading N also converts the block to a heading."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"), QStringLiteral("Block ids; or pass id"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("Single block id (alias of ids with one entry)"))},
                              {QStringLiteral("style"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("style")}));
    a.append(tool(QStringLiteral("create_style"),
                  QStringLiteral("Create or replace a named style (kind=paragraph|character|list|page|frame|table)."),
                  QJsonObject{{QStringLiteral("name"), prop(QStringLiteral("string"))},
                              {QStringLiteral("font"), prop(QStringLiteral("string"))},
                              {QStringLiteral("size_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("bold"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("italic"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("color"), prop(QStringLiteral("string"))},
                              {QStringLiteral("align"), prop(QStringLiteral("string"))},
                              {QStringLiteral("space_before_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("space_after_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("kind"), prop(QStringLiteral("string"), QStringLiteral("paragraph|character|list|page|frame|table"))},
                              {QStringLiteral("next"), prop(QStringLiteral("string"), QStringLiteral("Style applied to the new block after split_block"))},
                              {QStringLiteral("list_kind"), prop(QStringLiteral("string"))},
                              {QStringLiteral("list_bullet"), prop(QStringLiteral("string"))},
                              {QStringLiteral("page_size"), prop(QStringLiteral("string"))},
                              {QStringLiteral("page_orientation"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("name")}));
    a.append(tool(QStringLiteral("set_page_layout"),
                  QStringLiteral("Set paper size, orientation, margins, columns, hyphenate, gutter, border, background, page numbers, custom mm, tone, grid, booklet. style= applies a kind=page style."),
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
                              {QStringLiteral("number_start"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("column_widths"), prop(QStringLiteral("array"), QStringLiteral("Relative column weights when columns>1"))},
                              {QStringLiteral("width_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("height_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("hyphen_zone_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("gutter_mirror"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("line_numbers"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("chapter_numbers"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("note_restart"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("tone"), prop(QStringLiteral("string"), QStringLiteral("sepia|night"))},
                              {QStringLiteral("writing_mode"), prop(QStringLiteral("string"))},
                              {QStringLiteral("grid_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("booklet"), prop(QStringLiteral("boolean"))}}));
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
                  QStringLiteral("Insert a page, column, or section break (kind=page|column|page_restart|section). hidden=true hides the following section."),
                  QJsonObject{{QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("kind"), prop(QStringLiteral("string"))},
                              {QStringLiteral("page_restart"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("page_number"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("hidden"), prop(QStringLiteral("boolean"))}}));
    a.append(tool(QStringLiteral("find_replace"),
                  QStringLiteral("Find/replace. regex=true uses QRegularExpression. Optional format={bold,...} applied to replaced runs."),
                  QJsonObject{{QStringLiteral("find"), prop(QStringLiteral("string"))},
                              {QStringLiteral("replace"), prop(QStringLiteral("string"))},
                              {QStringLiteral("all"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("regex"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("format"), prop(QStringLiteral("object"))}},
                  QJsonArray{QStringLiteral("find")}));
    a.append(tool(QStringLiteral("undo"), QStringLiteral("Undo the last semantic edit."), {}));
    a.append(tool(QStringLiteral("redo"), QStringLiteral("Redo."), {}));
    a.append(tool(QStringLiteral("page_preview"),
                  QStringLiteral("PNG preview of a page (base64). page=n, page=all, or from/to inclusive range."),
                  QJsonObject{{QStringLiteral("page"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("from"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("to"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("zoom"), prop(QStringLiteral("number"))}}));
    a.append(tool(QStringLiteral("caret_geometry"),
                  QStringLiteral("Caret position for a block offset, from the real line layout. Returns page, x_mm, y_mm, line (0-based), rtl."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("offset"), prop(QStringLiteral("integer"), QStringLiteral("Model offset in the block plain text, or in the cell when row and col are set"))},
                              {QStringLiteral("row"), prop(QStringLiteral("integer"), QStringLiteral("Table cell row; omit for body text"))},
                              {QStringLiteral("col"), prop(QStringLiteral("integer"), QStringLiteral("Table cell column"))}},
                  QJsonArray{QStringLiteral("id"), QStringLiteral("offset")}));
    a.append(tool(QStringLiteral("hit_test"),
                  QStringLiteral("Block and model offset under a point on the page (millimetres from the top-left)."),
                  QJsonObject{{QStringLiteral("page"), prop(QStringLiteral("integer"), QStringLiteral("1-based physical page"))},
                              {QStringLiteral("x_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("y_mm"), prop(QStringLiteral("number"))}},
                  QJsonArray{QStringLiteral("page"), QStringLiteral("x_mm"), QStringLiteral("y_mm")}));
    a.append(tool(QStringLiteral("caret_move"),
                  QStringLiteral("Move a text caret by one visual step (left/right) or one grapheme (next/prev). Returns the new model offset."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("offset"), prop(QStringLiteral("integer"), QStringLiteral("Model offset in the block plain text"))},
                              {QStringLiteral("move"), prop(QStringLiteral("string"), QStringLiteral("left|right|next|prev"))}},
                  QJsonArray{QStringLiteral("id"), QStringLiteral("offset"), QStringLiteral("move")}));
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
                  QStringLiteral("Set table cell text, runs, or format (border, background, v_align, padding). nested_row/nested_col write into a nested table."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("row"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("col"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("runs"), prop(QStringLiteral("array"))},
                              {QStringLiteral("nested_row"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("nested_col"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("border_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("border_color"), prop(QStringLiteral("string"))},
                              {QStringLiteral("background"), prop(QStringLiteral("string"))},
                              {QStringLiteral("v_align"), prop(QStringLiteral("string"), QStringLiteral("top|middle|bottom"))},
                              {QStringLiteral("padding_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("formula"), prop(QStringLiteral("string"), QStringLiteral("sum:0 | avg:1 | count:0"))}},
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
    a.append(tool(QStringLiteral("backspace"),
                  QStringLiteral("Delete the character before the caret. With track changes on, records a zero-width delete revision."),
                  {}));
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
                              {QStringLiteral("author"), prop(QStringLiteral("string"))},
                              {QStringLiteral("parent_id"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("text")}));
    a.append(tool(QStringLiteral("comment_resolve"),
                  QStringLiteral("Mark a comment resolved."),
                  QJsonObject{{QStringLiteral("comment_id"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("comment_id")}));
    a.append(tool(QStringLiteral("document_set_title"),
                  QStringLiteral("Set document title, author, subject, keywords, watermark."),
                  QJsonObject{{QStringLiteral("title"), prop(QStringLiteral("string"))},
                              {QStringLiteral("author"), prop(QStringLiteral("string"))},
                              {QStringLiteral("subject"), prop(QStringLiteral("string"))},
                              {QStringLiteral("keywords"), prop(QStringLiteral("string"))},
                              {QStringLiteral("watermark"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("set_header"),
                  QStringLiteral("Set header text, page_numbers, chapter (last Heading 1), image path. even=true or first=true selects the variant."),
                  QJsonObject{{QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("page_numbers"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("chapter"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("image"), prop(QStringLiteral("string"))},
                              {QStringLiteral("even"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("first"), prop(QStringLiteral("boolean"))}}));
    a.append(tool(QStringLiteral("set_footer"),
                  QStringLiteral("Set footer text, page_numbers, chapter, image. even=true or first=true selects the variant."),
                  QJsonObject{{QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("page_numbers"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("chapter"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("image"), prop(QStringLiteral("string"))},
                              {QStringLiteral("even"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("first"), prop(QStringLiteral("boolean"))}}));
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
                  QStringLiteral("Insert a cross-reference field to a block or bookmark id. kind: text (default, target's text/caption) | page (target's live page number) | heading_number (target heading's outline number, e.g. 1.2) | above_below (resolved once at insert time to \"acima\"/\"abaixo\") | category_number (target's ordinal among same-type figures/tables) | footnote_number | endnote_number."),
                  QJsonObject{{QStringLiteral("target"), prop(QStringLiteral("string"))},
                              {QStringLiteral("kind"), prop(QStringLiteral("string"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("offset"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("target")}));
    a.append(tool(QStringLiteral("insert_field"),
                  QStringLiteral("Insert a field: page_number, page_count, date, title, author, xref, filename, word_count, custom, merge, chapter, var, input, time, cite, filepath, if. if takes arg {cond, then, else}."),
                  QJsonObject{{QStringLiteral("kind"), prop(QStringLiteral("string"))},
                              {QStringLiteral("arg"), prop(QStringLiteral("string"), QStringLiteral("For if, a JSON object {cond, then, else} or that object as a string"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("offset"), prop(QStringLiteral("integer"))}},
                  QJsonArray{QStringLiteral("kind")}));
    a.append(tool(QStringLiteral("field_evaluate"),
                  QStringLiteral("Evaluate an expression (numbers, strings, variables, page/pages/words/chars/paragraphs/tables/images, comparisons, && || !, parentheses). Returns {value, type} or {ok:false, error}."),
                  QJsonObject{{QStringLiteral("expr"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("expr")}));
    a.append(tool(QStringLiteral("insert_footnote"),
                  QStringLiteral("Insert a footnote marker at the caret and a footnote block."),
                  QJsonObject{{QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("insert_endnote"),
                  QStringLiteral("Insert an endnote marker at the caret."),
                  QJsonObject{{QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("toc_update"),
                  QStringLiteral("Create or refresh a table of contents from headings. Applies named styles TOC 1…TOC 6. Optional min_level, max_level, styles[] extra paragraph styles."),
                  QJsonObject{{QStringLiteral("min_level"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("max_level"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("styles"), prop(QStringLiteral("array"))}}));
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
                  QStringLiteral("Set image width_mm, alt, caption, wrap, anchor, rotate, opacity, flip_h/v, crop[4], shadow_pt, z_index."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("width_mm"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("alt"), prop(QStringLiteral("string"))},
                              {QStringLiteral("caption"), prop(QStringLiteral("string"))},
                              {QStringLiteral("wrap"), prop(QStringLiteral("string"))},
                              {QStringLiteral("anchor"), prop(QStringLiteral("string"))},
                              {QStringLiteral("rotate"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("opacity"), prop(QStringLiteral("number"))},
                              {QStringLiteral("flip_h"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("flip_v"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("crop"), prop(QStringLiteral("array"))},
                              {QStringLiteral("shadow_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("z_index"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("group_id"), prop(QStringLiteral("string"))}},
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
    a.append(tool(QStringLiteral("join_blocks"),
                  QStringLiteral("Join the block with the previous one."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("insert_html"),
                  QStringLiteral("Insert sanitized HTML as blocks (p, h1-h3, lists, b/i/u, links). Scripts are dropped."),
                  QJsonObject{{QStringLiteral("html"), prop(QStringLiteral("string"))},
                              {QStringLiteral("after_id"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("html")}));
    a.append(tool(QStringLiteral("insert_plain"),
                  QStringLiteral("Insert plain text as paragraphs (one block per line). No character format."),
                  QJsonObject{{QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("after_id"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("text")}));
    a.append(tool(QStringLiteral("insert_frame"),
                  QStringLiteral("Insert a text frame (bordered box) after after_id."),
                  QJsonObject{{QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("width_mm"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("border_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("border_color"), prop(QStringLiteral("string"))},
                              {QStringLiteral("style"), prop(QStringLiteral("string"))},
                              {QStringLiteral("after_id"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("autoformat"),
                  QStringLiteral("Replace --, ..., 1/2 and straight quotes; link bare URLs. lang defaults to pt-PT."),
                  QJsonObject{{QStringLiteral("lang"), prop(QStringLiteral("string"), QStringLiteral("pt-PT or en"))}}));
    a.append(tool(QStringLiteral("load_styles"),
                  QStringLiteral("Merge named styles from another .aio.json (replace same names)."),
                  QJsonObject{{QStringLiteral("path"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("path")}));
    a.append(tool(QStringLiteral("promote_heading"),
                  QStringLiteral("Promote heading level (H2→H1; paragraph→Heading 1). delta default -1."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("delta"), prop(QStringLiteral("integer"))}}));
    a.append(tool(QStringLiteral("demote_heading"),
                  QStringLiteral("Demote heading level (H1→H2; H6 stays; heading→paragraph at 0)."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("delta"), prop(QStringLiteral("integer"))}}));
    a.append(tool(QStringLiteral("wrap_list"),
                  QStringLiteral("Turn paragraphs into list-item blocks (kind: bullet, decimal, checkbox)."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("kind"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("unwrap_list"),
                  QStringLiteral("Turn list-item blocks back into Normal paragraphs."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("ids"), prop(QStringLiteral("array"))}}));
    a.append(tool(QStringLiteral("text_to_table"),
                  QStringLiteral("Replace paragraphs with a table. Each line is a row; split cells by separator (default tab)."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("separator"), prop(QStringLiteral("string"))},
                              {QStringLiteral("header"), prop(QStringLiteral("boolean"))}}));
    a.append(tool(QStringLiteral("table_to_text"),
                  QStringLiteral("Replace a table with one paragraph per row (cells joined by separator)."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("separator"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("split_table"),
                  QStringLiteral("Split a table at row (0-based). The second table starts at that row; header is copied if set."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("row"), prop(QStringLiteral("integer"))}}));
    a.append(tool(QStringLiteral("export_markdown"),
                  QStringLiteral("Serialize the document (or ids) as Markdown. Optional path writes a file."),
                  QJsonObject{{QStringLiteral("path"), prop(QStringLiteral("string"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("ids"), prop(QStringLiteral("array"))}}));
    a.append(tool(QStringLiteral("export_text"),
                  QStringLiteral("Serialize the document (or ids) as plain text. Optional path writes a file."),
                  QJsonObject{{QStringLiteral("path"), prop(QStringLiteral("string"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("ids"), prop(QStringLiteral("array"))}}));
    a.append(tool(QStringLiteral("copy_markdown"),
                  QStringLiteral("Return Markdown for the selection or ids without writing a file."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("ids"), prop(QStringLiteral("array"))}}));
    a.append(tool(QStringLiteral("export_html"),
                  QStringLiteral("Serialize the document (or ids) as HTML. Optional path writes a file."),
                  QJsonObject{{QStringLiteral("path"), prop(QStringLiteral("string"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("ids"), prop(QStringLiteral("array"))}}));
    a.append(tool(QStringLiteral("convert_note"),
                  QStringLiteral("Toggle a footnote block to endnote or the reverse."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("id")}));
    a.append(tool(QStringLiteral("set_vars"),
                  QStringLiteral("Set document variables. Pass vars={k:v} and/or name+value."),
                  QJsonObject{{QStringLiteral("vars"), prop(QStringLiteral("object"))},
                              {QStringLiteral("name"), prop(QStringLiteral("string"))},
                              {QStringLiteral("value"), prop(QStringLiteral("string"))},
                              {QStringLiteral("delete"), prop(QStringLiteral("boolean"))}}));
    a.append(tool(QStringLiteral("set_read_only"),
                  QStringLiteral("Lock mutations except reads, export, document_new/open, and this tool. on=true|false."),
                  QJsonObject{{QStringLiteral("on"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("read_only"), prop(QStringLiteral("boolean"))}}));
    a.append(tool(QStringLiteral("insert_index"),
                  QStringLiteral("Insert a concordance: heading plus one paragraph per term with page numbers."),
                  QJsonObject{{QStringLiteral("terms"), prop(QStringLiteral("array"))},
                              {QStringLiteral("term"), prop(QStringLiteral("string"))},
                              {QStringLiteral("title"), prop(QStringLiteral("string"))},
                              {QStringLiteral("after_id"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("terms")}));
    a.append(tool(QStringLiteral("table_formulas"),
                  QStringLiteral("Evaluate cell formulas (sum:col, avg:col, count:col) and write the result into the cell text."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("table_auto_width"),
                  QStringLiteral("Set col_widths_mm from cell text lengths."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("id")}));
    a.append(tool(QStringLiteral("insert_shape"),
                  QStringLiteral("Insert a shape block (rect|ellipse|line) with fill, stroke, width_mm, height_mm."),
                  QJsonObject{{QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("shape"), prop(QStringLiteral("string"))},
                              {QStringLiteral("kind"), prop(QStringLiteral("string"))},
                              {QStringLiteral("fill"), prop(QStringLiteral("string"))},
                              {QStringLiteral("stroke"), prop(QStringLiteral("string"))},
                              {QStringLiteral("width_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("height_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("connect_to"), prop(QStringLiteral("string"))},
                              {QStringLiteral("z_index"), prop(QStringLiteral("integer"))}}));
    a.append(tool(QStringLiteral("insert_chart"),
                  QStringLiteral("Insert a bar chart. data=[{label,value}]."),
                  QJsonObject{{QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("data"), prop(QStringLiteral("array"))},
                              {QStringLiteral("width_mm"), prop(QStringLiteral("number"))},
                              {QStringLiteral("height_mm"), prop(QStringLiteral("number"))}},
                  QJsonArray{QStringLiteral("data")}));
    a.append(tool(QStringLiteral("insert_qr"),
                  QStringLiteral("Insert a QR preview (byte mode, short payload). qr=text."),
                  QJsonObject{{QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("qr"), prop(QStringLiteral("string"))},
                              {QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("width_mm"), prop(QStringLiteral("number"))}},
                  QJsonArray{QStringLiteral("qr")}));
    a.append(tool(QStringLiteral("insert_equation"),
                  QStringLiteral("Insert a numbered equation line (plain text, not Math OLE)."),
                  QJsonObject{{QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("numbered"), prop(QStringLiteral("boolean"))}},
                  QJsonArray{QStringLiteral("text")}));
    a.append(tool(QStringLiteral("insert_form"),
                  QStringLiteral("Insert a form control block: kind=text|checkbox|dropdown."),
                  QJsonObject{{QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("kind"), prop(QStringLiteral("string"))},
                              {QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("checked"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("options"), prop(QStringLiteral("array"))}}));
    a.append(tool(QStringLiteral("bibliography_set"),
                  QStringLiteral("Set bibliography entries and/or a single one, plus the document's cite_style (apa|abnt|mla|chicago). "
                                 "Entry fields: id, key, author (\"Last, First\", multiple ';'-separated), title, year, "
                                 "type (book|article-journal|webpage|chapter, default book), container_title (journal/parent book/site), "
                                 "publisher, city, volume, issue, pages, edition, editor, doi, url, accessed."),
                  QJsonObject{{QStringLiteral("entries"), prop(QStringLiteral("array"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("key"), prop(QStringLiteral("string"))},
                              {QStringLiteral("author"), prop(QStringLiteral("string"))},
                              {QStringLiteral("title"), prop(QStringLiteral("string"))},
                              {QStringLiteral("year"), prop(QStringLiteral("string"))},
                              {QStringLiteral("type"), prop(QStringLiteral("string"))},
                              {QStringLiteral("container_title"), prop(QStringLiteral("string"))},
                              {QStringLiteral("publisher"), prop(QStringLiteral("string"))},
                              {QStringLiteral("city"), prop(QStringLiteral("string"))},
                              {QStringLiteral("volume"), prop(QStringLiteral("string"))},
                              {QStringLiteral("issue"), prop(QStringLiteral("string"))},
                              {QStringLiteral("pages"), prop(QStringLiteral("string"))},
                              {QStringLiteral("edition"), prop(QStringLiteral("string"))},
                              {QStringLiteral("editor"), prop(QStringLiteral("string"))},
                              {QStringLiteral("doi"), prop(QStringLiteral("string"))},
                              {QStringLiteral("url"), prop(QStringLiteral("string"))},
                              {QStringLiteral("accessed"), prop(QStringLiteral("string"))},
                              {QStringLiteral("cite_style"), prop(QStringLiteral("string"), QStringLiteral("apa|abnt|mla|chicago, document-wide"))}}));
    a.append(tool(QStringLiteral("insert_citation"),
                  QStringLiteral("Insert a cite field. key or arg matches bibliography id/key. Rendered in-text per the document's "
                                 "cite_style (set via bibliography_set); re-renders live if cite_style changes later."),
                  QJsonObject{{QStringLiteral("key"), prop(QStringLiteral("string"))},
                              {QStringLiteral("arg"), prop(QStringLiteral("string"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("figure_index"),
                  QStringLiteral("Insert an illustration index from image/chart/equation captions."),
                  QJsonObject{{QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("title"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("autotext_set"),
                  QStringLiteral("Store autotext snippets. snippets={name:text} and/or name+text."),
                  QJsonObject{{QStringLiteral("snippets"), prop(QStringLiteral("object"))},
                              {QStringLiteral("name"), prop(QStringLiteral("string"))},
                              {QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("replace"), prop(QStringLiteral("boolean"))}}));
    a.append(tool(QStringLiteral("autotext_insert"),
                  QStringLiteral("Insert a named autotext snippet at the caret."),
                  QJsonObject{{QStringLiteral("name"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("name")}));
    a.append(tool(QStringLiteral("document_stats"),
                  QStringLiteral("Word/char/paragraph/page/image/table counts."),
                  {}));
    a.append(tool(QStringLiteral("document_compare"),
                  QStringLiteral("Diff the current document against another .aio.json path."),
                  QJsonObject{{QStringLiteral("path"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("path")}));
    a.append(tool(QStringLiteral("document_a11y"),
                  QStringLiteral("Accessibility report: missing image alt, reading_order."),
                  {}));
    a.append(tool(QStringLiteral("redact"),
                  QStringLiteral("Mark runs as redacted (█ in preview). Same targeting as apply_character_format."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("start"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("end"), prop(QStringLiteral("integer"))}}));
    a.append(tool(QStringLiteral("repeat_last"),
                  QStringLiteral("Repeat the last successful tool call (except itself)."),
                  {}));
    a.append(tool(QStringLiteral("snapshot_save"),
                  QStringLiteral("Save a named version snapshot (beyond checkpoint)."),
                  QJsonObject{{QStringLiteral("name"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("snapshot_list"),
                  QStringLiteral("List named snapshots."),
                  {}));
    a.append(tool(QStringLiteral("snapshot_restore"),
                  QStringLiteral("Restore a named snapshot."),
                  QJsonObject{{QStringLiteral("name"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("name")}));
    a.append(tool(QStringLiteral("set_user_dic"),
                  QStringLiteral("Add extra spell-check words (user.dic). words[] and/or word."),
                  QJsonObject{{QStringLiteral("words"), prop(QStringLiteral("array"))},
                              {QStringLiteral("word"), prop(QStringLiteral("string"))},
                              {QStringLiteral("replace"), prop(QStringLiteral("boolean"))}}));
    a.append(tool(QStringLiteral("thesaurus_get"),
                  QStringLiteral("Read the document thesaurus map. Optional word."),
                  QJsonObject{{QStringLiteral("word"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("thesaurus_set"),
                  QStringLiteral("Set thesaurus entries. map={word:synonyms} and/or word+synonyms."),
                  QJsonObject{{QStringLiteral("map"), prop(QStringLiteral("object"))},
                              {QStringLiteral("word"), prop(QStringLiteral("string"))},
                              {QStringLiteral("synonyms"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("grammar_check"),
                  QStringLiteral("Naive grammar: double spaces and repeated words."),
                  {}));
    a.append(tool(QStringLiteral("document_links"),
                  QStringLiteral("List hyperlinks in runs."),
                  {}));
    a.append(tool(QStringLiteral("align_objects"),
                  QStringLiteral("Align image/shape/frame objects: left|center|right|distribute."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("align"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("revision_view"),
                  QStringLiteral("Set revision view: original|final|empty. Returned on document_get."),
                  QJsonObject{{QStringLiteral("view"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("image_library_add"),
                  QStringLiteral("Append a path to the document image library."),
                  QJsonObject{{QStringLiteral("path"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("path")}));
    a.append(tool(QStringLiteral("image_library_list"),
                  QStringLiteral("List image library paths."),
                  {}));
    a.append(tool(QStringLiteral("image_library_insert"),
                  QStringLiteral("Insert an image from the library (index or path) after after_id."),
                  QJsonObject{{QStringLiteral("index"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("path"), prop(QStringLiteral("string"))},
                              {QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("width_mm"), prop(QStringLiteral("number"))}}));
    a.append(tool(QStringLiteral("insert_bibliography"),
                  QStringLiteral("Insert a bibliography list from document entries, formatted per style: apa|abnt|mla|chicago "
                                 "(default: document cite_style). Covers book/article-journal/webpage/chapter entry types with "
                                 "real per-style rules (author list format, title italics, container/volume/issue/pages), sorted "
                                 "by author then year. Not a generic CSL-file interpreter (no arbitrary custom .csl styles)."),
                  QJsonObject{{QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("title"), prop(QStringLiteral("string"))},
                              {QStringLiteral("style"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("document_include"),
                  QStringLiteral("Insert another .aio.json body after after_id, assigning new stable IDs."),
                  QJsonObject{{QStringLiteral("path"), prop(QStringLiteral("string"))},
                              {QStringLiteral("after_id"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("path")}));
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
