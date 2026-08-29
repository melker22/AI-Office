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
                  QStringLiteral("Export PDF natively (QPainter). LibreOffice is not required."),
                  QJsonObject{{QStringLiteral("path"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("path")}));
    a.append(tool(QStringLiteral("document_get"),
                  QStringLiteral("Return the full semantic JSON (stable block ids, styles, page layout)."),
                  {}));
    a.append(tool(QStringLiteral("document_get_outline"),
                  QStringLiteral("Return heading outline with ids."),
                  {}));
    a.append(tool(QStringLiteral("insert_blocks"),
                  QStringLiteral("Insert semantic blocks after after_id (start|end|<id>). Omit block ids to auto-assign."),
                  QJsonObject{{QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("blocks"), prop(QStringLiteral("array"))}},
                  QJsonArray{QStringLiteral("blocks")}));
    a.append(tool(QStringLiteral("replace_range"),
                  QStringLiteral("Replace blocks whose ids are listed with new blocks."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("blocks"), prop(QStringLiteral("array"))}},
                  QJsonArray{QStringLiteral("ids"), QStringLiteral("blocks")}));
    a.append(tool(QStringLiteral("delete_range"),
                  QStringLiteral("Delete blocks by id."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"))}},
                  QJsonArray{QStringLiteral("ids")}));
    a.append(tool(QStringLiteral("apply_character_format"),
                  QStringLiteral("Apply bold/italic/underline/font/size/color. Optional start/end offsets on the block text."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("start"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("end"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("bold"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("italic"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("underline"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("font"), prop(QStringLiteral("string"))},
                              {QStringLiteral("size_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("color"), prop(QStringLiteral("string"))},
                              {QStringLiteral("highlight"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("ids")}));
    a.append(tool(QStringLiteral("apply_paragraph_format"),
                  QStringLiteral("Set align (left|center|right|justify) and/or paragraph style name."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("align"), prop(QStringLiteral("string"))},
                              {QStringLiteral("style"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("ids")}));
    a.append(tool(QStringLiteral("apply_style"),
                  QStringLiteral("Apply a named style. Heading N also converts the block to a heading."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("style"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("ids"), QStringLiteral("style")}));
    a.append(tool(QStringLiteral("create_style"),
                  QStringLiteral("Create or replace a named paragraph style."),
                  QJsonObject{{QStringLiteral("name"), prop(QStringLiteral("string"))},
                              {QStringLiteral("font"), prop(QStringLiteral("string"))},
                              {QStringLiteral("size_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("bold"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("italic"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("color"), prop(QStringLiteral("string"))},
                              {QStringLiteral("align"), prop(QStringLiteral("string"))},
                              {QStringLiteral("space_before_pt"), prop(QStringLiteral("number"))},
                              {QStringLiteral("space_after_pt"), prop(QStringLiteral("number"))}},
                  QJsonArray{QStringLiteral("name")}));
    a.append(tool(QStringLiteral("set_page_layout"),
                  QStringLiteral("Set paper size (A4|Letter), orientation, margins_mm [top,right,bottom,left]."),
                  QJsonObject{{QStringLiteral("size"), prop(QStringLiteral("string"))},
                              {QStringLiteral("orientation"), prop(QStringLiteral("string"))},
                              {QStringLiteral("margins_mm"), prop(QStringLiteral("array"))}}));
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
                  QStringLiteral("Insert bullet or decimal list items."),
                  QJsonObject{{QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("kind"), prop(QStringLiteral("string"))},
                              {QStringLiteral("items"), prop(QStringLiteral("array"))}},
                  QJsonArray{QStringLiteral("items")}));
    a.append(tool(QStringLiteral("insert_break"),
                  QStringLiteral("Insert a page break."),
                  QJsonObject{{QStringLiteral("after_id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("kind"), prop(QStringLiteral("string"))}}));
    a.append(tool(QStringLiteral("find_replace"),
                  QStringLiteral("Find/replace plain text across the document."),
                  QJsonObject{{QStringLiteral("find"), prop(QStringLiteral("string"))},
                              {QStringLiteral("replace"), prop(QStringLiteral("string"))},
                              {QStringLiteral("all"), prop(QStringLiteral("boolean"))}},
                  QJsonArray{QStringLiteral("find")}));
    a.append(tool(QStringLiteral("undo"), QStringLiteral("Undo the last semantic edit."), {}));
    a.append(tool(QStringLiteral("redo"), QStringLiteral("Redo."), {}));
    a.append(tool(QStringLiteral("page_preview"),
                  QStringLiteral("PNG preview of a page (base64) so you can verify appearance."),
                  QJsonObject{{QStringLiteral("page"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("zoom"), prop(QStringLiteral("number"))}}));
    a.append(tool(QStringLiteral("integrate_help"),
                  QStringLiteral("How a user AI should attach to AI Office (no API keys). Includes title, words, outline."),
                  {}));
    a.append(tool(QStringLiteral("search"),
                  QStringLiteral("Find text without replacing. Returns ids + start/end. forward=false for reverse."),
                  QJsonObject{{QStringLiteral("find"), prop(QStringLiteral("string"))},
                              {QStringLiteral("forward"), prop(QStringLiteral("boolean"))}},
                  QJsonArray{QStringLiteral("find")}));
    a.append(tool(QStringLiteral("set_cell"),
                  QStringLiteral("Set table cell text or runs."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("row"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("col"), prop(QStringLiteral("integer"))},
                              {QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("runs"), prop(QStringLiteral("array"))}},
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
    a.append(tool(QStringLiteral("move_blocks"),
                  QStringLiteral("Move blocks after after_id."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("after_id"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("ids")}));
    a.append(tool(QStringLiteral("duplicate_blocks"),
                  QStringLiteral("Duplicate blocks after after_id."),
                  QJsonObject{{QStringLiteral("ids"), prop(QStringLiteral("array"))},
                              {QStringLiteral("after_id"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("ids")}));
    a.append(tool(QStringLiteral("comment_add"),
                  QStringLiteral("Add a comment on a block."),
                  QJsonObject{{QStringLiteral("id"), prop(QStringLiteral("string"))},
                              {QStringLiteral("text"), prop(QStringLiteral("string"))},
                              {QStringLiteral("author"), prop(QStringLiteral("string"))}},
                  QJsonArray{QStringLiteral("id"), QStringLiteral("text")}));
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
                  QStringLiteral("Load carta, ata, or contrato. Requires force=true if the session is dirty. Optional fields {{placeholders}}."),
                  QJsonObject{{QStringLiteral("template"), prop(QStringLiteral("string"))},
                              {QStringLiteral("name"), prop(QStringLiteral("string"), QStringLiteral("Alias for template"))},
                              {QStringLiteral("force"), prop(QStringLiteral("boolean"))},
                              {QStringLiteral("fields"), prop(QStringLiteral("object"))}},
                  QJsonArray{}));
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
