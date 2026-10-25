#!/usr/bin/env python3
"""Write LibreOffice-shaped ODT fixtures for wave 24 ODT fidelity tests.

The packages are hand-built (not exported by AI Office): page size in centimetres,
list styles, note citation separate from the note body, font-name / font-family,
merged and nested tables, a point bookmark, and a drawing with no embedded block blob.
"""

import base64
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent

OFFICE = "urn:oasis:names:tc:opendocument:xmlns:office:1.0"
STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0"
TEXT = "urn:oasis:names:tc:opendocument:xmlns:text:1.0"
TABLE = "urn:oasis:names:tc:opendocument:xmlns:table:1.0"
DRAW = "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
XLINK = "http://www.w3.org/1999/xlink"
SVG = "urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
DC = "http://purl.org/dc/elements/1.1/"
META = "urn:oasis:names:tc:opendocument:xmlns:meta:1.0"

# 1x1 PNG. Foreign drawings carry real image bytes; there is no aioffice:extra blob.
PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="
)


def content_xml(auto, body):
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<office:document-content xmlns:office="{OFFICE}" xmlns:style="{STYLE}" xmlns:text="{TEXT}" xmlns:table="{TABLE}" xmlns:draw="{DRAW}" xmlns:fo="{FO}" xmlns:xlink="{XLINK}" xmlns:dc="{DC}" xmlns:meta="{META}" xmlns:svg="{SVG}" office:version="1.3">
<office:automatic-styles>{auto}</office:automatic-styles>
<office:body><office:text>{body}</office:text></office:body>
</office:document-content>
"""


def styles_xml(width_cm, height_cm, orient="portrait"):
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<office:document-styles xmlns:office="{OFFICE}" xmlns:style="{STYLE}" xmlns:fo="{FO}" xmlns:svg="{SVG}" office:version="1.3">
<office:automatic-styles>
<style:page-layout style:name="pm1">
<style:page-layout-properties fo:page-width="{width_cm}cm" fo:page-height="{height_cm}cm" style:print-orientation="{orient}" fo:margin-top="2cm" fo:margin-bottom="2cm" fo:margin-left="2cm" fo:margin-right="2cm"/>
</style:page-layout>
</office:automatic-styles>
<office:master-styles>
<style:master-page style:name="Standard" style:page-layout-name="pm1"/>
</office:master-styles>
</office:document-styles>
"""


def meta_xml(title):
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<office:document-meta xmlns:office="{OFFICE}" xmlns:dc="{DC}" xmlns:meta="{META}" office:version="1.3">
<office:meta><dc:title>{title}</dc:title></office:meta>
</office:document-meta>
"""


def manifest_xml(extras):
    parts = [
        '<manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.text"/>',
        '<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>',
        '<manifest:file-entry manifest:full-path="styles.xml" manifest:media-type="text/xml"/>',
        '<manifest:file-entry manifest:full-path="meta.xml" manifest:media-type="text/xml"/>',
    ]
    parts.extend(extras)
    inner = "".join(parts)
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.3">{inner}</manifest:manifest>
"""


def write_odt(name, content, styles, title, extras=None):
    extras = extras or []
    path = HERE / name
    man_extra = []
    files = []
    for href, data, mime in extras:
        files.append((href, data))
        man_extra.append(
            f'<manifest:file-entry manifest:full-path="{href}" manifest:media-type="{mime}"/>'
        )
    # mimetype is the first stored entry, the way LibreOffice writes a package.
    with zipfile.ZipFile(path, "w") as z:
        info = zipfile.ZipInfo("mimetype")
        info.compress_type = zipfile.ZIP_STORED
        z.writestr(info, "application/vnd.oasis.opendocument.text")
        z.writestr("content.xml", content)
        z.writestr("styles.xml", styles)
        z.writestr("meta.xml", meta_xml(title))
        z.writestr("META-INF/manifest.xml", manifest_xml(man_extra))
        for href, data in files:
            z.writestr(href, data)
    print(path.name)


def p(text):
    return f'<text:p text:style-name="Standard">{text}</text:p>'


def main():
    pages = [
        ("wave24-odt-page-legal.odt", "21.59", "35.56", "Legal"),
        ("wave24-odt-page-letter.odt", "21.59", "27.94", "Letter"),
        ("wave24-odt-page-a3.odt", "29.7", "42.0", "A3"),
        ("wave24-odt-page-a5.odt", "14.8", "21.0", "A5"),
        # Wider than tall. print-orientation stays portrait so the measures decide.
        ("wave24-odt-page-a4-land.odt", "29.7", "21.0", "A4 landscape"),
        ("wave24-odt-page-custom.odt", "18.0", "24.0", "Custom"),
    ]
    for name, w, h, title in pages:
        orient = "portrait"
        write_odt(name, content_xml("", p(title)), styles_xml(w, h, orient), title)

    note = (
        '<text:p text:style-name="Standard">Alpha'
        '<text:note text:id="ftn1" text:note-class="footnote">'
        '<text:note-citation>1</text:note-citation>'
        '<text:note-body><text:p text:style-name="Standard">Body only</text:p></text:note-body>'
        "</text:note> beta.</text:p>"
        '<text:p text:style-name="Standard">Gamma'
        '<text:note text:id="en1" text:note-class="endnote">'
        '<text:note-citation>i</text:note-citation>'
        '<text:note-body><text:p text:style-name="Standard">End body</text:p></text:note-body>'
        "</text:note></text:p>"
    )
    write_odt("wave24-odt-notes.odt", content_xml("", note), styles_xml("21.0", "29.7"), "Notes")

    font_auto = (
        '<style:style style:name="TSans" style:family="text">'
        '<style:text-properties style:font-name="Liberation Sans" fo:font-size="14pt"/>'
        "</style:style>"
        '<style:style style:name="TFam" style:family="text">'
        '<style:text-properties fo:font-family="\'Carlito\'" fo:font-size="12pt"/>'
        "</style:style>"
    )
    font_body = (
        '<text:p text:style-name="Standard"><text:span text:style-name="TSans">Sans</text:span></text:p>'
        '<text:p text:style-name="Standard"><text:span text:style-name="TFam">Family</text:span></text:p>'
    )
    write_odt(
        "wave24-odt-font.odt",
        content_xml(font_auto, font_body),
        styles_xml("21.0", "29.7"),
        "Font",
    )

    list_auto = (
        '<text:list-style style:name="Lbullet">'
        '<text:list-level-style-bullet text:level="1" text:bullet-char="•"/>'
        "</text:list-style>"
        '<text:list-style style:name="Lnum">'
        '<text:list-level-style-number text:level="1" style:num-format="1" style:num-suffix="."/>'
        '<text:list-level-style-number text:level="2" style:num-format="a" style:num-suffix=")"/>'
        "</text:list-style>"
        '<text:list-style style:name="Lupper">'
        '<text:list-level-style-number text:level="1" style:num-format="A"/>'
        "</text:list-style>"
        '<text:list-style style:name="Lroman">'
        '<text:list-level-style-number text:level="1" style:num-format="i"/>'
        "</text:list-style>"
        '<text:list-style style:name="LROMAN">'
        '<text:list-level-style-number text:level="1" style:num-format="I"/>'
        "</text:list-style>"
    )
    list_body = (
        '<text:list text:style-name="Lbullet"><text:list-item>'
        + p("Apple")
        + "</text:list-item></text:list>"
        '<text:list text:style-name="Lnum"><text:list-item>'
        + p("One")
        + "<text:list><text:list-item>"
        + p("aye")
        + "</text:list-item></text:list></text:list-item></text:list>"
        '<text:list text:style-name="Lupper"><text:list-item>'
        + p("Bee")
        + "</text:list-item></text:list>"
        '<text:list text:style-name="Lroman"><text:list-item>'
        + p("eye")
        + "</text:list-item></text:list>"
        '<text:list text:style-name="LROMAN"><text:list-item>'
        + p("Eye")
        + "</text:list-item></text:list>"
    )
    write_odt(
        "wave24-odt-lists.odt",
        content_xml(list_auto, list_body),
        styles_xml("21.0", "29.7"),
        "Lists",
    )

    table = (
        '<table:table table:name="Table1">'
        '<table:table-column table:number-columns-repeated="3"/>'
        "<table:table-row>"
        '<table:table-cell table:number-columns-spanned="2" table:number-rows-spanned="2">'
        + p("Merged")
        + '<table:table table:name="Inner">'
        '<table:table-column table:number-columns-repeated="2"/>'
        "<table:table-row>"
        "<table:table-cell>" + p("InA") + "</table:table-cell>"
        "<table:table-cell>" + p("InB") + "</table:table-cell>"
        "</table:table-row></table:table>"
        "</table:table-cell>"
        "<table:covered-table-cell/>"
        "<table:table-cell>" + p("Right") + "</table:table-cell>"
        "</table:table-row>"
        "<table:table-row>"
        '<table:covered-table-cell table:number-columns-repeated="2"/>'
        "<table:table-cell>" + p("Below") + "</table:table-cell>"
        "</table:table-row></table:table>"
    )
    write_odt(
        "wave24-odt-table.odt",
        content_xml("", table),
        styles_xml("21.0", "29.7"),
        "Table",
    )

    marks = (
        '<text:h text:style-name="Heading_20_1" text:outline-level="1">'
        'Ti<text:bookmark-start text:name="mid"/>tle<text:bookmark-end text:name="mid"/>'
        "</text:h>"
        '<text:p text:style-name="Standard">Hello <text:bookmark text:name="here"/>world</text:p>'
    )
    write_odt(
        "wave24-odt-bookmark.odt",
        content_xml("", marks),
        styles_xml("21.0", "29.7"),
        "Bookmark",
    )

    figure = (
        p("Lead")
        + '<text:p text:style-name="Standard">'
        '<draw:frame draw:name="PhotoFrame" svg:width="20mm" svg:height="10mm">'
        "<svg:title>Photo</svg:title><svg:desc>Photo</svg:desc>"
        '<draw:image xlink:href="Pictures/photo.png" xlink:type="simple" xlink:show="embed" xlink:actuate="onLoad"/>'
        "</draw:frame></text:p>"
        + '<text:p text:style-name="Standard">'
        '<draw:rect draw:name="Box1" svg:width="20mm" svg:height="10mm">'
        "<svg:title>Box</svg:title>"
        "</draw:rect></text:p>"
    )
    write_odt(
        "wave24-odt-figure.odt",
        content_xml("", figure),
        styles_xml("21.0", "29.7"),
        "Figure",
        extras=[("Pictures/photo.png", PNG, "image/png")],
    )


if __name__ == "__main__":
    main()
