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


# The underdeclared playbook lowers the unpacker to these caps (CMake
# ENVIRONMENT on playbook_wave23-io-zip-underdeclared.json). Both stay far
# below the built-in 256 MiB / 1 GiB ceilings. Three `chunk` entries sum past
# the total; one entry of entry_cap + 50 KiB exceeds only the per-entry cap.
ZIP_ENTRY_CAP = 200_000
ZIP_TOTAL_CAP = 300_000
ZIP_UNDERDECLARED_CHUNK = 120_000


def zip_entries(entries: list[tuple[str, bytes, int]]) -> bytes:
    """ZIP with method 8. `declared` is the central-directory uncompressed size."""
    local = bytearray()
    recs: list[tuple[bytes, int, int, int, int, int]] = []
    for name, payload, declared in entries:
        comp = raw_deflate(payload)
        crc = zlib.crc32(payload) & 0xFFFFFFFF
        name_b = name.encode("utf-8")
        method = 8
        offset = len(local)
        local += b"PK\x03\x04" + struct.pack(
            "<HHHHHIIIHH",
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
        )
        local += name_b + comp
        recs.append((name_b, crc, len(comp), declared & 0xFFFFFFFF, method, offset))
    central = bytearray()
    for name_b, crc, comp_len, declared, method, offset in recs:
        central += b"PK\x01\x02" + struct.pack(
            "<HHHHHHIIIHHHHHII",
            0x0314,
            20,
            0,
            method,
            0,
            0,
            crc,
            comp_len,
            declared,
            len(name_b),
            0,
            0,
            0,
            0,
            0,
            offset,
        )
        central += name_b
    eocd = b"PK\x05\x06" + struct.pack(
        "<HHHHIIH", 0, 0, len(recs), len(recs), len(central), len(local), 0
    )
    return bytes(local) + bytes(central) + eocd


def zip_lying_uncompressed(name: str, payload: bytes, declared: int) -> bytes:
    return zip_entries([(name, payload, declared)])


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
    chunk = b"\0" * ZIP_UNDERDECLARED_CHUNK
    (dest / "underdeclared.docx").write_bytes(
        zip_entries(
            [
                ("a/1.bin", chunk, 0),
                ("a/2.bin", chunk, 0),
                ("a/3.bin", chunk, 0),
            ]
        )
    )
    (dest / "underdeclared-entry.docx").write_bytes(
        zip_entries([("word/huge.bin", b"\0" * (ZIP_ENTRY_CAP + 50_000), 0)])
    )
    (dest / "underdeclared-small.docx").write_bytes(zip_entries([("pad.bin", b"\0" * 1000, 0)]))
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
