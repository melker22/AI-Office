#!/usr/bin/env python3
"""document_save prose: the MCP schema and docs/mcp-tools.md name .doc as an export.

The check reads the document_save section of each file. It fails when either
one still lists the export extensions without .doc, or omits LibreOffice.
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def slice_between(text: str, start: str, end: str) -> str:
    i = text.find(start)
    if i < 0:
        return ""
    j = text.find(end, i + len(start))
    if j < 0:
        return text[i:]
    return text[i:j]


def main() -> int:
    dispatch = (REPO / "src/mcp/ToolDispatch.cpp").read_text(encoding="utf-8")
    docs = (REPO / "docs/mcp-tools.md").read_text(encoding="utf-8")
    regions = {
        "ToolDispatch": slice_between(
            dispatch,
            'QStringLiteral("document_save")',
            'QStringLiteral("document_export_pdf")',
        ),
        "docs/mcp-tools.md": slice_between(
            docs,
            "### `document_save`",
            "### `document_export_pdf`",
        ),
    }
    list_forms = (
        ".rtf, and .doc are exports",
        ".rtf`, and `.doc` are exports",
    )
    errors: list[str] = []
    for label, region in regions.items():
        if not region:
            errors.append(f"{label}: document_save section not found")
            continue
        for needle in (".doc", "LibreOffice", "export-only", "Word 97"):
            if needle not in region:
                errors.append(f"{label}: document_save does not mention {needle!r}")
        if not any(form in region for form in list_forms):
            errors.append(f"{label}: export list does not include .doc")
    if errors:
        for err in errors:
            print(f"FAIL {err}")
        return 1
    print("ok   document_save .doc export prose")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
