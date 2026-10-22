#!/usr/bin/env python3
"""Build minimal foreign DOCX/ODT revision fixtures from literal XML (reviewable, no aioffice.json)."""

from __future__ import annotations

import hashlib
import io
import json
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent

# Paragraph 1: insert + zero-width delete (bold "B") + italic format on "A" (mirrors ODT fixture).
# Paragraph 2: mid-word delete "ll" in "Hello" → live "Heo", two deleted runs (bold l + plain l).
# Paragraph 3: "Plain" with rPrChange bold→not bold on leading "P".

# Keep the body paragraph on one line — ODF import treats whitespace text nodes as content.
ODT_CONTENT_XML = (
    '<?xml version="1.0" encoding="UTF-8"?>'
    '<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
    'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" '
    'xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" '
    'xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" '
    'xmlns:dc="http://purl.org/dc/elements/1.1/" office:version="1.3">'
    "<office:automatic-styles>"
    '<style:style style:name="T1" style:family="text"><style:text-properties fo:font-weight="bold"/></style:style>'
    '<style:style style:name="T2" style:family="text"><style:text-properties fo:font-style="italic"/></style:style>'
    "</office:automatic-styles>"
    "<office:body><office:text>"
    "<text:tracked-changes>"
    '<text:changed-region text:id="ct-fmt"><text:format-change><office:change-info>'
    "<dc:creator>Ada</dc:creator><dc:date>2024-01-02T03:04:05</dc:date>"
    "</office:change-info></text:format-change></text:changed-region>"
    '<text:changed-region text:id="ct-del"><text:deletion><office:change-info>'
    "<dc:creator>Ada</dc:creator><dc:date>2024-01-02T03:04:06</dc:date>"
    "</office:change-info>"
    '<text:p text:style-name="Standard"><text:span text:style-name="T1">B</text:span></text:p>'
    "</text:deletion></text:changed-region>"
    '<text:changed-region text:id="ct-ins"><text:insertion><office:change-info>'
    "<dc:creator>Ada</dc:creator><dc:date>2024-01-02T03:04:07</dc:date>"
    "</office:change-info></text:insertion></text:changed-region>"
    "</text:tracked-changes>"
    '<text:p text:style-name="Standard">'
    '<text:change-start text:change-id="ct-fmt"/><text:span text:style-name="T2">A</text:span>'
    '<text:change-end text:change-id="ct-fmt"/>'
    '<text:change text:change-id="ct-del"/>'
    '<text:change-start text:change-id="ct-ins"/>C<text:change-end text:change-id="ct-ins"/>'
    "</text:p>"
    "</office:text></office:body></office:document-content>"
)

DOCX_DOCUMENT_XML = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"
 xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
<w:body>
<w:p>
<w:r>
<w:rPr><w:i/><w:rPrChange w:id="1" w:author="Ada" w:date="2024-01-02T03:04:05Z"><w:rPr/></w:rPrChange></w:rPr>
<w:t>A</w:t>
</w:r>
<w:del w:id="2" w:author="Ada" w:date="2024-01-02T03:04:06Z">
<w:r><w:rPr><w:b/></w:rPr><w:delText>B</w:delText></w:r>
</w:del>
<w:ins w:id="3" w:author="Ada" w:date="2024-01-02T03:04:07Z">
<w:r><w:t>C</w:t></w:r>
</w:ins>
</w:p>
<w:p>
<w:r><w:t>He</w:t></w:r>
<w:del w:id="4" w:author="Bob" w:date="2024-01-02T04:00:00Z">
<w:r><w:rPr><w:b/></w:rPr><w:delText>l</w:delText></w:r>
<w:r><w:delText>l</w:delText></w:r>
</w:del>
<w:r><w:t>o</w:t></w:r>
</w:p>
<w:p>
<w:r>
<w:rPr><w:rPrChange w:id="5" w:author="Cara" w:date="2024-01-02T05:00:00Z"><w:rPr><w:b/></w:rPr></w:rPrChange></w:rPr>
<w:t>P</w:t>
</w:r>
<w:r><w:t>lain</w:t></w:r>
</w:p>
<w:sectPr><w:pgSz w:w="12240" w:h="15840"/><w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440"/></w:sectPr>
</w:body>
</w:document>
"""

CONTENT_TYPES = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
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

CORE_PROPS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties"
 xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:title>Wave17 foreign revision fixture</dc:title>
</cp:coreProperties>
"""

ODT_MIMETYPE = "application/vnd.oasis.opendocument.text"
ODT_MANIFEST = """<?xml version="1.0" encoding="UTF-8"?>
<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0">
<manifest:file-entry manifest:media-type="application/vnd.oasis.opendocument.text" manifest:full-path="/"/>
<manifest:file-entry manifest:media-type="text/xml" manifest:full-path="content.xml"/>
</manifest:manifest>
"""


def write_docx(path: Path) -> None:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", CONTENT_TYPES)
        z.writestr("_rels/.rels", RELS_ROOT)
        z.writestr("word/document.xml", DOCX_DOCUMENT_XML)
        z.writestr("word/_rels/document.xml.rels", DOC_RELS)
        z.writestr("docProps/core.xml", CORE_PROPS)
    path.write_bytes(buf.getvalue())


# Hand-written foreign fields: IF (comparison), SEQ, NUMPAGES, plus the other D5 instructions.
# No word/aioffice.json. fldSimple stays on one line so cached text has no extra whitespace.
DOCX_FIELDS_XML = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:body>
<w:p><w:r><w:t xml:space="preserve">See </w:t></w:r><w:fldSimple w:instr=" IF page &gt; 1 &quot;Later&quot; &quot;First&quot; "><w:r><w:t>First</w:t></w:r></w:fldSimple></w:p>
<w:p><w:fldSimple w:instr=" IF &quot;a&quot; = &quot;a&quot; &quot;SIM&quot; &quot;NAO&quot; "><w:r><w:t>SIM</w:t></w:r></w:fldSimple></w:p>
<w:p><w:fldSimple w:instr=" IF 1 &lt;&gt; 2 &quot;T&quot; &quot;F&quot; "><w:r><w:t>T</w:t></w:r></w:fldSimple></w:p>
<w:p><w:fldSimple w:instr=" SEQ Figura \\* ARABIC "><w:r><w:t>1</w:t></w:r></w:fldSimple></w:p>
<w:p><w:fldSimple w:instr=" NUMPAGES "><w:r><w:t>2</w:t></w:r></w:fldSimple></w:p>
<w:p><w:fldSimple w:instr=" NUMWORDS "><w:r><w:t>9</w:t></w:r></w:fldSimple></w:p>
<w:p><w:fldSimple w:instr=" NUMCHARS "><w:r><w:t>4</w:t></w:r></w:fldSimple></w:p>
<w:p><w:fldSimple w:instr=" DOCPROPERTY &quot;Subject&quot; "><w:r><w:t>Tema</w:t></w:r></w:fldSimple></w:p>
<w:p><w:fldSimple w:instr=" DOCPROPERTY Keywords "><w:r><w:t>alfa</w:t></w:r></w:fldSimple></w:p>
<w:p><w:fldSimple w:instr=" CREATEDATE "><w:r><w:t>2020-01-01T00:00:00</w:t></w:r></w:fldSimple></w:p>
<w:p><w:fldSimple w:instr=" SAVEDATE "><w:r><w:t>2020-01-02T00:00:00</w:t></w:r></w:fldSimple></w:p>
<w:p><w:fldSimple w:instr=" REVNUM "><w:r><w:t>4</w:t></w:r></w:fldSimple></w:p>
<w:p><w:pPr><w:rPr><w:vanish/></w:rPr></w:pPr><w:r><w:rPr><w:vanish/></w:rPr><w:t>Secret</w:t></w:r></w:p>
<w:p><w:r><w:rPr><w:vanish/></w:rPr><w:t>HiddenRun</w:t></w:r></w:p>
<w:p><w:r><w:rPr><w:vanish/></w:rPr><w:t>H</w:t></w:r><w:r><w:t>V</w:t></w:r></w:p>
<w:sectPr><w:pgSz w:w="12240" w:h="15840"/><w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440"/></w:sectPr>
</w:body>
</w:document>
"""


def write_fields_docx(path: Path) -> None:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", CONTENT_TYPES)
        z.writestr("_rels/.rels", RELS_ROOT)
        z.writestr("word/document.xml", DOCX_FIELDS_XML)
        z.writestr("word/_rels/document.xml.rels", DOC_RELS)
        z.writestr("docProps/core.xml", CORE_PROPS)
    path.write_bytes(buf.getvalue())


def write_odt(path: Path) -> None:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("mimetype", ODT_MIMETYPE, compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/manifest.xml", ODT_MANIFEST)
        z.writestr("content.xml", ODT_CONTENT_XML)
    path.write_bytes(buf.getvalue())


# Hand-built ODT fields (wave 18 D6). No aioffice JSON. One paragraph per field,
# with no whitespace text nodes inside the paragraph.
ODT_FIELDS_CONTENT_XML = (
    '<?xml version="1.0" encoding="UTF-8"?>'
    '<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
    'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" '
    'xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" '
    'xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" '
    'xmlns:ooow="http://openoffice.org/2004/writer" office:version="1.3">'
    "<office:body><office:text>"
    "<text:user-field-decls>"
    '<text:user-field-decl office:value-type="string" office:string-value="go" text:name="status"/>'
    "</text:user-field-decls>"
    "<text:sequence-decls>"
    '<text:sequence-decl text:display-outline-level="0" text:name="Figura"/>'
    '<text:sequence-decl text:display-outline-level="0" text:name="Letra"/>'
    "</text:sequence-decls>"
    '<text:p text:style-name="Standard"><text:conditional-text text:condition="ooow:status = &quot;go&quot;" '
    'text:string-value-if-true="YES" text:string-value-if-false="NO">YES</text:conditional-text></text:p>'
    '<text:p text:style-name="Standard"><text:sequence text:name="Figura" text:formula="ooow:Figura+1" '
    'style:num-format="1">1</text:sequence></text:p>'
    '<text:p text:style-name="Standard"><text:sequence text:name="Letra" text:formula="ooow:Letra+1" '
    'style:num-format="a">a</text:sequence></text:p>'
    '<text:p text:style-name="Standard"><text:page-count>2</text:page-count></text:p>'
    '<text:p text:style-name="Standard"><text:word-count>3</text:word-count></text:p>'
    '<text:p text:style-name="Standard"><text:character-count>4</text:character-count></text:p>'
    '<text:p text:style-name="Standard"><text:paragraph-count>5</text:paragraph-count></text:p>'
    '<text:p text:style-name="Standard"><text:table-count>6</text:table-count></text:p>'
    '<text:p text:style-name="Standard"><text:image-count>7</text:image-count></text:p>'
    '<text:p text:style-name="Standard"><text:subject>Subj</text:subject></text:p>'
    '<text:p text:style-name="Standard"><text:keywords>k1</text:keywords></text:p>'
    '<text:p text:style-name="Standard"><text:creation-date>2020-01-02T03:04:05</text:creation-date></text:p>'
    '<text:p text:style-name="Standard"><text:modification-date>2021-02-03T04:05:06</text:modification-date></text:p>'
    '<text:p text:style-name="Standard"><text:editing-cycles>9</text:editing-cycles></text:p>'
    '<text:p text:style-name="Standard"><text:sender-firstname>Ada</text:sender-firstname></text:p>'
    '<text:p text:style-name="Standard"><text:sender-email>ada@example.com</text:sender-email></text:p>'
    '<text:p text:style-name="Standard"><text:sender-company>Analytical</text:sender-company></text:p>'
    '<text:p text:style-name="Standard"><text:sender-phone-work>555</text:sender-phone-work></text:p>'
    '<text:p text:style-name="Standard"><text:hidden-paragraph text:condition="ooow:status = &quot;go&quot;" '
    'text:is-hidden="true"/>Secret</text:p>'
    '<text:p text:style-name="Standard"><text:drop-down text:name="List1">'
    '<text:label text:value="Um"/>'
    '<text:label text:current-selected="true" text:value="Dois"/>'
    "</text:drop-down></text:p>"
    "</office:text></office:body></office:document-content>"
)


def write_fields_odt(path: Path) -> None:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("mimetype", ODT_MIMETYPE, compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/manifest.xml", ODT_MANIFEST)
        z.writestr("content.xml", ODT_FIELDS_CONTENT_XML)
    path.write_bytes(buf.getvalue())


# Wave 22: foreign hide_if that hideIfExprError rejects must be dropped on import.
# ooow:Page (capital P) is the identifier Page, which the checker accepts.
# ooow:page and an unparseable condition are dropped. A valid flag and w:vanish "1" stay.
ODT_HIDE_IMPORT_XML = (
    '<?xml version="1.0" encoding="UTF-8"?>'
    '<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
    'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" '
    'xmlns:ooow="http://openoffice.org/2004/writer" office:version="1.3">'
    "<office:body><office:text>"
    '<text:p text:style-name="Standard"><text:hidden-paragraph text:condition="ooow:Page &gt; 1"/>Pagina</text:p>'
    '<text:p text:style-name="Standard"><text:hidden-paragraph text:condition="ooow:page &gt; 1"/>Minuscula</text:p>'
    '<text:p text:style-name="Standard"><text:hidden-paragraph text:condition="ooow:???"/>Ruim</text:p>'
    '<text:p text:style-name="Standard"><text:hidden-paragraph text:condition="ooow:flag = &quot;on&quot;"/>Valido</text:p>'
    "</office:text></office:body></office:document-content>"
)


def write_hide_import_odt(path: Path) -> None:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("mimetype", ODT_MIMETYPE, compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/manifest.xml", ODT_MANIFEST)
        z.writestr("content.xml", ODT_HIDE_IMPORT_XML)
    path.write_bytes(buf.getvalue())


def write_hide_import_docx(path: Path) -> None:
    paras = [
        ("DocPage", "page > 1"),
        ("DocBad", "???"),
        ("DocOk", 'flag == "on"'),
    ]
    body = "".join(f"<w:p><w:r><w:t>{text}</w:t></w:r></w:p>" for text, _expr in paras)
    body += (
        "<w:p><w:pPr><w:rPr><w:vanish/></w:rPr></w:pPr>"
        "<w:r><w:rPr><w:vanish/></w:rPr><w:t>Secret</w:t></w:r></w:p>"
    )
    document = (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body>'
        + body
        + "<w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/></w:sectPr></w:body></w:document>"
    )
    conds = []
    for text, expr in paras:
        conds.append(
            {
                "type": "paragraph",
                "fp": hashlib.sha256(text.encode()).hexdigest(),
                "n": 1,
                "hide_if": expr,
            }
        )
    extra = json.dumps({"conditionals": conds}, separators=(",", ":"))
    ctypes = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Default Extension="json" ContentType="application/json"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
<Override PartName="/word/aioffice.json" ContentType="application/json"/>
</Types>
"""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", ctypes)
        z.writestr("_rels/.rels", RELS_ROOT)
        z.writestr("word/document.xml", document)
        z.writestr("word/_rels/document.xml.rels", DOC_RELS)
        z.writestr("word/aioffice.json", extra)
    path.write_bytes(buf.getvalue())


def main() -> None:
    write_odt(ROOT / "wave17-odt-revisions-foreign.odt")
    write_docx(ROOT / "wave17-docx-revisions-foreign.docx")
    write_fields_docx(ROOT / "wave18-docx-fields-foreign.docx")
    write_fields_odt(ROOT / "wave18-odt-fields-foreign.odt")
    write_hide_import_odt(ROOT / "wave22-hide-if-import.odt")
    write_hide_import_docx(ROOT / "wave22-hide-if-import.docx")
    print("wrote", ROOT / "wave17-odt-revisions-foreign.odt")
    print("wrote", ROOT / "wave17-docx-revisions-foreign.docx")
    print("wrote", ROOT / "wave18-docx-fields-foreign.docx")
    print("wrote", ROOT / "wave18-odt-fields-foreign.odt")


if __name__ == "__main__":
    main()
