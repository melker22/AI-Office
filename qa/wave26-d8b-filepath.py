#!/usr/bin/env python3
"""Wave 26 D8b: filepath is Word/RTF FILENAME \\p and ODT display=full.

A round trip of the display string is not enough: the native instruction is
checked in the file bytes, and hand-written files are opened as well.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parent.parent
TIMEOUT = 120


def child_env() -> dict[str, str]:
    env = os.environ.copy()
    env.pop("LD_PRELOAD", None)
    env.pop("LD_LIBRARY_PATH", None)
    env["AIOFFICE_HEADLESS"] = "1"
    env["QT_QPA_PLATFORM"] = "offscreen"
    return env


def find_cli(explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit)
        if not path.is_file():
            raise SystemExit(f"cli not found: {path}")
        return path
    env = os.environ.get("AIOFFICE_CLI")
    if env and Path(env).is_file():
        return Path(env)
    candidate = REPO / "build" / "linux" / "aioffice-cli"
    if candidate.is_file():
        return candidate
    raise SystemExit("aioffice-cli not found (pass --cli)")


def play(cli: Path, steps: list[dict[str, Any]], folder: Path) -> list[dict[str, Any]]:
    playbook = folder / "play.json"
    playbook.write_text(json.dumps(steps), encoding="utf-8")
    try:
        proc = subprocess.run(
            [str(cli), "play", str(playbook)],
            cwd=str(REPO),
            env=child_env(),
            capture_output=True,
            text=True,
            timeout=TIMEOUT,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise SystemExit(f"timeout after {TIMEOUT}s") from exc
    raw = proc.stdout.strip()
    if not raw:
        raise SystemExit(f"play failed ({proc.returncode}): {proc.stderr.strip() or 'empty stdout'}")
    try:
        out = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"play bad JSON: {exc}\n{raw[:400]}") from exc
    if not out.get("ok"):
        raise SystemExit(f"play: {out.get('error', out)}")
    results = out.get("results") or []
    if len(results) != len(steps):
        raise SystemExit(f"play result count {len(results)} != {len(steps)}")
    return results


def runs_of(result: dict[str, Any]) -> list[dict[str, Any]]:
    doc = result.get("document") or {}
    body = doc.get("body") or []
    if not body:
        raise SystemExit(f"opened document has no body: {result}")
    return body[0].get("runs") or []


def fields_of(result: dict[str, Any]) -> list[str]:
    # Importers keep whitespace-only runs between fields. Judge the fields.
    return [str(r.get("field") or "") for r in runs_of(result) if r.get("field")]


DOCX_XML = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:body>
    <w:p>
      <w:fldSimple w:instr=" FILENAME \\p ">
        <w:r><w:t>full</w:t></w:r>
      </w:fldSimple>
      <w:fldSimple w:instr=" FILENAME ">
        <w:r><w:t>name</w:t></w:r>
      </w:fldSimple>
    </w:p>
  </w:body>
</w:document>
"""

ODT_XML = """<?xml version="1.0" encoding="UTF-8"?>
<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0">
  <office:body>
    <office:text>
      <text:p>
        <text:file-name text:display="full">full</text:file-name>
        <text:file-name>name</text:file-name>
      </text:p>
    </office:text>
  </office:body>
</office:document-content>
"""

# \\\\ in the RTF source is one escaped backslash, so the reader sees \\p.
RTF = r"{\rtf1\ansi\pard{\field{\*\fldinst FILENAME \\p}{\fldrslt full}}{\field{\*\fldinst FILENAME }{\fldrslt name}}\par}"


def write_zip(path: Path, name: str, payload: str) -> None:
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        zf.writestr(name, payload.encode("utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", default=None)
    args = parser.parse_args()
    cli = find_cli(args.cli)
    errors: list[str] = []

    with tempfile.TemporaryDirectory(prefix="wave26-d8b-") as tmp_s:
        tmp = Path(tmp_s)
        docx = tmp / "out.docx"
        odt = tmp / "out.odt"
        rtf = tmp / "out.rtf"
        setup: list[dict[str, Any]] = [
            {"tool": "document_new", "args": {"title": "Wave26 D8b", "force": True}},
            {
                "tool": "replace_range",
                "args": {
                    "ids": ["p1"],
                    "blocks": [
                        {
                            "id": "p1",
                            "type": "paragraph",
                            "runs": [
                                {"field": "filename", "text": "name"},
                                {"field": "filepath", "text": "full"},
                            ],
                        }
                    ],
                },
            },
            {"tool": "document_save", "args": {"path": str(docx), "embed_json": False}},
            {"tool": "document_save", "args": {"path": str(odt), "embed_json": False}},
            {"tool": "document_save", "args": {"path": str(rtf), "embed_json": False}},
        ]
        play(cli, setup, tmp)
        with zipfile.ZipFile(docx) as zf:
            doc_xml = zf.read("word/document.xml").decode("utf-8", errors="replace")
        with zipfile.ZipFile(odt) as zf:
            content = zf.read("content.xml").decode("utf-8", errors="replace")
        rtf_text = rtf.read_text(encoding="utf-8", errors="replace")

        if "FILENAME \\p" not in doc_xml:
            errors.append("docx export: missing FILENAME \\p instruction")
        if not re.search(r"FILENAME(?!\s*\\p)", doc_xml):
            errors.append("docx export: filename field lost its bare FILENAME instruction")
        if 'w:instr="filepath"' in doc_xml or "instr=\"filepath\"" in doc_xml:
            errors.append("docx export: raw filepath instruction")
        if 'text:file-name text:display="full"' not in content:
            errors.append('odt export: missing text:file-name text:display="full"')
        if content.count("<text:file-name") < 2:
            errors.append("odt export: expected a file-name for filename and one for filepath")
        full_displays = content.count('text:display="full"')
        if full_displays != 1:
            errors.append(f"odt export: display=full count {full_displays} != 1")
        # Writer escapes the switch, so the file contains FILENAME \\p.
        if "FILENAME \\\\p" not in rtf_text:
            errors.append("rtf export: missing FILENAME \\\\p instruction")
        bare = re.findall(r"FILENAME(?! \\\\)", rtf_text)
        if not bare:
            errors.append("rtf export: filename field lost its bare FILENAME instruction")

        def opened_fields(path: Path) -> list[str]:
            got = play(
                cli,
                [
                    {"tool": "document_open", "args": {"path": str(path)}},
                    {"tool": "document_get", "args": {}},
                ],
                tmp,
            )
            return fields_of(got[-1])

        exported = {
            "docx roundtrip": opened_fields(docx),
            "odt roundtrip": opened_fields(odt),
            "rtf roundtrip": opened_fields(rtf),
        }
        for label, got in exported.items():
            if got[:2] != ["filename", "filepath"]:
                errors.append(f"{label}: fields {got[:2]} != ['filename', 'filepath']")

        hand_docx = tmp / "hand.docx"
        hand_odt = tmp / "hand.odt"
        hand_rtf = tmp / "hand.rtf"
        write_zip(hand_docx, "word/document.xml", DOCX_XML)
        write_zip(hand_odt, "content.xml", ODT_XML)
        hand_rtf.write_text(RTF, encoding="utf-8")
        handwritten = {
            "docx import": opened_fields(hand_docx),
            "odt import": opened_fields(hand_odt),
            "rtf import": opened_fields(hand_rtf),
        }
        for label, got in handwritten.items():
            if got[:2] != ["filepath", "filename"]:
                errors.append(f"{label}: fields {got[:2]} != ['filepath', 'filename']")

    if errors:
        print("wave26 D8b FAIL", file=sys.stderr)
        for err in errors:
            print(f"  {err}", file=sys.stderr)
        return 1
    print("wave26 D8b ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
