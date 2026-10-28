#!/usr/bin/env python3
"""Build hand-written wave24 foreign DOCX/ODT fixtures and .wave24.json expectations."""

from __future__ import annotations

import io
import json
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent

ODT_MIMETYPE = "application/vnd.oasis.opendocument.text"
ODT_MANIFEST = """<?xml version="1.0" encoding="UTF-8"?>
<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0">
<manifest:file-entry manifest:media-type="application/vnd.oasis.opendocument.text" manifest:full-path="/"/>
<manifest:file-entry manifest:media-type="text/xml" manifest:full-path="content.xml"/>
</manifest:manifest>
"""

CONTENT_TYPES_BASE = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
<Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>
</Types>
"""

RELS_ROOT = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>
</Relationships>
"""

DOC_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"/>
"""


def pack_docx(document_xml: str, core_xml: str, *, extra_parts: dict[str, str] | None = None) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", CONTENT_TYPES_BASE)
        z.writestr("_rels/.rels", RELS_ROOT)
        z.writestr("word/document.xml", document_xml)
        z.writestr("word/_rels/document.xml.rels", DOC_RELS)
        z.writestr("docProps/core.xml", core_xml)
        for name, data in (extra_parts or {}).items():
            z.writestr(name, data)
    return buf.getvalue()


def pack_odt(content_xml: str, *, styles_xml: str | None = None) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("mimetype", ODT_MIMETYPE, compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/manifest.xml", ODT_MANIFEST)
        z.writestr("content.xml", content_xml)
        if styles_xml:
            z.writestr("styles.xml", styles_xml)
    return buf.getvalue()


def write_json(name: str, data: dict) -> None:
    path = ROOT / name
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def write_bytes(name: str, data: bytes) -> None:
    (ROOT / name).write_bytes(data)


def fixture_d1_run_props() -> None:
    doc = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:body>
<w:p><w:r>
<w:rPr>
<w:sz w:val="28"/>
<w:rFonts w:ascii="Arial" w:hAnsi="Arial"/>
<w:color w:val="FF0000"/>
<w:b w:val="0"/><w:i w:val="false"/><w:u w:val="none"/>
</w:rPr>
<w:t>Styled</w:t>
</w:r></w:p>
<w:sectPr><w:pgSz w:w="11906" w:h="16838"/></w:sectPr>
</w:body></w:document>"""
    core = """<?xml version="1.0"?><cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties"
 xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>D1</dc:title></cp:coreProperties>"""
    write_bytes("wave24-docx-run-props.docx", pack_docx(doc, core))
    write_json(
        "wave24-docx-run-props.wave24.json",
        {
            "decision": "D1",
            "audit": ["A-docx-4"],
            "body": [
                {
                    "block_index": 0,
                    "plain": "Styled",
                    "runs": [
                        {
                            "text": "Styled",
                            "size_pt": 14,
                            "font": "Arial",
                            "color": "#FF0000",
                            "bold": False,
                            "italic": False,
                            "underline": False,
                        }
                    ],
                }
            ],
        },
    )


def fixture_d2_inline_image() -> None:
    doc = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"
 xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"
 xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing"
 xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
<w:body>
<w:p><w:r><w:t>Before</w:t></w:r>
<w:r><w:drawing><wp:inline><a:graphic/></wp:inline></w:drawing></w:r>
<w:r><w:t>After</w:t></w:r></w:p>
<w:sectPr><w:pgSz w:w="11906" w:h="16838"/></w:sectPr>
</w:body></w:document>"""
    core = """<?xml version="1.0"?><cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties"
 xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>D2</dc:title></cp:coreProperties>"""
    write_bytes("wave24-docx-inline-image.docx", pack_docx(doc, core))
    write_json(
        "wave24-docx-inline-image.wave24.json",
        {
            "decision": "D2",
            "audit": ["A-docx-5"],
            "block_count_min": 3,
            "block_types": ["paragraph", "image", "paragraph"],
            "body": [
                {"block_index": 0, "type": "paragraph", "plain": "Before"},
                {"block_index": 1, "type": "image"},
                {"block_index": 2, "type": "paragraph", "plain": "After"},
            ],
        },
    )


def fixture_d3_pages() -> None:
    doc = """<?xml version="1.0"?><w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:body><w:p><w:r><w:t>A3</w:t></w:r></w:p>
<w:sectPr><w:pgSz w:w="16838" w:h="23811" w:orient="portrait"/></w:sectPr>
</w:body></w:document>"""
    core = """<?xml version="1.0"?><cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties"
 xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>D3</dc:title></cp:coreProperties>"""
    write_bytes("wave24-docx-page-a3-check.docx", pack_docx(doc, core))
    write_json(
        "wave24-docx-page-a3-check.wave24.json",
        {
            "decision": "D3",
            "audit": ["A-docx-6"],
            "page": {"width_mm": 297, "height_mm": 420, "size": "A3"},
        },
    )
    odt = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
        'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" '
        'xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" '
        'xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3">'
        "<office:automatic-styles>"
        '<style:page-layout style:name="pm1"><style:page-layout-properties fo:page-width="21.59cm" '
        'fo:page-height="35.56cm"/></style:page-layout>'
        "</office:automatic-styles>"
        "<office:body><office:text>"
        '<text:p text:style-name="Standard">Legal</text:p>'
        "</office:text></office:body></office:document-content>"
    )
    write_bytes("wave24-odt-page-legal-check.odt", pack_odt(odt))
    write_json(
        "wave24-odt-page-legal-check.wave24.json",
        {
            "decision": "D3",
            "audit": ["A-odf-4"],
            "page": {"width_mm": 215.9, "height_mm": 355.6, "size": "Legal"},
        },
    )


def fixture_d4_tables() -> None:
    doc = """<?xml version="1.0"?><w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:body><w:tbl><w:tr><w:tc><w:tcPr><w:gridSpan w:val="2"/></w:tcPr><w:p><w:r><w:t>Merged</w:t></w:r></w:p></w:tc></w:tr>
<w:tr><w:tc><w:tbl><w:tr><w:tc><w:p><w:r><w:t>Inner</w:t></w:r></w:p></w:tc></w:tr></w:tbl></w:tc></w:tr></w:tbl>
<w:sectPr/></w:body></w:document>"""
    core = """<?xml version="1.0"?><cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties"
 xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>D4</dc:title></cp:coreProperties>"""
    write_bytes("wave24-docx-nested-table.docx", pack_docx(doc, core))
    write_json(
        "wave24-docx-nested-table.wave24.json",
        {
            "decision": "D4",
            "audit": ["A-docx-7", "A-docx-8"],
            "body": [{"block_index": 0, "type": "table"}],
            "export": {
                "docx": {
                    "must_contain": ["w:gridSpan", "<w:tbl>"],
                }
            },
        },
    )
    odt = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
        'xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" '
        'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" office:version="1.3">'
        "<office:body><office:text><table:table>"
        '<table:table-row><table:table-cell table:number-columns-spanned="2" table:number-rows-spanned="1">'
        '<text:p>Merged</text:p></table:table-cell></table:table-row>'
        "</table:table></office:text></office:body></office:document-content>"
    )
    write_bytes("wave24-odt-merged-cells.odt", pack_odt(odt))
    write_json(
        "wave24-odt-merged-cells.wave24.json",
        {
            "decision": "D4",
            "audit": ["A-odf-6"],
            "export": {
                "odt": {
                    "must_contain": [
                        "table:number-columns-spanned",
                        "table:number-rows-spanned",
                    ]
                }
            },
        },
    )


def fixture_d5_caption() -> None:
    doc = """<?xml version="1.0"?><w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:body>
<w:tbl><w:tr><w:tc><w:p><w:r><w:t>Cell</w:t></w:r></w:p></w:tc></w:tr></w:tbl>
<w:p><w:pPr><w:pStyle w:val="Caption"/></w:pPr><w:r><w:t>Table 1: Demo</w:t></w:r></w:p>
<w:sectPr/></w:body></w:document>"""
    core = """<?xml version="1.0"?><cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties"
 xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>D5</dc:title></cp:coreProperties>"""
    write_bytes("wave24-docx-caption-check.docx", pack_docx(doc, core))
    write_json(
        "wave24-docx-caption-check.wave24.json",
        {
            "decision": "D5",
            "audit": ["A-docx-8"],
            # D5: a Caption paragraph right after a table/image merges into that
            # block's caption_runs; it is never a separate body block.
            "body": [
                {"block_index": 0, "type": "table", "caption": "Table 1: Demo"},
            ],
            "export": {"docx": {"must_contain": ["Caption"]}},
        },
    )


def fixture_d6_meta() -> None:
    doc = """<?xml version="1.0"?><w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:body><w:p><w:r><w:t>Body</w:t></w:r></w:p><w:sectPr/></w:body></w:document>"""
    core = """<?xml version="1.0"?><cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties"
 xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties">
<dc:title>Wave24 Title</dc:title><dc:subject>Wave24 Subject</dc:subject>
<cp:keywords>alpha, beta</cp:keywords><dc:creator>Ada Lovelace</dc:creator>
</cp:coreProperties>"""
    write_bytes("wave24-docx-core-meta.docx", pack_docx(doc, core))
    write_json(
        "wave24-docx-core-meta.wave24.json",
        {
            "decision": "D6",
            "audit": ["A-docx-9"],
            "meta": {
                "title": "Wave24 Title",
                "subject": "Wave24 Subject",
                "keywords": "alpha, beta",
                "author": "Ada Lovelace",
            },
        },
    )


def fixture_d7_field_del() -> None:
    doc = """<?xml version="1.0"?><w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:body><w:p>
<w:del w:id="1" w:author="Bob" w:date="2024-01-01T00:00:00Z">
<w:r><w:fldSimple w:instr=" DATE "><w:r><w:t>2024-01-01</w:t></w:r></w:fldSimple></w:r>
</w:del>
</w:p><w:sectPr/></w:body></w:document>"""
    core = """<?xml version="1.0"?><cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties"
 xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>D7</dc:title></cp:coreProperties>"""
    write_bytes("wave24-docx-field-in-del.docx", pack_docx(doc, core))
    write_json(
        "wave24-docx-field-in-del.wave24.json",
        {
            "decision": "D7",
            "audit": ["A-docx-10"],
            "body": [{"block_index": 0, "plain": ""}],
            "export": {"docx": {"must_contain": ["w:delText"], "must_not_contain": ["<w:fldSimple"]}},
        },
    )


def fixture_d8_list() -> None:
    numbering = """<?xml version="1.0"?><w:numbering xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:abstractNum w:abstractNumId="0"><w:lvl w:ilvl="0"><w:numFmt w:val="lowerLetter"/><w:lvlText w:val="%1."/></w:lvl></w:abstractNum>
<w:num w:numId="7"><w:abstractNumId w:val="0"/></w:num>
</w:numbering>"""
    doc = """<?xml version="1.0"?><w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:body>
<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="7"/></w:numPr></w:pPr><w:r><w:t>Item</w:t></w:r></w:p>
<w:sectPr/></w:body></w:document>"""
    core = """<?xml version="1.0"?><cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties"
 xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>D8</dc:title></cp:coreProperties>"""
    write_bytes(
        "wave24-docx-list-kind.docx",
        pack_docx(doc, core, extra_parts={"word/numbering.xml": numbering}),
    )
    write_json(
        "wave24-docx-list-kind.wave24.json",
        {
            "decision": "D8",
            "audit": ["A-docx-11"],
            "body": [{"block_index": 0, "type": "list-item"}],
        },
    )


def fixture_d12_note() -> None:
    odt = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
        'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" office:version="1.3">'
        "<office:body><office:text>"
        '<text:p>See<text:note text:note-class="footnote">'
        '<text:note-citation>1</text:note-citation>'
        '<text:note-body><text:p>Body only</text:p></text:note-body>'
        "</text:note></text:p>"
        "</office:text></office:body></office:document-content>"
    )
    write_bytes("wave24-odt-footnote.odt", pack_odt(odt))
    write_json(
        "wave24-odt-footnote.wave24.json",
        {
            "decision": "D12",
            "audit": ["A-odf-1"],
            "body": [{"block_index": 0, "plain": "See"}],
        },
    )


def fixture_d13_font() -> None:
    odt = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
        'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" '
        'xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" office:version="1.3">'
        "<office:automatic-styles>"
        '<style:style style:name="T1" style:family="text"><style:text-properties style:font-name="DejaVu Sans"/></style:style>'
        "</office:automatic-styles>"
        "<office:body><office:text>"
        '<text:p><text:span text:style-name="T1">Fonted</text:span></text:p>'
        "</office:text></office:body></office:document-content>"
    )
    write_bytes("wave24-odt-run-font.odt", pack_odt(odt))
    write_json(
        "wave24-odt-run-font.wave24.json",
        {
            "decision": "D13",
            "audit": ["A-odf-5"],
            "body": [
                {
                    "block_index": 0,
                    "plain": "Fonted",
                    "runs": [{"text": "Fonted", "font": "DejaVu Sans"}],
                }
            ],
        },
    )


def fixture_d14_list() -> None:
    styles = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
        'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" '
        'xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" office:version="1.3">'
        "<office:styles>"
        '<text:list-style style:name="L1">'
        '<text:list-level-style-number text:level="1" style:num-format="a" text:style-name="Numbering_20_Symbols"/>'
        "</text:list-style>"
        "</office:styles></office:document-styles>"
    )
    content = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
        'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" office:version="1.3">'
        "<office:body><office:text>"
        '<text:list text:style-name="L1"><text:list-item><text:p>Alpha</text:p></text:list-item></text:list>'
        "</office:text></office:body></office:document-content>"
    )
    write_bytes("wave24-odt-list-style.odt", pack_odt(content, styles_xml=styles))
    write_json(
        "wave24-odt-list-style.wave24.json",
        {
            "decision": "D14",
            "audit": ["A-odf-7"],
            "body": [{"block_index": 0, "type": "list-item", "plain": "Alpha"}],
        },
    )


def fixture_d16_bookmark() -> None:
    odt = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
        'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" office:version="1.3">'
        "<office:body><office:text>"
        '<text:p><text:bookmark-start text:name="bm1"/>Mark<text:bookmark-end text:name="bm1"/></text:p>'
        "</office:text></office:body></office:document-content>"
    )
    write_bytes("wave24-odt-bookmark-check.odt", pack_odt(odt))
    write_json(
        "wave24-odt-bookmark-check.wave24.json",
        {
            "decision": "D16",
            "audit": ["PR-11-ODT-bookmark"],
            # Bookmarks are a single point (block_id + offset) in this app's model,
            # never a start/end range, so a re-export always collapses back to the
            # single-element <text:bookmark> form, not a bookmark-start/-end pair.
            "export": {
                "odt": {
                    "must_contain": ['text:bookmark text:name="bm1"'],
                }
            },
        },
    )


def playbook_files() -> None:
    qa = ROOT.parent
    (qa / "wave24-export-xml-escape.json").write_text(
        json.dumps(
            [
                {"tool": "document_new", "args": {"title": "D9", "force": True}},
                {
                    "tool": "insert_blocks",
                    "args": {
                        "after_id": "end",
                        "blocks": [{"type": "paragraph", "runs": [{"text": "Ctl\u000bX"}]}],
                    },
                },
            ],
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    write_json(
        "../wave24-export-xml-escape.wave24.json",
        {
            "decision": "D9",
            "audit": ["A-docx-12", "A-odf-9"],
            "export_after_play": True,
            "export": {
                "docx": {"must_not_contain": ["\u000b"]},
                "odt": {"must_not_contain": ["\u000b"]},
            },
        },
    )
    (qa / "wave24-odt-shape-export.json").write_text(
        json.dumps(
            [
                {"tool": "document_new", "args": {"title": "D15", "force": True}},
                {"tool": "insert_shape", "args": {"after_id": "end", "shape": "rect", "width_mm": 20, "height_mm": 10}},
            ],
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    write_json(
        "../wave24-odt-shape-export.wave24.json",
        {
            "decision": "D15",
            "audit": ["A-odf-8"],
            "export_after_play": True,
            "export": {"odt": {"odt_png_min": 1, "must_not_contain": ["<text:p></text:p>"]}},
        },
    )
    (qa / "wave24-export-bookmark-ids.json").write_text(
        json.dumps(
            [
                {"tool": "document_new", "args": {"title": "D11", "force": True}},
                {
                    "tool": "insert_blocks",
                    "args": {
                        "after_id": "end",
                        "blocks": [{"type": "paragraph", "runs": [{"text": "One"}]}],
                    },
                },
                {"tool": "bookmark_add", "args": {"id": "p1", "name": "a", "start": 0, "end": 3}},
                {"tool": "bookmark_add", "args": {"id": "p1", "name": "b", "start": 0, "end": 3}},
            ],
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    write_json(
        "../wave24-export-bookmark-ids.wave24.json",
        {
            "decision": "D11",
            "audit": ["PR-11-DOCX-bookmark"],
            "export_after_play": True,
            "export": {"docx": {"unique_bookmark_ids": True}},
        },
    )


def write_zbar_golden_png() -> None:
    """PNG decodable as SELFTEST — prefer qrencode when regenerating fixtures."""
    import shutil
    import subprocess

    out = ROOT / "wave24-zbar-golden.png"
    qrencode = shutil.which("qrencode")
    if qrencode:
        proc = subprocess.run(
            [qrencode, "-o", str(out), "-s", "6", "SELFTEST"],
            capture_output=True,
            check=False,
        )
        if proc.returncode == 0 and out.is_file():
            return
    # Fallback: keep existing file if present (committed artifact).
    if out.is_file():
        return
    raise SystemExit(
        "wave24-zbar-golden.png missing: install qrencode or commit a golden PNG"
    )


def main() -> None:
    fixture_d1_run_props()
    fixture_d2_inline_image()
    fixture_d3_pages()
    fixture_d4_tables()
    fixture_d5_caption()
    fixture_d6_meta()
    fixture_d7_field_del()
    fixture_d8_list()
    fixture_d12_note()
    fixture_d13_font()
    fixture_d14_list()
    fixture_d16_bookmark()
    playbook_files()
    write_zbar_golden_png()
    print("wave24 fixtures written under", ROOT)


if __name__ == "__main__":
    main()
