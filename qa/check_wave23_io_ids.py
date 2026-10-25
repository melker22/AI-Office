#!/usr/bin/env python3
"""EPUB sanitized-id suffixes, DOCX bookmark w:id uniqueness, XML error shape."""

from __future__ import annotations

import json
import re
import subprocess
import sys
import zipfile
from pathlib import Path


def call(cli: str, tool: str, args: dict) -> dict:
    proc = subprocess.run(
        [cli, "call", tool, json.dumps(args)],
        check=False,
        capture_output=True,
        text=True,
    )
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError:
        return {"ok": False, "error": proc.stderr or proc.stdout or "no json"}


def play(cli: str, steps: list) -> dict:
    # One process keeps the session across steps.
    import tempfile

    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as fh:
        json.dump(steps, fh)
        path = fh.name
    proc = subprocess.run([cli, "play", path], check=False, capture_output=True, text=True)
    Path(path).unlink(missing_ok=True)
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError:
        return {"ok": False, "error": proc.stderr or proc.stdout or "no json"}


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_wave23_io_ids.py CLI FIXTURE", file=sys.stderr)
        return 2
    cli, fixture = sys.argv[1], Path(sys.argv[2])
    if not Path(cli).is_file():
        print(f"FAIL cli missing: {cli}")
        return 1
    errors: list[str] = []

    for name, prefix in (
        ("truncated.docx", "document.xml:"),
        ("truncated.odt", "content.xml:"),
    ):
        got = call(cli, "document_open", {"path": str(fixture / name)})
        err = str(got.get("error", ""))
        if got.get("ok") is not False or not err.startswith(prefix) or " at line " not in err:
            errors.append(f"{name}: want {prefix} … at line N, got {got}")

    epub_path = fixture / "epub-ids.epub"
    played = play(
        cli,
        [
            {"tool": "document_new", "args": {"force": True}},
            {"tool": "document_open", "args": {"path": str(fixture / "epub-ids.aio.json")}},
            {"tool": "document_save", "args": {"path": str(epub_path)}},
        ],
    )
    if not played.get("ok"):
        errors.append(f"epub id save failed: {played.get('error')}")
    else:
        with zipfile.ZipFile(epub_path) as zf:
            xhtml = zf.read("OEBPS/c1.xhtml").decode("utf-8")
        ids = re.findall(r'\bid="([^"]+)"', xhtml)
        if "a-b" not in ids or "a-b-2" not in ids or ids.count("a-b") != 1:
            errors.append(f"epub ids collided or missing suffix: {ids}")

    docx_path = fixture / "bookmark-ids.docx"
    played = play(
        cli,
        [
            {"tool": "document_new", "args": {"force": True}},
            {"tool": "insert_at_caret", "args": {"text": "one"}},
            {
                "tool": "insert_blocks",
                "args": {"after_id": "end", "blocks": [{"type": "paragraph", "text": "two"}]},
            },
            {"tool": "bookmark_add", "args": {"name": "First", "id": "p1"}},
            {"tool": "bookmark_add", "args": {"name": "Second", "id": "p2"}},
            {"tool": "document_save", "args": {"path": str(docx_path)}},
        ],
    )
    if not played.get("ok"):
        errors.append(f"bookmark save failed: {played.get('error')}")
    else:
        with zipfile.ZipFile(docx_path) as zf:
            xml = zf.read("word/document.xml").decode("utf-8")
        bm = re.findall(r'<w:bookmarkStart w:id="(\d+)"', xml)
        if len(bm) != 2 or len(set(bm)) != 2:
            errors.append(f"bookmark w:id not unique: {bm}")

    if errors:
        for err in errors:
            print(f"FAIL {err}")
        return 1
    print("ok   wave23 io ids and xml errors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
