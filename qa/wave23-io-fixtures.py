#!/usr/bin/env python3
"""Hostile fixtures for wave 23 I/O playbooks. Never allocates the declared zip size."""

from __future__ import annotations

import json
import struct
import sys
import zlib
import zipfile
from pathlib import Path


def raw_deflate(data: bytes) -> bytes:
    comp = zlib.compressobj(wbits=-15)
    return comp.compress(data) + comp.flush()


def zip_lying_uncompressed(name: str, payload: bytes, declared: int) -> bytes:
    comp = raw_deflate(payload)
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    name_b = name.encode("utf-8")
    method = 8
    local = b"PK\x03\x04" + struct.pack(
        "<HHHHHIIIHH", 20, 0, method, 0, 0, crc, len(comp), declared & 0xFFFFFFFF, len(name_b), 0
    )
    local += name_b + comp
    central = b"PK\x01\x02" + struct.pack(
        "<HHHHHHIIIHHHHHII",
        0x0314,
        20,
        0,
        method,
        0,
        0,
        crc,
        len(comp),
        declared & 0xFFFFFFFF,
        len(name_b),
        0,
        0,
        0,
        0,
        0,
        0,
    )
    central += name_b
    eocd = b"PK\x05\x06" + struct.pack("<HHHHIIH", 0, 0, 1, 1, len(central), len(local), 0)
    return local + central + eocd


ODF_CONTENT_OPEN = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
    'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" '
    'xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" '
    'office:version="1.3">'
    "<office:body><office:text>"
)


def zip_stored(path: Path, entries: dict[str, str]) -> None:
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for name, text in entries.items():
            zf.writestr(name, text)


def main() -> int:
    dest = Path(sys.argv[1])
    declared = 256 * 1024 * 1024 + 1
    (dest / "bomb.docx").write_bytes(zip_lying_uncompressed("word/document.xml", b"x", declared))
    zip_stored(
        dest / "truncated.docx",
        {
            "word/document.xml": (
                '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
                '<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">'
                "<w:body><w:p><w:r><w:t>Hello</w:t></w:r></w:p><w:p><w:r><w:t>Trun"
            )
        },
    )
    zip_stored(
        dest / "truncated.odt",
        {
            "content.xml": (
                ODF_CONTENT_OPEN + "<text:p>Hello</text:p><text:p>Trun"
            )
        },
    )
    zip_stored(
        dest / "spaces.odt",
        {
            "content.xml": (
                ODF_CONTENT_OPEN
                + '<text:p>Hi<text:s text:c="2000000000"/></text:p>'
                + "</office:text></office:body></office:document-content>"
            )
        },
    )
    (dest / "body-nonobject.aio.json").write_text(
        json.dumps({"schema": 1, "body": [None]}), encoding="utf-8"
    )
    (dest / "duplicate-ids.aio.json").write_text(
        json.dumps(
            {
                "schema": 1,
                "body": [
                    {"id": "p1", "type": "paragraph", "text": "a"},
                    {"id": "p1", "type": "paragraph", "text": "b"},
                ],
            }
        ),
        encoding="utf-8",
    )
    (dest / "assign-ids.aio.json").write_text(
        json.dumps(
            {
                "schema": 1,
                "body": [
                    {"type": "paragraph", "text": "first"},
                    {"id": "p1", "type": "paragraph", "text": "second"},
                ],
            }
        ),
        encoding="utf-8",
    )
    (dest / "table-caption.aio.json").write_text(
        json.dumps(
            {
                "schema": 1,
                "body": [
                    {"id": "p1", "type": "paragraph", "text": "x"},
                    {
                        "id": "t1",
                        "type": "table",
                        "caption": "Vendas",
                        "rows": [[{"text": "a"}]],
                    },
                ],
            }
        ),
        encoding="utf-8",
    )
    (dest / "image-fifo.aio.json").write_text(
        json.dumps(
            {
                "schema": 1,
                "body": [
                    {"id": "p1", "type": "paragraph", "text": "x"},
                    {"id": "img1", "type": "image", "path": "image.fifo", "alt": "fifo"},
                ],
            }
        ),
        encoding="utf-8",
    )
    (dest / "image-device.aio.json").write_text(
        json.dumps(
            {
                "schema": 1,
                "body": [
                    {"id": "p1", "type": "paragraph", "text": "x"},
                    {"id": "img1", "type": "image", "path": "/dev/zero", "alt": "device"},
                ],
            }
        ),
        encoding="utf-8",
    )
    (dest / "epub-ids.aio.json").write_text(
        json.dumps(
            {
                "schema": 1,
                "meta": {"title": "Ids"},
                "body": [
                    {"id": "a b", "type": "paragraph", "text": "one"},
                    {"id": "a-b", "type": "paragraph", "text": "two"},
                ],
            }
        ),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
