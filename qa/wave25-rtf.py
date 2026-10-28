#!/usr/bin/env python3
"""Wave 25 RTF writer judges (D1, D3, D4, D5, D6, D7, D8, D9, D11, D12).

Each check fails on the writer from before this wave. The script drives
aioffice_cli, writes RTF under mktemp, and reads the bytes back. It does not
use a fixed /tmp path and it does not pass LD_PRELOAD to the CLI.

ctest runs this through qa/check_rtf.py --self-test (rtf_check_selftest).
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parent.parent
TIMEOUT = 120


def child_env() -> dict[str, str]:
    env = os.environ.copy()
    # CI inherits an interposer from the parent. The CLI must not see it (L22).
    env.pop("LD_PRELOAD", None)
    env["AIOFFICE_HEADLESS"] = "1"
    env["QT_QPA_PLATFORM"] = "offscreen"
    return env


def find_cli(explicit: str | None) -> Path | None:
    if explicit:
        path = Path(explicit)
        return path if path.is_file() else None
    env = os.environ.get("AIOFFICE_CLI")
    if env and Path(env).is_file():
        return Path(env)
    candidate = REPO / "build" / "linux" / "aioffice-cli"
    return candidate if candidate.is_file() else None


def quote_field_arg(raw: str) -> str:
    """Field-argument quoting from D6: strip controls, escape \\ { } \"."""
    out: list[str] = ['"']
    for ch in raw:
        code = ord(ch)
        if code < 0x20 or code == 0x7F:
            continue
        if ch in '\\{}"':
            out.append("\\")
        out.append(ch)
    out.append('"')
    return "".join(out)


def escape_text(text: str) -> str:
    """Mirror RtfWriter::escapeText for ASCII and the controls the writer emits."""
    out: list[str] = []
    for ch in text:
        if ch == "\\":
            out.append("\\\\")
        elif ch == "{":
            out.append("\\{")
        elif ch == "}":
            out.append("\\}")
        elif ch == "\n":
            out.append("\\line ")
        elif ch == "\t":
            out.append("\\tab ")
        elif ord(ch) < 128:
            out.append(ch)
        else:
            u = ord(ch)
            if u >= 0x8000:
                u -= 0x10000
            out.append(f"\\u{u}?")
    return "".join(out)


def fldinst_rtf(instr: str) -> str:
    return escape_text(instr.strip())


def need(haystack: str, needle: str, label: str) -> str | None:
    if needle not in haystack:
        return f"{label}: missing {needle!r}"
    return None


def forbid(haystack: str, needle: str, label: str) -> str | None:
    if needle in haystack:
        return f"{label}: unexpected {needle!r}"
    return None


def judge_fail_case() -> str | None:
    """The same judge the suite uses must be able to fail."""
    missed = need("\\lang2070", "\\lang1046", "judge")
    if not missed:
        return "judge did not report a missing marker"
    kept = need("\\lang1046", "\\lang1046", "judge")
    if kept:
        return f"judge rejected a present marker: {kept}"
    quoted = fldinst_rtf('MERGEFIELD ' + quote_field_arg('A "B"\\C{D}\n'))
    if '\\"' not in quoted or "\\line" in quoted:
        return f"field quoting helper drifted: {quoted!r}"
    return None


def play(cli: Path, steps: list[dict[str, Any]], folder: Path) -> dict[str, Any]:
    playbook = folder / "play.json"
    playbook.write_text(json.dumps(steps), encoding="utf-8")
    try:
        proc = subprocess.run(
            [str(cli), "play", str(playbook)],
            cwd=REPO,
            env=child_env(),
            capture_output=True,
            text=True,
            timeout=TIMEOUT,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise SystemExit(f"timeout after {TIMEOUT}s: play") from exc
    except FileNotFoundError as exc:
        raise SystemExit(f"cli not executable: {cli}") from exc
    raw = proc.stdout.strip()
    if not raw:
        raise SystemExit(
            f"play failed ({proc.returncode}): {proc.stderr.strip() or 'empty stdout'}"
        )
    try:
        out = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"play bad JSON: {exc}\n{raw[:400]}") from exc
    if not out.get("ok"):
        raise SystemExit(f"play: {out.get('error', out)}")
    return out


def roundtrip(
    cli: Path, steps: list[dict[str, Any]], folder: Path, name: str
) -> tuple[dict[str, Any], str, dict[str, Any]]:
    path = folder / name
    # The save result is the warnings channel. Open replaces the session.
    calls = list(steps) + [
        {"tool": "document_save", "args": {"path": str(path)}},
        {"tool": "document_open", "args": {"path": str(path)}},
        {"tool": "document_get", "args": {}},
    ]
    out = play(cli, calls, folder)
    results = out.get("results") or []
    if len(results) != len(calls):
        raise SystemExit(f"play result count {len(results)} != {len(calls)}")
    save = results[-3]
    got = results[-1]
    if not path.is_file() or path.stat().st_size == 0:
        raise SystemExit(f"RTF was not written: {path}")
    rtf = path.read_text(encoding="utf-8", errors="replace")
    doc = got.get("document") or {}
    return save, rtf, doc


def braces_balanced(rtf: str) -> bool:
    depth = 0
    i = 0
    while i < len(rtf):
        ch = rtf[i]
        if ch == "\\":
            i += 2
            continue
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth < 0:
                return False
        i += 1
    return depth == 0


def plain_of(block: dict[str, Any]) -> str:
    if block.get("type") == "table":
        parts: list[str] = []
        for row in block.get("rows") or []:
            for cell in row:
                parts.append("".join((run.get("text") or "") for run in (cell.get("runs") or [])))
        return " ".join(parts)
    return "".join((run.get("text") or "") for run in (block.get("runs") or []))


def blocks(doc: dict[str, Any]) -> list[dict[str, Any]]:
    return list(doc.get("body") or [])


def by_plain(doc: dict[str, Any], text: str) -> dict[str, Any] | None:
    for block in blocks(doc):
        if text in plain_of(block):
            return block
    return None


def warnings_of(save: dict[str, Any]) -> list[str]:
    return [str(item) for item in (save.get("warnings") or [])]


def num_close(value: Any, want: float, tol: float = 0.02) -> bool:
    try:
        return abs(float(value) - want) <= tol
    except (TypeError, ValueError):
        return False


def check_balanced(rtf: str, label: str, errors: list[str]) -> None:
    if not rtf.startswith("{\\rtf1"):
        errors.append(f"{label}: RTF does not start with {{\\rtf1")
    if not braces_balanced(rtf):
        errors.append(f"{label}: braces are not balanced")


def test_d1(cli: Path, folder: Path) -> list[str]:
    """Known tags round-trip. Unknown tags omit \\lang and warn. No region prefix."""
    errors: list[str] = []
    steps = [
        {"tool": "document_new", "args": {"title": "wave25 d1", "force": True}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {"id": "u1", "type": "paragraph", "lang": "xx-YY", "runs": [{"text": "unknown"}]},
                    {"id": "u2", "type": "paragraph", "lang": "pt-AO", "runs": [{"text": "angola"}]},
                    {"id": "br", "type": "paragraph", "lang": "pt-BR", "runs": [{"text": "brasil"}]},
                    {"id": "bare", "type": "paragraph", "lang": "pt", "runs": [{"text": "portugues"}]},
                    {"id": "gb", "type": "paragraph", "lang": "en-GB", "runs": [{"text": "britain"}]},
                    {"id": "it", "type": "paragraph", "lang": "it-IT", "runs": [{"text": "italia"}]},
                    {"id": "cn", "type": "paragraph", "lang": "zh-CN", "runs": [{"text": "china"}]},
                    {"id": "tw", "type": "paragraph", "lang": "zh-TW", "runs": [{"text": "taiwan"}]},
                    {"id": "es", "type": "paragraph", "lang": "es-ES", "runs": [{"text": "espanha"}]},
                    {"id": "mx", "type": "paragraph", "lang": "es-MX", "runs": [{"text": "mexico"}]},
                    {"id": "he", "type": "paragraph", "lang": "he", "runs": [{"text": "ivrit"}]},
                    {"id": "pt", "type": "paragraph", "lang": "pt-PT", "runs": [{"text": "lisboa"}]},
                    {"id": "us", "type": "paragraph", "lang": "en-US", "runs": [{"text": "usa"}]},
                    # After a known language. \\pard alone must not keep \\lang1033.
                    {"id": "zz", "type": "paragraph", "lang": "xx-ZZ", "runs": [{"text": "depois"}]},
                    {"id": "heil", "type": "paragraph", "lang": "he-IL", "runs": [{"text": "heil"}]},
                    {"id": "fr", "type": "paragraph", "lang": "fr-FR", "runs": [{"text": "franca"}]},
                    {"id": "de", "type": "paragraph", "lang": "de-DE", "runs": [{"text": "alemanha"}]},
                    {"id": "esbare", "type": "paragraph", "lang": "es", "runs": [{"text": "castelhano"}]},
                ],
            },
        },
    ]
    save, rtf, doc = roundtrip(cli, steps, folder, "d1.rtf")
    check_balanced(rtf, "D1", errors)
    warns = warnings_of(save)
    for msg in (
        'RTF export: unknown language "xx-YY" written without \\lang',
        'RTF export: unknown language "pt-AO" written without \\lang',
        'RTF export: unknown language "xx-ZZ" written without \\lang',
        'RTF export: unknown language "he-IL" written without \\lang',
    ):
        if msg not in warns:
            errors.append(f"D1: warning missing {msg!r} in {warns!r}")

    def lang_of(text: str) -> str:
        block = by_plain(doc, text)
        if block is None:
            errors.append(f"D1: paragraph {text!r} missing after import")
            return ""
        return str(block.get("lang") or "")

    expect = {
        "unknown": "",
        "angola": "",
        "brasil": "pt-BR",
        "portugues": "pt-BR",
        "britain": "en-GB",
        "italia": "it-IT",
        "china": "zh-CN",
        "taiwan": "zh-TW",
        "espanha": "es-ES",
        "lisboa": "pt-PT",
        "usa": "en-US",
        "depois": "",
        "heil": "",
        "franca": "fr-FR",
        "alemanha": "de-DE",
        "castelhano": "es-ES",
    }
    for text, want in expect.items():
        got = lang_of(text)
        if got != want:
            errors.append(f"D1: {text!r} lang {got!r} != {want!r}")
    for needle, label in (
        ("\\lang1046", "pt-BR"),
        ("\\lang2057", "en-GB"),
        ("\\lang1040", "it-IT"),
        ("\\lang2052", "zh-CN"),
        ("\\lang1028", "zh-TW"),
        ("\\lang3082", "es-ES"),
        ("\\lang2058", "es-MX"),
        ("\\lang1037", "he"),
        ("\\lang1036", "fr-FR"),
        ("\\lang1031", "de-DE"),
        ("\\lang1049", "ru-absent-ok"),
    ):
        if needle == "\\lang1049":
            continue
        err = need(rtf, needle, f"D1 {label}")
        if err:
            errors.append(err)
    # es-ES is 3082 in the shared table, not the old 1034 alias.
    err = forbid(rtf, "\\lang1034", "D1 es-ES")
    if err:
        errors.append(err)
    # A second document locks ru/ja/ko without depending on the reader tag.
    steps_raw = [
        {"tool": "document_new", "args": {"title": "wave25 d1 raw", "force": True}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {"type": "paragraph", "lang": "ru", "runs": [{"text": "ru"}]},
                    {"type": "paragraph", "lang": "ja", "runs": [{"text": "ja"}]},
                    {"type": "paragraph", "lang": "ko", "runs": [{"text": "ko"}]},
                    {"type": "paragraph", "lang": "ar", "runs": [{"text": "ar"}]},
                ],
            },
        },
    ]
    _save, raw, _doc = roundtrip(cli, steps_raw, folder, "d1-raw.rtf")
    for needle in ("\\lang1049", "\\lang1041", "\\lang1042", "\\lang1025"):
        err = need(raw, needle, "D1 raw")
        if err:
            errors.append(err)
    return errors


def test_d3(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    steps = [
        {"tool": "document_new", "args": {"title": "wave25 d3", "force": True}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {
                        "type": "heading",
                        "level": 1,
                        "space_before_pt": 0,
                        "space_after_pt": 0,
                        "runs": [{"text": "Zeros"}],
                    },
                    {
                        "type": "heading",
                        "level": 1,
                        "space_before_pt": 18,
                        "runs": [{"text": "Dezoito"}],
                    },
                    {"type": "heading", "level": 1, "runs": [{"text": "Padrao"}]},
                ],
            },
        },
    ]
    _save, rtf, doc = roundtrip(cli, steps, folder, "d3.rtf")
    check_balanced(rtf, "D3", errors)

    def spacing(text: str) -> tuple[Any, Any]:
        block = by_plain(doc, text)
        if block is None:
            errors.append(f"D3: heading {text!r} missing")
            return None, None
        return block.get("space_before_pt"), block.get("space_after_pt")

    before, after = spacing("Zeros")
    if not num_close(before, 0, 0.001) or not num_close(after, 0, 0.001):
        errors.append(f"D3: explicit 0 came back as {before!r} / {after!r}")
    before, after = spacing("Dezoito")
    if not num_close(before, 18, 0.001):
        errors.append(f"D3: explicit 18pt before came back as {before!r}")
    before, after = spacing("Padrao")
    if not num_close(before, 12, 0.001) or not num_close(after, 6, 0.001):
        errors.append(f"D3: default heading spacing came back as {before!r} / {after!r}")
    return errors


def test_d4(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    steps = [
        {"tool": "document_new", "args": {"title": "wave25 d4", "force": True}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {
                        "type": "list-item",
                        "list_kind": "decimal",
                        "level": 3,
                        "runs": [{"text": "Terceiro"}],
                    },
                    {
                        "type": "list-item",
                        "list_kind": "bullet",
                        "level": 1,
                        "runs": [{"text": "Bala"}],
                    },
                    {
                        "type": "list-item",
                        "list_kind": "checkbox",
                        "checked": True,
                        "runs": [{"text": "Feito"}],
                    },
                    {
                        "type": "list-item",
                        "list_kind": "checkbox",
                        "checked": False,
                        "runs": [{"text": "Aberto"}],
                    },
                    {
                        "type": "list-item",
                        "list_kind": "task",
                        "checked": True,
                        "runs": [{"text": "Tarefa"}],
                    },
                ],
            },
        },
    ]
    _save, rtf, doc = roundtrip(cli, steps, folder, "d4.rtf")
    check_balanced(rtf, "D4", errors)
    third = by_plain(doc, "Terceiro")
    if third is None:
        errors.append("D4: Terceiro missing")
    else:
        if third.get("type") != "list-item" or int(third.get("level") or 0) != 3:
            errors.append(
                f"D4: level 3 imported as type={third.get('type')!r} level={third.get('level')!r}"
            )
    bullet = by_plain(doc, "Bala")
    if bullet is None or bullet.get("list_kind") != "bullet" or int(bullet.get("level") or 0) != 1:
        errors.append(f"D4: bullet level 1 drifted: {bullet}")
    for needle in (
        "\\ilvl2",
        "{\\*\\aiolistkind checkbox}",
        "{\\*\\aiolistkind task}",
        "{\\*\\aiocheck 1}",
        "{\\*\\aiocheck 0}",
        "\\u9745?",
        "\\u9744?",
    ):
        err = need(rtf, needle, "D4")
        if err:
            errors.append(err)
    if rtf.count("\\listlevel") < 9 * 4:
        errors.append(f"D4: expected 9 list levels per list, saw {rtf.count(chr(92) + 'listlevel')}")
    return errors


def test_d5(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    steps = [
        {"tool": "document_new", "args": {"title": "wave25 d5", "force": True}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {
                        "id": "t1",
                        "type": "table",
                        "header": False,
                        "rows": [[{"text": "A"}, {"text": "B"}]],
                    },
                    {
                        "id": "imgCell",
                        "type": "image",
                        "path": "qa/fixtures/wave19-pixel.png",
                        "alt": "Foto",
                        "anchor": "t1:0:0",
                        "width_mm": 10,
                    },
                    {
                        "id": "imgLost",
                        "type": "image",
                        "path": "qa/fixtures/wave19-pixel.png",
                        "alt": "Perdida",
                        "anchor": "t9:0:0",
                        "width_mm": 10,
                    },
                ],
            },
        },
    ]
    save, rtf, doc = roundtrip(cli, steps, folder, "d5.rtf")
    check_balanced(rtf, "D5", errors)
    warns = warnings_of(save)
    placed = "RTF export: image imgCell in a table cell replaced with [imagem: Foto]"
    missed = "RTF export: image imgLost anchor does not match a table cell; written as \\pict"
    if placed not in warns:
        errors.append(f"D5: missing placed warning in {warns!r}")
    if missed not in warns:
        errors.append(f"D5: missing unmatched-anchor warning in {warns!r}")
    table = next((b for b in blocks(doc) if b.get("type") == "table"), None)
    cell = ""
    if table is None:
        errors.append("D5: table missing after import")
    else:
        rows = table.get("rows") or []
        if rows and rows[0]:
            cell = "".join((run.get("text") or "") for run in (rows[0][0].get("runs") or []))
    if "[imagem: Foto]" not in cell:
        errors.append(f"D5: cell text {cell!r} does not contain the placeholder")
    err = need(rtf, "[imagem: Foto]", "D5 raw")
    if err:
        errors.append(err)
    images = [b for b in blocks(doc) if b.get("type") == "image"]
    if len(images) != 1:
        errors.append(f"D5: unmatched image should still import once, saw {len(images)}")
    err = need(rtf, "\\pngblip", "D5 unmatched pict")
    if err:
        errors.append(err)
    return errors


def test_d6(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    nasty = 'A "B"\\C{D}\n\x01'
    seq_arg = json.dumps({"name": 'Fig "A"', "format": "roman"}, separators=(",", ":"))
    steps = [
        {"tool": "document_new", "args": {"title": "wave25 d6", "force": True}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {
                        "id": "pFields",
                        "type": "paragraph",
                        "runs": [
                            {"text": "Palavras ", "field": "word_count"},
                            {"text": " ", "field": "merge", "field_arg": nasty},
                            {"text": " ", "field": "var", "field_arg": "Var Name"},
                            {"text": " ", "field": "input", "field_arg": 'Say "yes"'},
                            {"text": " ", "field": "seq", "field_arg": seq_arg},
                            {"text": " ", "field": "xref", "field_arg": "alvo"},
                            {"text": " ", "field": "xref", "field_arg": "pgalvo", "xref_kind": "page"},
                            {"text": "site", "link": 'https://ex.com/a"b'},
                        ],
                    }
                ],
            },
        },
    ]
    _save, rtf, doc = roundtrip(cli, steps, folder, "d6.rtf")
    check_balanced(rtf, "D6", errors)
    fields = []
    for block in blocks(doc):
        for run in block.get("runs") or []:
            if run.get("field"):
                fields.append(run.get("field"))
    if "word_count" not in fields:
        errors.append(f"D6: word_count did not round-trip, fields={fields!r}")
    snippets = {
        "NUMWORDS": "NUMWORDS",
        "MERGEFIELD": fldinst_rtf("MERGEFIELD " + quote_field_arg(nasty)),
        "DOCVARIABLE": fldinst_rtf("DOCVARIABLE " + quote_field_arg("Var Name")),
        "FILLIN": fldinst_rtf("FILLIN " + quote_field_arg('Say "yes"')),
        "SEQ": fldinst_rtf("SEQ " + quote_field_arg('Fig "A"') + " \\* ROMAN"),
        "REF": fldinst_rtf('REF "alvo"'),
        "PAGEREF": fldinst_rtf('PAGEREF "pgalvo"'),
        "HYPERLINK": fldinst_rtf("HYPERLINK " + quote_field_arg('https://ex.com/a"b')),
    }
    for label, needle in snippets.items():
        err = need(rtf, needle, f"D6 {label}")
        if err:
            errors.append(err)
    # The quoted instruction itself has no \\line. The field result may, because
    # the visible text still contains the original newline.
    merge = snippets["MERGEFIELD"]
    if "\\line" in merge:
        errors.append("D6: sanitized MERGEFIELD instruction still contains \\\\line")
    return errors


def test_d7(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    steps = [
        {"tool": "document_new", "args": {"title": "wave25 d7", "force": True}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {
                        "id": "pHid",
                        "type": "paragraph",
                        "section_hidden": True,
                        "runs": [{"text": "Segredo"}],
                    },
                    {
                        "id": "pExpr",
                        "type": "paragraph",
                        "hide_if": "1",
                        "runs": [{"text": "Oculto"}],
                    },
                    {
                        "id": "pShow",
                        "type": "paragraph",
                        "hide_if": "0",
                        "runs": [{"text": "Visivel"}],
                    },
                ],
            },
        },
    ]
    save, rtf, doc = roundtrip(cli, steps, folder, "d7.rtf")
    check_balanced(rtf, "D7", errors)
    warns = warnings_of(save)
    for block_id in ("pHid", "pExpr"):
        msg = f"RTF export: hidden text in block {block_id} is published with \\v (not redacted)"
        if msg not in warns:
            errors.append(f"D7: missing warning {msg!r} in {warns!r}")
    if any("pShow" in msg for msg in warns):
        errors.append(f"D7: visible hide_if warned: {warns!r}")
    for text in ("Segredo", "Oculto", "Visivel"):
        if by_plain(doc, text) is None:
            errors.append(f"D7: {text!r} was dropped on import")
    err = need(rtf, "\\v", "D7")
    if err:
        errors.append(err)
    return errors


def test_d8(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    steps = [
        {"tool": "document_new", "args": {"title": "wave25 d8", "force": True}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {"id": "pNote", "type": "paragraph", "runs": [{"text": "Host nota."}]},
                    {"id": "pEnd", "type": "paragraph", "runs": [{"text": "Host fim."}]},
                ],
            },
        },
        {"tool": "insert_footnote", "args": {"id": "pNote", "offset": 4, "text": "Nota forte"}},
        {"tool": "insert_endnote", "args": {"id": "pEnd", "offset": 4, "text": "Nota final"}},
        {"tool": "apply_character_format", "args": {"id": "fn1", "bold": True}},
        {"tool": "apply_character_format", "args": {"id": "en1", "bold": True}},
    ]
    _save, rtf, doc = roundtrip(cli, steps, folder, "d8.rtf")
    check_balanced(rtf, "D8", errors)
    notes = [b for b in blocks(doc) if b.get("type") in ("footnote", "endnote")]
    bold_note = False
    for note in notes:
        text = plain_of(note)
        if "Nota forte" in text and any(run.get("bold") for run in (note.get("runs") or [])):
            bold_note = True
    if not bold_note:
        shown = [(plain_of(n), n.get("runs")) for n in notes]
        errors.append(f"D8: footnote body lost bold: {shown!r}")
    for needle in ("\\ftnalt", "\\chftn"):
        err = need(rtf, needle, "D8")
        if err:
            errors.append(err)
    return errors


def test_d9(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    steps = [
        {"tool": "document_new", "args": {"title": "wave25 d9", "force": True}},
        {
            "tool": "insert_table",
            "args": {
                "after_id": "end",
                "rows": 2,
                "cols": 2,
                "header": True,
                "header_texts": ["H1", "H2"],
                "cells": ["a", "b"],
            },
        },
        {"tool": "insert_break", "args": {"after_id": "end", "kind": "page"}},
        {"tool": "comment_add", "args": {"id": "t1", "text": "na tabela", "author": "IA"}},
        {"tool": "comment_add", "args": {"id": "br1", "text": "na quebra", "author": "IA"}},
    ]
    save, rtf, doc = roundtrip(cli, steps, folder, "d9.rtf")
    check_balanced(rtf, "D9", errors)
    msg = "RTF export: block comments exported as plain text"
    if msg not in warnings_of(save):
        errors.append(f"D9: missing comment warning in {warnings_of(save)!r}")
    blob = "\n".join(plain_of(b) for b in blocks(doc))
    if "na tabela" not in blob:
        errors.append(f"D9: table comment text missing from import: {blob!r}")
    if "na quebra" not in blob:
        errors.append(f"D9: break comment text missing from import: {blob!r}")
    err = need(rtf, "na tabela", "D9 raw table")
    if err:
        errors.append(err)
    err = need(rtf, "na quebra", "D9 raw break")
    if err:
        errors.append(err)
    return errors


def test_d11(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    steps = [
        {"tool": "document_new", "args": {"title": "wave25 d11", "force": True}},
        {
            "tool": "replace_range",
            "args": {
                "id": "p1",
                "blocks": [{"id": "p1", "type": "paragraph", "runs": [{"text": "ABCDEFGH"}]}],
            },
        },
        {"tool": "bookmark_add", "args": {"id": "p1", "name": "meio", "offset": 3}},
    ]
    _save, rtf, doc = roundtrip(cli, steps, folder, "d11.rtf")
    check_balanced(rtf, "D11", errors)
    marks = doc.get("bookmarks") or []
    found = next((m for m in marks if m.get("name") == "meio"), None)
    if found is None:
        errors.append(f"D11: bookmark meio missing: {marks!r}")
    elif int(found.get("offset") or 0) != 3:
        errors.append(f"D11: offset {found.get('offset')!r} != 3")
    err = need(rtf, "{\\*\\bkmkstart meio}", "D11")
    if err:
        errors.append(err)
    # The marker sits after the first three letters, not around \\pard.
    start = rtf.find("{\\*\\bkmkstart meio}")
    if start >= 0:
        before = rtf[max(0, start - 12) : start]
        if "ABC" not in before:
            errors.append(f"D11: bookmark is not after ABC (before={before!r})")
    return errors


def test_d12(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    steps = [
        {"tool": "document_new", "args": {"title": "wave25 d12", "force": True}},
        {
            "tool": "insert_table",
            "args": {
                "after_id": "end",
                "rows": 2,
                "cols": 2,
                "header": True,
                "header_texts": ["L", "R"],
                "cells": ["a", "b"],
                "col_widths_mm": [20, 30],
            },
        },
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [{"type": "paragraph", "runs": [{"text": "Sep1"}]}],
            },
        },
        {
            "tool": "insert_table",
            "args": {
                "after_id": "end",
                "rows": 2,
                "cols": 2,
                "header": False,
                "cells": ["c", "d", "e", "f"],
            },
        },
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [{"type": "paragraph", "runs": [{"text": "Sep2"}]}],
            },
        },
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {
                        "id": "tPartial",
                        "type": "table",
                        "header": False,
                        "col_widths_mm": [20],
                        "rows": [[{"text": "p"}, {"text": "q"}]],
                    }
                ],
            },
        },
    ]
    _save, rtf, doc = roundtrip(cli, steps, folder, "d12.rtf")
    check_balanced(rtf, "D12", errors)
    tables = [b for b in blocks(doc) if b.get("type") == "table"]
    if len(tables) < 3:
        errors.append(f"D12: expected 3 tables, saw {len(tables)}")
        return errors
    widths = tables[0].get("col_widths_mm") or []
    if len(widths) != 2 or not num_close(widths[0], 20, 0.001) or not num_close(widths[1], 30, 0.001):
        errors.append(f"D12: 20/30mm came back as {widths!r}")
    equal = tables[1].get("col_widths_mm") or []
    if len(equal) != 2 or not num_close(equal[0], float(equal[1]), 0.02):
        errors.append(f"D12: absent widths are not equal: {equal!r}")
    partial = tables[2].get("col_widths_mm") or []
    if len(partial) != 2 or num_close(partial[0], float(partial[1]), 0.02):
        errors.append(f"D12: a short width list fell back to equal columns: {partial!r}")
    # 20mm = 1134 twips, 30mm = 1701 twips. cellx is the cumulative right edge.
    err = need(rtf, "\\cellx1134", "D12")
    if err:
        errors.append(err)
    err = need(rtf, "\\cellx2835", "D12")
    if err:
        errors.append(err)
    return errors


def run_suite(cli: Path) -> list[str]:
    errors: list[str] = []
    with tempfile.TemporaryDirectory(prefix="wave25-rtf-") as tmp:
        folder = Path(tmp)
        for name, fn in (
            ("D1", test_d1),
            ("D3", test_d3),
            ("D4", test_d4),
            ("D5", test_d5),
            ("D6", test_d6),
            ("D7", test_d7),
            ("D8", test_d8),
            ("D9", test_d9),
            ("D11", test_d11),
            ("D12", test_d12),
        ):
            try:
                found = fn(cli, folder)
            except SystemExit as exc:
                found = [f"{name}: {exc}"]
            for item in found:
                print(f"FAIL {item}")
                errors.append(item)
            if not found:
                print(f"ok   {name}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", default=None)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        bad = judge_fail_case()
        if bad:
            print(f"FAIL {bad}")
            return 1
        print("ok   wave25 judge fail-case")
    cli = find_cli(args.cli)
    if cli is None:
        print("FAIL missing aioffice-cli (build linux preset or pass --cli)")
        return 1
    errors = run_suite(cli)
    if errors:
        print(f"FAIL wave25 rtf writer ({len(errors)})")
        return 1
    print("ok   wave25 rtf writer")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
