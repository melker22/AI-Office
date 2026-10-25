#!/usr/bin/env python3
"""Wave 24 DOCX export (D4, D5, D7, D9, D11).

Builds a document with aioffice-cli, reads word/document.xml, and reopens it
with the current importer where that importer can see the result. With
--interop, LibreOffice must open the DOCX and keep the table structure.

The checker does not use a fixed /tmp path, a privilege, or LD_PRELOAD.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
W_NS = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
T_NS = "urn:oasis:names:tc:opendocument:xmlns:table:1.0"
C0_FORBIDDEN = bytes(b for b in range(0x20) if b not in (0x09, 0x0A, 0x0D))


def w(tag: str) -> str:
    return f"{{{W_NS}}}{tag}"


def t(tag: str) -> str:
    return f"{{{T_NS}}}{tag}"


def aval(el: ET.Element, name: str) -> str:
    return el.attrib.get(f"{{{W_NS}}}{name}", "")


def play(cli: Path, calls: list[dict], work: Path) -> tuple[dict | None, str]:
    path = work / "playbook.json"
    path.write_text(json.dumps(calls), encoding="utf-8")
    env = os.environ.copy()
    env["AIOFFICE_HEADLESS"] = "1"
    env["QT_QPA_PLATFORM"] = "offscreen"
    env.pop("LD_PRELOAD", None)
    try:
        proc = subprocess.run(
            [str(cli), "play", str(path)],
            cwd=ROOT,
            env=env,
            capture_output=True,
            text=True,
            timeout=180,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return None, "aioffice-cli play timed out after 180s"
    raw = proc.stdout
    start = raw.find("{")
    if proc.returncode != 0 or start < 0:
        detail = (proc.stdout + proc.stderr).strip()
        return None, detail[:2000] or f"play exit {proc.returncode}"
    try:
        data = json.loads(raw[start:])
    except json.JSONDecodeError as exc:
        return None, f"play bad JSON: {exc}"
    if not data.get("ok"):
        return None, str(data.get("error") or data)[:2000]
    return data, ""


def read_docx(path: Path) -> tuple[bytes, bytes]:
    if not path.is_file() or path.stat().st_size < 64:
        raise RuntimeError(f"{path.name} was not written")
    with zipfile.ZipFile(path) as zf:
        xml = zf.read("word/document.xml")
        extra = zf.read("word/aioffice.json") if "word/aioffice.json" in zf.namelist() else b""
    if not xml:
        raise RuntimeError(f"{path.name} has an empty word/document.xml")
    return xml, extra


def parse_xml(blob: bytes) -> ET.Element:
    return ET.fromstring(blob)


def block_text(block: dict) -> str:
    return "".join((run.get("text") or "") for run in (block.get("runs") or []))


def cell_text(cell: dict) -> str:
    text = "".join((run.get("text") or "") for run in (cell.get("runs") or []))
    for row in cell.get("nested") or []:
        if isinstance(row, list):
            for nested in row:
                if isinstance(nested, dict):
                    text += cell_text(nested)
    return text


def body_has_cell(body: list, needle: str) -> bool:
    for block in body:
        if block.get("type") != "table":
            continue
        for row in block.get("rows") or []:
            if not isinstance(row, list):
                continue
            for cell in row:
                if isinstance(cell, dict) and needle in cell_text(cell):
                    return True
    return False


def reopen(cli: Path, docx: Path, work: Path) -> tuple[dict | None, str]:
    data, err = play(
        cli,
        [
            {"tool": "document_open", "args": {"path": str(docx)}},
            {"tool": "document_get", "args": {}},
            {"tool": "revision_list", "args": {}},
        ],
        work,
    )
    if err or data is None:
        return None, err or "open failed"
    results = data.get("results") or []
    if len(results) < 3:
        return None, "open did not return document_get and revision_list"
    return {"document": results[1].get("document") or {}, "revisions": results[2].get("revisions") or []}, ""


def check_tables(cli: Path, work: Path) -> list[str]:
    """D4: gridSpan, vMerge, and a nested w:tbl. Import still sees the span text."""
    errors: list[str] = []
    docx = work / "tables.docx"
    calls = [
        {"tool": "document_new", "args": {"title": "Wave24 tables", "force": True}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {
                        "id": "tSpan",
                        "type": "table",
                        "rows": [
                            [
                                {"text": "SpanH", "col_span": 2},
                                {"text": "COVEREDX"},
                            ]
                        ],
                    },
                    {
                        "id": "tVert",
                        "type": "table",
                        "rows": [
                            [
                                {"text": "Down", "row_span": 2},
                                {"text": "R1"},
                            ],
                            [
                                {"text": "COVEREDV"},
                                {"text": "R2"},
                            ],
                        ],
                    },
                    {
                        "id": "tNest",
                        "type": "table",
                        "rows": [
                            [
                                {
                                    "text": "Host",
                                    "nested": [
                                        [{"text": "Inner"}, {"text": "In2"}],
                                        [{"text": "In3"}, {"text": "In4"}],
                                    ],
                                }
                            ]
                        ],
                    },
                ],
            },
        },
        {"tool": "document_save", "args": {"path": str(docx)}},
    ]
    _data, err = play(cli, calls, work)
    if err:
        return [f"D4 play: {err}"]
    try:
        xml, _extra = read_docx(docx)
    except (RuntimeError, zipfile.BadZipFile, KeyError) as exc:
        return [f"D4 package: {exc}"]
    if b"COVEREDX" in xml:
        errors.append("D4 export still writes the horizontally covered cell COVEREDX")
    if b"COVEREDV" in xml:
        errors.append("D4 export still writes the vertically covered cell COVEREDV")
    try:
        root = parse_xml(xml)
    except ET.ParseError as exc:
        return errors + [f"D4 document.xml is not well-formed: {exc}"]
    span_row = None
    for tr in root.iter(w("tr")):
        if "SpanH" in "".join(tr.itertext()):
            span_row = tr
            break
    if span_row is None:
        errors.append("D4 export has no row containing SpanH")
    else:
        cells = [child for child in list(span_row) if child.tag == w("tc")]
        if len(cells) != 1:
            errors.append(f"D4 SpanH row has {len(cells)} cells; gridSpan should leave one")
        spans = [aval(el, "val") for el in span_row.iter(w("gridSpan"))]
        if "2" not in spans:
            errors.append(f"D4 SpanH row has no w:gridSpan 2 (got {spans})")
    merges = [aval(el, "val") for el in root.iter(w("vMerge"))]
    if "restart" not in merges or "continue" not in merges:
        errors.append(f"D4 vMerge restart/continue missing (got {merges})")
    nested = False
    for tc in root.iter(w("tc")):
        inner = [child for child in list(tc) if child.tag == w("tbl")]
        if inner and "Inner" in "".join(tc.itertext()):
            nested = True
    if not nested:
        errors.append("D4 no w:tc contains a nested w:tbl with Inner")
    opened, open_err = reopen(cli, docx, work)
    if open_err or opened is None:
        errors.append(f"D4 reopen: {open_err}")
    else:
        body = opened["document"].get("body") or []
        if not body_has_cell(body, "SpanH") or not body_has_cell(body, "Down"):
            errors.append("D4 reopen lost SpanH or Down")
    return errors


def check_captions(cli: Path, work: Path) -> list[str]:
    """D5: Caption paragraph with a native SEQ immediately after the table and the image."""
    errors: list[str] = []
    docx = work / "caption.docx"
    calls = [
        {"tool": "document_new", "args": {"title": "Wave24 captions", "force": True}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {
                        "id": "tCap",
                        "type": "table",
                        "rows": [[{"text": "CapCell"}]],
                    },
                    {
                        "id": "img1",
                        "type": "image",
                        "path": "qa/fixtures/wave19-pixel.png",
                        "width_mm": 12,
                    },
                ],
            },
        },
        {
            "tool": "insert_caption",
            "args": {"id": "tCap", "text": "Vendas", "seq": {"name": "Tabela", "format": "arabic"}},
        },
        {"tool": "insert_caption", "args": {"id": "img1", "text": "Foto da capa"}},
        {"tool": "document_save", "args": {"path": str(docx)}},
    ]
    _data, err = play(cli, calls, work)
    if err:
        return [f"D5 play: {err}"]
    try:
        xml, _extra = read_docx(docx)
        root = parse_xml(xml)
    except (RuntimeError, zipfile.BadZipFile, ET.ParseError, KeyError) as exc:
        return [f"D5 package: {exc}"]
    body = root.find(w("body"))
    children = list(body) if body is not None else []
    saw_tbl = False
    caption_after_tbl = False
    for child in children:
        if child.tag == w("tbl"):
            saw_tbl = True
            continue
        if saw_tbl and child.tag == w("p"):
            styles = [aval(el, "val") for el in child.iter(w("pStyle"))]
            instrs = [aval(el, "instr") for el in child.iter(w("fldSimple"))]
            text = "".join(child.itertext())
            if "Caption" in styles and "Vendas" in text and any("SEQ" in i and "Tabela" in i for i in instrs):
                caption_after_tbl = True
            break
    if not caption_after_tbl:
        errors.append("D5 export has no Caption paragraph with SEQ Tabela immediately after the table")
    saw_drawing = False
    foto_after = False
    for child in children:
        if child.tag == w("p") and any(el.tag == w("drawing") or el.tag.endswith("drawing") for el in child.iter()):
            saw_drawing = True
            continue
        if saw_drawing and child.tag == w("p"):
            styles = [aval(el, "val") for el in child.iter(w("pStyle"))]
            text = "".join(child.itertext())
            if "Caption" in styles and "Foto da capa" in text:
                foto_after = True
            break
    if not foto_after:
        errors.append("D5 export has no Caption paragraph immediately after the image")
    opened, open_err = reopen(cli, docx, work)
    if open_err or opened is None:
        errors.append(f"D5 reopen: {open_err}")
        return errors
    blocks = opened["document"].get("body") or []

    def find_block(pred):
        for index, block in enumerate(blocks):
            if pred(block):
                return index, block
        return -1, None

    table_at, _table = find_block(
        lambda b: b.get("type") == "table" and body_has_cell([b], "CapCell")
    )
    cap_at, cap = find_block(lambda b: b.get("style") == "Caption" and "Vendas" in block_text(b))
    if cap is None or table_at < 0 or cap_at != table_at + 1:
        errors.append(
            f"D5 reopen caption is not the paragraph after the table (table {table_at}, caption {cap_at})"
        )
    elif not any(run.get("field") == "seq" and "Tabela" in (run.get("field_arg") or "") for run in cap.get("runs") or []):
        errors.append("D5 reopen Caption paragraph has no SEQ field named Tabela")
    image_at, _image = find_block(lambda b: b.get("type") == "image")
    foto_at, _foto = find_block(lambda b: b.get("style") == "Caption" and "Foto da capa" in block_text(b))
    if image_at < 0 or foto_at != image_at + 1:
        errors.append(
            f"D5 reopen image caption is not the next paragraph (image {image_at}, caption {foto_at})"
        )
    return errors


def check_deleted_field(cli: Path, work: Path) -> list[str]:
    """D7: a field inside a delete is w:delText, and the field stays in the embedded JSON."""
    errors: list[str] = []
    docx = work / "deleted-field.docx"
    calls = [
        {"tool": "document_new", "args": {"title": "Wave24 delete field", "force": True}},
        {"tool": "document_set_title", "args": {"author": "Ada"}},
        {"tool": "insert_at_caret", "args": {"id": "p1", "offset": 0, "text": "Keep"}},
        {"tool": "insert_field", "args": {"id": "p1", "offset": 4, "kind": "page_number"}},
        {"tool": "track_changes", "args": {"on": True}},
        {
            "tool": "set_selection",
            "args": {"anchor_id": "p1", "anchor_off": 4, "focus_id": "p1", "focus_off": 5},
        },
        {"tool": "insert_at_caret", "args": {"id": "p1", "text": ""}},
        {"tool": "document_save", "args": {"path": str(docx), "embed_json": True}},
    ]
    _data, err = play(cli, calls, work)
    if err:
        return [f"D7 play: {err}"]
    try:
        xml, extra = read_docx(docx)
        root = parse_xml(xml)
    except (RuntimeError, zipfile.BadZipFile, ET.ParseError, KeyError) as exc:
        return [f"D7 package: {exc}"]
    deletions = list(root.iter(w("del")))
    if not deletions:
        errors.append("D7 export has no w:del")
    for deletion in deletions:
        if list(deletion.iter(w("fldSimple"))):
            errors.append("D7 export puts w:fldSimple inside w:del")
        shown = "".join((el.text or "") for el in deletion.iter(w("delText")))
        if shown != "1":
            errors.append(f"D7 w:delText is {shown!r}, want the displayed page number '1'")
    try:
        blob = json.loads(extra.decode("utf-8"))
    except json.JSONDecodeError as exc:
        errors.append(f"D7 aioffice.json: {exc}")
        blob = {}
    revs = blob.get("revisions") or []
    deleted = [rev for rev in revs if rev.get("kind") == "delete"]
    if not deleted:
        errors.append("D7 embedded JSON has no delete revision")
    else:
        runs = deleted[0].get("deleted_runs") or []
        if not runs or runs[0].get("field") != "page_number":
            errors.append(f"D7 embedded JSON lost the field identity: {runs!r}")
    opened, open_err = reopen(cli, docx, work)
    if open_err or opened is None:
        errors.append(f"D7 reopen: {open_err}")
        return errors
    paragraph = None
    for block in opened["document"].get("body") or []:
        if "Keep" in block_text(block):
            paragraph = block
            break
    if paragraph is None:
        errors.append("D7 reopen lost the paragraph Keep")
    else:
        if any(run.get("field") for run in paragraph.get("runs") or []):
            errors.append("D7 reopen put the deleted field back into the live runs")
        if block_text(paragraph) != "Keep":
            errors.append(f"D7 reopen live text is {block_text(paragraph)!r}, want 'Keep'")
    live_revs = [rev for rev in opened["revisions"] if rev.get("kind") == "delete"]
    kept = ""
    if live_revs:
        runs = live_revs[0].get("deleted_runs") or []
        if runs:
            kept = runs[0].get("field") or ""
    if kept != "page_number":
        errors.append(f"D7 reopen revision lost the field: {live_revs!r}")
    return errors


def check_xml_color(cli: Path, work: Path) -> list[str]:
    """D9: C0 controls are dropped; w:color is six hex digits or omitted."""
    errors: list[str] = []
    docx = work / "xml.docx"
    bad = 'red"><w:t'
    calls = [
        {"tool": "document_new", "args": {"title": "Wave24 xml", "force": True}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {"id": "pHex", "type": "paragraph", "runs": [{"text": "Hex", "color": "#1A2B3C"}]},
                    {"id": "pBad", "type": "paragraph", "runs": [{"text": "Bad", "color": bad}]},
                    {"id": "pShort", "type": "paragraph", "runs": [{"text": "Short", "color": "#abc"}]},
                    {
                        "id": "pCtl",
                        "type": "paragraph",
                        "runs": [{"text": "A\u0001B\u000bC"}],
                    },
                ],
            },
        },
        {"tool": "document_save", "args": {"path": str(docx)}},
    ]
    _data, err = play(cli, calls, work)
    if err:
        return [f"D9 play: {err}"]
    try:
        xml, _extra = read_docx(docx)
    except (RuntimeError, zipfile.BadZipFile, KeyError) as exc:
        return [f"D9 package: {exc}"]
    if any(byte in xml for byte in C0_FORBIDDEN):
        errors.append("D9 document.xml still contains a forbidden C0 control")
    try:
        root = parse_xml(xml)
    except ET.ParseError as exc:
        return errors + [f"D9 document.xml is not well-formed: {exc}"]
    colors = [aval(el, "val") for el in root.iter(w("color"))]
    if "1A2B3C" not in colors:
        errors.append(f"D9 missing w:color 1A2B3C (got {colors})")
    if any(len(color) != 6 or any(ch not in "0123456789ABCDEFabcdef" for ch in color) for color in colors):
        errors.append(f"D9 wrote a w:color that is not 6 hex digits: {colors}")
    if any(color.lower() in {"abc", "red", "auto"} for color in colors):
        errors.append(f"D9 wrote a rejected color: {colors}")
    plain = "".join((el.text or "") for el in root.iter(w("t")))
    if "ABC" not in plain:
        errors.append(f"D9 control characters were not dropped from the text (got {plain!r})")
    opened, open_err = reopen(cli, docx, work)
    if open_err or opened is None:
        errors.append(f"D9 reopen: {open_err}")
        return errors
    texts = [block_text(block) for block in opened["document"].get("body") or []]
    for needle in ("Hex", "Bad", "Short", "ABC"):
        if needle not in texts:
            errors.append(f"D9 reopen lost {needle!r} (got {texts!r})")
    return errors


def check_bookmarks_fillin(cli: Path, work: Path) -> list[str]:
    """D11: bookmark ids pair uniquely; a FILLIN prompt is escaped once."""
    errors: list[str] = []
    docx = work / "marks.docx"
    calls = [
        {"tool": "document_new", "args": {"title": "Wave24 marks", "force": True}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {"id": "pA", "type": "paragraph", "runs": [{"text": "Alpha"}]},
                    {"id": "pB", "type": "paragraph", "runs": [{"text": "Beta"}]},
                    {"id": "pF", "type": "paragraph", "runs": [{"text": ""}]},
                    {"id": "pQ", "type": "paragraph", "runs": [{"text": ""}]},
                ],
            },
        },
        {"tool": "bookmark_add", "args": {"id": "pA", "name": "first"}},
        {"tool": "bookmark_add", "args": {"id": "pB", "name": "second"}},
        {"tool": "insert_field", "args": {"id": "pF", "offset": 0, "kind": "input", "arg": "R&D"}},
        {"tool": "insert_field", "args": {"id": "pQ", "offset": 0, "kind": "input", "arg": 'say "hi"'}},
        {"tool": "document_save", "args": {"path": str(docx)}},
    ]
    _data, err = play(cli, calls, work)
    if err:
        return [f"D11 play: {err}"]
    try:
        xml, _extra = read_docx(docx)
        root = parse_xml(xml)
    except (RuntimeError, zipfile.BadZipFile, ET.ParseError, KeyError) as exc:
        return [f"D11 package: {exc}"]
    starts = list(root.iter(w("bookmarkStart")))
    ends = list(root.iter(w("bookmarkEnd")))
    start_ids = [aval(el, "id") for el in starts]
    end_ids = [aval(el, "id") for el in ends]
    if len(start_ids) < 2:
        errors.append(f"D11 expected two bookmark starts, got {start_ids}")
    if len(set(start_ids)) != len(start_ids):
        errors.append(f"D11 bookmark start ids are not unique: {start_ids}")
    if sorted(start_ids) != sorted(end_ids):
        errors.append(f"D11 bookmark start/end ids differ: {start_ids} vs {end_ids}")
    if any(not ident.isdigit() for ident in start_ids + end_ids):
        errors.append(f"D11 bookmark ids are not numeric: {start_ids} {end_ids}")
    texts = [(el.text or "") for el in root.iter(w("t"))]
    if "Alpha" not in texts or "Beta" not in texts:
        errors.append(f"D11 paragraph text was not written as w:t (got {texts!r})")
    fillins = [aval(el, "instr") for el in root.iter(w("fldSimple")) if "FILLIN" in aval(el, "instr")]
    if not any("R&D" in instr and "R&amp;" not in instr for instr in fillins):
        errors.append(f"D11 FILLIN prompt R&D was escaped more than once: {fillins!r}")
    if not any('\\"' in instr and 'say' in instr for instr in fillins):
        errors.append(f"D11 FILLIN prompt with a quote was not field-quoted: {fillins!r}")
    opened, open_err = reopen(cli, docx, work)
    if open_err or opened is None:
        errors.append(f"D11 reopen: {open_err}")
        return errors
    texts_back = [block_text(block) for block in opened["document"].get("body") or []]
    if "Alpha" not in texts_back or "Beta" not in texts_back:
        errors.append(f"D11 reopen lost bookmark paragraph text: {texts_back!r}")
    if "R&D" not in texts_back:
        errors.append(f"D11 reopen lost the FILLIN display text: {texts_back!r}")
    fields = [
        run.get("field")
        for block in opened["document"].get("body") or []
        for run in block.get("runs") or []
        if run.get("field")
    ]
    if fields.count("input") < 2:
        errors.append(f"D11 reopen did not keep both input fields: {fields!r}")
    return errors


def check_libreoffice(docx: Path, work: Path) -> list[str]:
    """LibreOffice opens the exported DOCX and keeps nested tables and spans."""
    soffice = shutil.which("soffice")
    if soffice is None:
        return ["D4 interop: soffice is not installed"]
    outdir = work / "lo"
    outdir.mkdir()
    profile = tempfile.mkdtemp(prefix="aioffice-lo-wave24-")
    try:
        proc = subprocess.run(
            [
                soffice,
                "--headless",
                "--nolockcheck",
                "--nologo",
                "--norestore",
                f"-env:UserInstallation=file://{profile}",
                "--convert-to",
                "odt",
                "--outdir",
                str(outdir),
                str(docx),
            ],
            capture_output=True,
            text=True,
            timeout=180,
            check=False,
        )
    except subprocess.TimeoutExpired:
        shutil.rmtree(profile, ignore_errors=True)
        return ["D4 interop: soffice timed out"]
    finally:
        shutil.rmtree(profile, ignore_errors=True)
    odt = outdir / "tables.odt"
    if proc.returncode != 0:
        return [f"D4 interop: soffice exit {proc.returncode}: {(proc.stdout + proc.stderr)[:800]}"]
    if not odt.is_file() or odt.stat().st_size < 64:
        return ["D4 interop: soffice did not write tables.odt"]
    try:
        with zipfile.ZipFile(odt) as zf:
            content = zf.read("content.xml")
    except (zipfile.BadZipFile, KeyError) as exc:
        return [f"D4 interop: {exc}"]
    try:
        root = ET.fromstring(content)
    except ET.ParseError as exc:
        return [f"D4 interop: content.xml is not well-formed: {exc}"]
    errors: list[str] = []
    col_span = False
    row_span = False
    covered = False
    nested = False
    for cell in root.iter(t("table-cell")):
        cols = cell.attrib.get(f"{{{T_NS}}}number-columns-spanned", "")
        rows = cell.attrib.get(f"{{{T_NS}}}number-rows-spanned", "")
        if cols.isdigit() and int(cols) >= 2:
            col_span = True
        if rows.isdigit() and int(rows) >= 2:
            row_span = True
        if any(child.tag == t("table") for child in list(cell)):
            nested = True
    if any(el.tag == t("covered-table-cell") for el in root.iter()):
        covered = True
    text = "".join(root.itertext())
    if not col_span:
        errors.append("D4 interop: LibreOffice dropped the column span")
    if not row_span and not covered:
        errors.append("D4 interop: LibreOffice dropped the row span")
    if not nested or "Inner" not in text:
        errors.append("D4 interop: LibreOffice dropped the nested table")
    if "SpanH" not in text or "Down" not in text:
        errors.append("D4 interop: LibreOffice dropped spanned cell text")
    return errors


def self_test() -> int:
    """The structure checks must reject a document that lacks the wave 24 markup."""
    errors: list[str] = []
    bare = (
        '<?xml version="1.0"?>'
        f'<w:document xmlns:w="{W_NS}"><w:body>'
        "<w:tbl><w:tr><w:tc><w:p><w:r><w:t>SpanH</w:t></w:r></w:p></w:tc>"
        "<w:tc><w:p><w:r><w:t>COVEREDX</w:t></w:r></w:p></w:tc></w:tr></w:tbl>"
        "</w:body></w:document>"
    )
    root = ET.fromstring(bare)
    if b"COVEREDX" not in bare.encode():
        errors.append("self-test fixture lost COVEREDX")
    spans = [aval(el, "val") for el in root.iter(w("gridSpan"))]
    if spans:
        errors.append("self-test bare table unexpectedly has gridSpan")
    good = (
        f'<w:document xmlns:w="{W_NS}"><w:body><w:tbl><w:tr>'
        '<w:tc><w:tcPr><w:gridSpan w:val="2"/></w:tcPr>'
        "<w:p><w:r><w:t>SpanH</w:t></w:r></w:p></w:tc>"
        "</w:tr><w:tr>"
        '<w:tc><w:tcPr><w:vMerge w:val="restart"/></w:tcPr><w:p/></w:tc>'
        "</w:tr><w:tr>"
        '<w:tc><w:tcPr><w:vMerge w:val="continue"/></w:tcPr><w:p/></w:tc>'
        "</w:tr></w:tbl></w:body></w:document>"
    )
    good_root = ET.fromstring(good)
    merges = [aval(el, "val") for el in good_root.iter(w("vMerge"))]
    if "restart" not in merges or "continue" not in merges:
        errors.append("self-test did not read vMerge")
    if "2" not in [aval(el, "val") for el in good_root.iter(w("gridSpan"))]:
        errors.append("self-test did not read gridSpan")
    sample = "A\u0001B".encode() + b"\x0b"
    if not any(byte in sample for byte in C0_FORBIDDEN):
        errors.append("self-test C0 set missed a forbidden byte")
    if any(byte in b"A\tB\nC\rD" for byte in C0_FORBIDDEN):
        errors.append("self-test C0 set rejects tab, LF, or CR")
    if errors:
        for err in errors:
            print("FAIL", err)
        return 1
    print("ok   wave24 docx export self-test")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Wave 24 DOCX export checks")
    parser.add_argument("--cli", type=Path, default=ROOT / "build/linux/aioffice-cli")
    parser.add_argument("--interop", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.cli.is_file():
        print(f"FAIL CLI not found: {args.cli}", file=sys.stderr)
        return 1
    work = Path(tempfile.mkdtemp(prefix="aioffice-wave24-docx-"))
    try:
        errors = []
        errors += check_tables(args.cli, work)
        errors += check_captions(args.cli, work)
        errors += check_deleted_field(args.cli, work)
        errors += check_xml_color(args.cli, work)
        errors += check_bookmarks_fillin(args.cli, work)
        if args.interop:
            tables = work / "tables.docx"
            if tables.is_file():
                errors += check_libreoffice(tables, work)
            else:
                errors.append("D4 interop: tables.docx was not produced")
        if errors:
            for err in errors:
                print("FAIL", err)
            return 1
        print("ok   wave24 docx export")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
