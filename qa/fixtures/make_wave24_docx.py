#!/usr/bin/env python3
"""Hand-written Word-style DOCX fixtures for wave 24 import (stdlib only)."""

from __future__ import annotations

import io
import struct
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent

W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
CT = "http://schemas.openxmlformats.org/package/2006/content-types"
REL = "http://schemas.openxmlformats.org/package/2006/relationships"
CP = "http://schemas.openxmlformats.org/package/2006/metadata/core-properties"
DC = "http://purl.org/dc/elements/1.1/"


def png_rgb(w: int = 1, h: int = 1, rgb: bytes = b"\xff\x00\x00") -> bytes:
    def chunk(tag: bytes, data: bytes) -> bytes:
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

    raw = b"".join(b"\x00" + rgb for _ in range(h))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def document(body: str) -> str:
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
        f'<w:document xmlns:w="{W}" xmlns:r="{R}">\n'
        f"<w:body>\n{body}\n</w:body>\n</w:document>\n"
    )


def core_xml(title: str, subject: str = "", keywords: str = "", creator: str = "") -> str:
    parts = [f"<dc:title>{title}</dc:title>"]
    if subject:
        parts.append(f"<dc:subject>{subject}</dc:subject>")
    if creator:
        parts.append(f"<dc:creator>{creator}</dc:creator>")
    if keywords:
        parts.append(f"<cp:keywords>{keywords}</cp:keywords>")
    inner = "".join(parts)
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
        f'<cp:coreProperties xmlns:cp="{CP}" xmlns:dc="{DC}">\n'
        f"{inner}\n</cp:coreProperties>\n"
    )


def content_types(extra: str = "") -> str:
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
        f'<Types xmlns="{CT}">\n'
        '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>\n'
        '<Default Extension="xml" ContentType="application/xml"/>\n'
        '<Default Extension="png" ContentType="image/png"/>\n'
        '<Override PartName="/word/document.xml" '
        'ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>\n'
        '<Override PartName="/docProps/core.xml" '
        'ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>\n'
        f"{extra}</Types>\n"
    )


def root_rels() -> str:
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
        f'<Relationships xmlns="{REL}">\n'
        '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" '
        'Target="word/document.xml"/>\n'
        '<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" '
        'Target="docProps/core.xml"/>\n'
        "</Relationships>\n"
    )


def doc_rels(image: bool) -> str:
    image_rel = ""
    if image:
        image_rel = (
            '<Relationship Id="rId1" '
            'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" '
            'Target="media/image1.png"/>\n'
        )
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
        f'<Relationships xmlns="{REL}">\n{image_rel}</Relationships>\n'
    )


def pack(
    name: str,
    body: str,
    *,
    core: str | None = None,
    numbering: str | None = None,
    extra_json: str | None = None,
    image: bool = False,
) -> None:
    extra_ct = ""
    if numbering is not None:
        extra_ct += (
            '<Override PartName="/word/numbering.xml" '
            'ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml"/>\n'
        )
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types(extra_ct))
        z.writestr("_rels/.rels", root_rels())
        z.writestr("word/document.xml", document(body))
        z.writestr("word/_rels/document.xml.rels", doc_rels(image))
        z.writestr("docProps/core.xml", core if core is not None else core_xml("Wave24"))
        if numbering is not None:
            z.writestr("word/numbering.xml", numbering)
        if extra_json is not None:
            z.writestr("word/aioffice.json", extra_json)
        if image:
            z.writestr("word/media/image1.png", png_rgb())
    (ROOT / name).write_bytes(buf.getvalue())


def p(inner: str, ppr: str = "") -> str:
    pr = f"<w:pPr>{ppr}</w:pPr>" if ppr else ""
    return f"<w:p>{pr}{inner}</w:p>"


def t(text: str, rpr: str = "") -> str:
    props = f"<w:rPr>{rpr}</w:rPr>" if rpr else ""
    return f'<w:r>{props}<w:t xml:space="preserve">{text}</w:t></w:r>'


def sect(w: int, h: int) -> str:
    return f'<w:sectPr><w:pgSz w:w="{w}" w:h="{h}"/><w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440"/></w:sectPr>'


def write_all() -> None:
    pack(
        "wave24-docx-runs.docx",
        "\n".join(
            [
                p(
                    t(
                        "Off",
                        "<w:b w:val=\"0\"/><w:i w:val=\"false\"/><w:u w:val=\"none\"/>"
                        "<w:strike w:val=\"false\"/><w:sz w:val=\"28\"/>"
                        "<w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Arial\"/>"
                        "<w:color w:val=\"FF0000\"/><w:highlight w:val=\"yellow\"/>",
                    ),
                    '<w:bidi w:val="false"/>',
                ),
                p(
                    t(
                        "On",
                        "<w:b/><w:i w:val=\"true\"/><w:u w:val=\"single\"/><w:strike w:val=\"1\"/>"
                        "<w:sz w:val=\"24\"/><w:rFonts w:hAnsi=\"Times New Roman\"/>"
                        "<w:color w:val=\"auto\"/><w:highlight w:val=\"green\"/>",
                    ),
                    '<w:bidi w:val="on"/>',
                ),
                p(t("Zero"), '<w:bidi w:val="0"/>'),
                p(t("Empty"), "<w:bidi/>"),
                sect(12240, 15840),
            ]
        ),
    )

    drawing = (
        '<w:r><w:drawing xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main">'
        '<a:blip r:embed="rId1"/></w:drawing></w:r>'
    )
    pack(
        "wave24-docx-image.docx",
        "\n".join(
            [
                p(t("Before") + drawing + t("After"), '<w:pStyle w:val="Quote"/>'),
                sect(12240, 15840),
            ]
        ),
        image=True,
    )

    pages = {
        "wave24-docx-page-a4.docx": (11906, 16838),
        "wave24-docx-page-letter.docx": (12240, 15840),
        "wave24-docx-page-legal.docx": (12240, 20160),
        "wave24-docx-page-a3.docx": (16838, 23811),
        "wave24-docx-page-a5.docx": (8391, 11906),
        "wave24-docx-page-custom.docx": (10000, 14400),
        "wave24-docx-page-land.docx": (16838, 11906),
    }
    for name, (w, h) in pages.items():
        pack(name, "\n".join([p(t("P")), sect(w, h)]))

    nested = (
        "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/></w:tblPr>"
        "<w:tr>"
        "<w:tc><w:p><w:r><w:t>In1</w:t></w:r></w:p></w:tc>"
        "<w:tc><w:p><w:r><w:t>In2</w:t></w:r></w:p></w:tc>"
        "</w:tr></w:tbl>"
    )
    table = "\n".join(
        [
            "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/></w:tblPr>",
            "<w:tr>",
            "<w:tc><w:p><w:r><w:t>Outer</w:t></w:r></w:p></w:tc>",
            f"<w:tc>{nested}<w:p/></w:tc>",
            "</w:tr>",
            "<w:tr>",
            '<w:tc><w:tcPr><w:vMerge w:val="restart"/></w:tcPr><w:p><w:r><w:t>Tall</w:t></w:r></w:p></w:tc>',
            "<w:tc><w:p><w:r><w:t>Top</w:t></w:r></w:p></w:tc>",
            "</w:tr>",
            "<w:tr>",
            '<w:tc><w:tcPr><w:vMerge w:val="continue"/></w:tcPr><w:p/></w:tc>',
            "<w:tc><w:p><w:r><w:t>Bottom</w:t></w:r></w:p></w:tc>",
            "</w:tr>",
            "<w:tr>",
            '<w:tc><w:tcPr><w:gridSpan w:val="2"/></w:tcPr><w:p><w:r><w:t>Span</w:t></w:r></w:p></w:tc>',
            "</w:tr>",
            "</w:tbl>",
            sect(12240, 15840),
        ]
    )
    pack("wave24-docx-tables.docx", table)

    caption_body = "\n".join(
        [
            "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/></w:tblPr>",
            "<w:tr><w:tc><w:p><w:r><w:t>Cell</w:t></w:r></w:p></w:tc></w:tr>",
            "</w:tbl>",
            p(
                t("Table ")
                + '<w:fldSimple w:instr=" SEQ Table \\* ARABIC "><w:r><w:t>1</w:t></w:r></w:fldSimple>'
                + t(" Sales"),
                '<w:pStyle w:val="Caption"/>',
            ),
            p(drawing, ""),
            p(t("Figure view"), '<w:pStyle w:val="Caption"/>'),
            sect(12240, 15840),
        ]
    )
    pack("wave24-docx-caption.docx", caption_body, image=True)

    pack(
        "wave24-docx-core.docx",
        "\n".join([p(t("Body")), sect(12240, 15840)]),
        core=core_xml("Relatorio", "Vendas", "trimestre", "Ada"),
    )
    pack(
        "wave24-docx-core-json.docx",
        "\n".join([p(t("Body")), sect(12240, 15840)]),
        core=core_xml("FromCore", "Tema", "alfa", "CoreAda"),
        extra_json='{"schema":1,"title":"FromJson","author":"JsonAda","bookmarks":[],"revisions":[]}',
    )

    pack(
        "wave24-docx-del-field.docx",
        "\n".join(
            [
                p(
                    t("Hi ")
                    + '<w:del w:id="1" w:author="Ada" w:date="2024-01-02T03:04:06Z">'
                    + '<w:fldSimple w:instr=" PAGE "><w:r><w:t>1</w:t></w:r></w:fldSimple>'
                    + "</w:del>"
                    + t("there")
                ),
                sect(12240, 15840),
            ]
        ),
    )

    numbering = f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:numbering xmlns:w="{W}">
<w:abstractNum w:abstractNumId="0"><w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="lowerRoman"/><w:lvlText w:val="%1."/></w:lvl></w:abstractNum>
<w:abstractNum w:abstractNumId="1"><w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="upperRoman"/><w:lvlText w:val="%1."/></w:lvl></w:abstractNum>
<w:abstractNum w:abstractNumId="2"><w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="lowerLetter"/><w:lvlText w:val="%1."/></w:lvl></w:abstractNum>
<w:abstractNum w:abstractNumId="3"><w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="upperLetter"/><w:lvlText w:val="%1."/></w:lvl></w:abstractNum>
<w:abstractNum w:abstractNumId="4"><w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="decimal"/><w:lvlText w:val="%1."/></w:lvl></w:abstractNum>
<w:abstractNum w:abstractNumId="5">
<w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="bullet"/><w:lvlText w:val="•"/></w:lvl>
<w:lvl w:ilvl="1"><w:start w:val="1"/><w:numFmt w:val="bullet"/><w:lvlText w:val="◦"/></w:lvl>
</w:abstractNum>
<w:num w:numId="5"><w:abstractNumId w:val="0"/></w:num>
<w:num w:numId="6"><w:abstractNumId w:val="1"/></w:num>
<w:num w:numId="7"><w:abstractNumId w:val="2"/></w:num>
<w:num w:numId="8"><w:abstractNumId w:val="3"/></w:num>
<w:num w:numId="9"><w:abstractNumId w:val="4"/></w:num>
<w:num w:numId="10"><w:abstractNumId w:val="5"/></w:num>
</w:numbering>
"""
    lists = []
    for text, ilvl, num in (
        ("Roman", 0, 5),
        ("ROMAN", 0, 6),
        ("letter", 0, 7),
        ("LETTER", 0, 8),
        ("Decimal", 0, 9),
        ("Bullet", 1, 10),
    ):
        lists.append(
            p(t(text), f'<w:numPr><w:ilvl w:val="{ilvl}"/><w:numId w:val="{num}"/></w:numPr>')
        )
    lists.append(p(t("Plain")))
    lists.append(sect(12240, 15840))
    pack("wave24-docx-lists.docx", "\n".join(lists), numbering=numbering)

    rev_body = "\n".join(
        [
            "<w:p>",
            '<w:ins w:id="1" w:author="Ada" w:date="2024-01-02T03:04:07Z"><w:r><w:t>Old</w:t></w:r></w:ins>',
            '<w:ins w:id="2" w:author="Bea" w:date="2024-06-01T00:00:00Z"><w:r><w:t>New</w:t></w:r></w:ins>',
            "<w:r><w:rPr><w:b/>"
            '<w:rPrChange w:id="9" w:author="Cara" w:date="2024-07-01T00:00:00Z"><w:rPr/></w:rPrChange>'
            "</w:rPr><w:t>X</w:t></w:r>",
            "</w:p>",
            sect(12240, 15840),
        ]
    )
    pack(
        "wave24-docx-revisions.docx",
        rev_body,
        extra_json=(
            '{"schema":1,"track_changes":true,"bookmarks":[],"revisions":['
            '{"id":"rev-json","author":"Ada","kind":"insert","block_id":"p1",'
            '"start":0,"end":3,"text":"Old","timestamp":1700000000000}]}'
        ),
    )


if __name__ == "__main__":
    write_all()
