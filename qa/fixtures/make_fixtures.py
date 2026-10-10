#!/usr/bin/env python3
"""Build minimal foreign DOCX/ODT revision fixtures from literal XML (reviewable, no aioffice.json)."""

from __future__ import annotations

import io
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


def write_odt(path: Path) -> None:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("mimetype", ODT_MIMETYPE, compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/manifest.xml", ODT_MANIFEST)
        z.writestr("content.xml", ODT_CONTENT_XML)
    path.write_bytes(buf.getvalue())


def main() -> None:
    write_odt(ROOT / "wave17-odt-revisions-foreign.odt")
    write_docx(ROOT / "wave17-docx-revisions-foreign.docx")
    print("wrote", ROOT / "wave17-odt-revisions-foreign.odt")
    print("wrote", ROOT / "wave17-docx-revisions-foreign.docx")


if __name__ == "__main__":
    main()
