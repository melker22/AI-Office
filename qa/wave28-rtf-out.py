#!/usr/bin/env python3
"""Wave 28 RTF writer judges (T1: D1, D2, D3, D4, D9 write).

Each check fails on the writer from before this wave. The script drives
aioffice-cli, writes RTF under mktemp, and does not pass LD_PRELOAD to the
CLI. Reading the RTF back into the document model is T2; this judge checks
the bytes and, with --check interop, what LibreOffice keeps in ODT.

Run:  wave28-rtf-out.py --check d1|d2|d3|d4|d9|interop --cli <aioffice-cli>
      wave28-rtf-out.py --self-test --cli <aioffice-cli>
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parent.parent
TIMEOUT = 120
TWIPS_PER_10MM = 567


def child_env() -> dict[str, str]:
    env = os.environ.copy()
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


def need(haystack: str, needle: str, label: str) -> str | None:
    if needle not in haystack:
        return label + ": missing " + repr(needle)
    return None


def forbid(haystack: str, needle: str, label: str) -> str | None:
    if needle in haystack:
        return label + ": unexpected " + repr(needle)
    return None


def twips(mm: float) -> int:
    return int(round(mm * TWIPS_PER_10MM / 10.0))


def dttm(ms: int) -> int:
    """Signed RTF DTTM. Weekday is Sunday=0; values above 2^31-1 are negative."""
    dt = datetime.datetime.fromtimestamp(ms / 1000.0, datetime.timezone.utc)
    dow = dt.isoweekday() % 7
    year = dt.year - 1900
    n = (
        (dt.minute & 0x3F)
        | ((dt.hour & 0x1F) << 6)
        | ((dt.day & 0x1F) << 11)
        | ((dt.month & 0xF) << 16)
        | ((year & 0x1FF) << 20)
        | ((dow & 7) << 29)
    )
    if n >= 2**31:
        n -= 2**32
    return n


def judge_fail_case() -> str | None:
    sample = "{\\rtf1\\ansi hello}"
    missed = need(sample, "\\revtbl", "judge")
    if missed is None:
        return "judge accepted a file with no author table"
    if "revtbl" in sample:
        return "judge needle matched the header by accident"
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
        raise SystemExit("timeout after " + str(TIMEOUT) + "s: play") from exc
    except FileNotFoundError as exc:
        raise SystemExit("cli not executable: " + str(cli)) from exc
    raw = proc.stdout.strip()
    if not raw:
        raise SystemExit("play failed (" + str(proc.returncode) + "): " + (proc.stderr.strip() or "empty stdout"))
    try:
        out = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise SystemExit("play bad JSON: " + str(exc)) from exc
    if not out.get("ok"):
        raise SystemExit("play: " + str(out.get("error", out)))
    return out


def export_rtf(cli: Path, doc: dict[str, Any], folder: Path, name: str) -> tuple[dict[str, Any], str]:
    src = folder / (name + ".aio.json")
    dst = folder / (name + ".rtf")
    src.write_text(json.dumps(doc), encoding="utf-8")
    out = play(
        cli,
        [
            {"tool": "document_open", "args": {"path": str(src)}},
            {"tool": "document_save", "args": {"path": str(dst)}},
        ],
        folder,
    )
    results = out.get("results") or []
    if len(results) != 2:
        raise SystemExit("play result count " + str(len(results)))
    save = results[1]
    if not save.get("ok"):
        raise SystemExit("save: " + str(save))
    if not dst.is_file() or dst.stat().st_size == 0:
        raise SystemExit("RTF was not written: " + str(dst))
    return save, dst.read_text(encoding="utf-8", errors="replace")


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


def check_balanced(rtf: str, label: str, errors: list[str]) -> None:
    if not rtf.startswith("{\\rtf1"):
        errors.append(label + ": RTF does not start with {\\rtf1")
    if not braces_balanced(rtf):
        errors.append(label + ": braces are not balanced")


def warnings_of(save: dict[str, Any]) -> list[str]:
    return [str(item) for item in (save.get("warnings") or [])]


def base_doc(title: str, body: list[dict[str, Any]], revisions: list[dict[str, Any]] | None = None) -> dict[str, Any]:
    doc: dict[str, Any] = {
        "schema": 1,
        "meta": {"title": title, "author": "Ada"},
        "body": body,
    }
    if revisions is not None:
        doc["revisions"] = revisions
    return doc


def test_d1(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    # Revision-list order is the opposite of body order, so a table built from
    # the array would put A;B first. First appearance follows the body.
    stamp = 1700000000000
    doc = base_doc(
        "wave28 d1",
        [
            {"id": "p1", "type": "paragraph", "runs": [{"text": "Um"}]},
            {"id": "p2", "type": "paragraph", "runs": [{"text": "Dois"}]},
        ],
        [
            {
                "id": "rev-b",
                "author": "A;B",
                "kind": "insert",
                "block_id": "p2",
                "start": 0,
                "end": 4,
                "text": "Dois",
                "timestamp": stamp,
            },
            {
                "id": "rev-j",
                "author": "Jos\u00e9",
                "kind": "insert",
                "block_id": "p1",
                "start": 0,
                "end": 2,
                "text": "Um",
                "timestamp": stamp,
            },
            {
                "id": "rev-empty",
                "author": "",
                "kind": "delete",
                "block_id": "p1",
                "start": 2,
                "end": 2,
                "text": "Z",
                "timestamp": 0,
                "deleted_runs": [{"text": "Z"}],
            },
        ],
    )
    _save, rtf = export_rtf(cli, doc, folder, "d1")
    check_balanced(rtf, "D1", errors)
    err = need(rtf, "{\\*\\revtbl{Unknown;}{Jos\\u233?;}{A\\'3bB;}}", "D1")
    if err:
        errors.append(err)
    err = forbid(rtf, "}{;", "D1 empty author entry")
    if err:
        errors.append(err)
    err = need(rtf, "\\revauth1", "D1 Jose index")
    if err:
        errors.append(err)
    err = need(rtf, "\\revauth2", "D1 semicolon author index")
    if err:
        errors.append(err)
    err = need(rtf, "\\revauthdel0", "D1 empty author")
    if err:
        errors.append(err)
    # No revisions: the author table stays out of the header.
    quiet = base_doc("wave28 d1 quiet", [{"id": "p1", "type": "paragraph", "runs": [{"text": "Plain"}]}])
    _save2, rtf2 = export_rtf(cli, quiet, folder, "d1-quiet")
    err = forbid(rtf2, "\\revtbl", "D1 quiet")
    if err:
        errors.append(err)
    return errors


def test_d2(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    stamp = 1700000000000
    want_dttm = str(dttm(stamp))
    doc = base_doc(
        "wave28 d2",
        [
            {"id": "p1", "type": "paragraph", "runs": [{"text": "HELLO"}]},
            {
                "id": "p2",
                "type": "paragraph",
                "runs": [{"text": "See "}, {"text": "", "note_id": "fn1"}],
            },
            {"id": "fn1", "type": "footnote", "runs": [{"text": "Nota"}]},
            {
                "id": "t1",
                "type": "table",
                "rows": [[{"runs": [{"text": "cell"}]}]],
            },
        ],
        [
            {
                "id": "rev-ins",
                "author": "Ada",
                "kind": "insert",
                "block_id": "p1",
                "start": 0,
                "end": 5,
                "text": "HELLO",
                "timestamp": stamp,
            },
            {
                "id": "rev-del",
                "author": "Ada",
                "kind": "delete",
                "block_id": "p1",
                "start": 0,
                "end": 0,
                "text": "XY",
                "timestamp": stamp,
                "deleted_runs": [{"text": "X", "bold": True}, {"text": "Y", "italic": True}],
            },
            {
                "id": "rev-fmt",
                "author": "Ada",
                "kind": "format",
                "block_id": "p1",
                "start": 0,
                "end": 5,
                "timestamp": 0,
                "payload": "{\"bold\":true}",
                "prior_runs": [{"text": "HELLO", "bold": False}],
            },
            {
                "id": "rev-note",
                "author": "Ada",
                "kind": "insert",
                "block_id": "fn1",
                "start": 0,
                "end": 4,
                "text": "Nota",
                "timestamp": stamp,
            },
            {
                "id": "rev-table",
                "author": "Ada",
                "kind": "insert",
                "block_id": "t1",
                "start": 0,
                "end": 4,
                "text": "cell",
                "timestamp": stamp,
            },
        ],
    )
    save, rtf = export_rtf(cli, doc, folder, "d2")
    check_balanced(rtf, "D2", errors)
    err = forbid("\n".join(warnings_of(save)), "tracked revisions exported as plain text", "D2")
    if err:
        errors.append(err)
    warned = [w for w in warnings_of(save) if "rev-table" in w]
    if not warned:
        errors.append("D2: table revision warning does not cite rev-table: " + repr(warnings_of(save)))
    deleted_at = rtf.find("{\\deleted")
    revised_at = rtf.find("{\\revised")
    if deleted_at < 0 or revised_at < 0 or deleted_at > revised_at:
        errors.append("D2: delete at the same offset is not written before the insert")
    err = need(rtf, "\\revdttm" + want_dttm, "D2 insert date")
    if err:
        errors.append(err)
    err = need(rtf, "\\revdttmdel" + want_dttm, "D2 delete date")
    if err:
        errors.append(err)
    err = need(rtf, "{\\b X}", "D2 bold deleted slice")
    if err:
        errors.append(err)
    err = need(rtf, "{\\i Y}", "D2 italic deleted slice")
    if err:
        errors.append(err)
    # timestamp 0 omits the date word. \\crdate must not ride on this format.
    if "\\crdate" in rtf:
        errors.append("D2: format timestamp 0 still wrote \\crdate")
    err = need(rtf, "\\crauth1", "D2 format author")
    if err:
        errors.append(err)
    fmt = group_after(rtf, "{\\*\\aiorevfmt")
    if not fmt:
        errors.append("D2: missing aiorevfmt")
    else:
        for needle in ("priorRuns", "HELLO", "bold"):
            err = need(fmt, needle, "D2 aiorevfmt")
            if err:
                errors.append(err)
    foot = rtf.find("{\\footnote")
    note_rev = rtf.find("\\revised", foot if foot >= 0 else 0)
    note_text = rtf.find("Nota", foot if foot >= 0 else 0)
    if foot < 0 or note_rev < 0 or note_text < 0 or not (foot < note_rev < note_text):
        errors.append("D2: note insert is not inside the footnote")
    return errors


def test_d3(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    doc = base_doc(
        "wave28 d3",
        [
            {
                "id": "p1",
                "type": "paragraph",
                "runs": [{"text": "Host"}],
                "comments": [
                    {"id": "c1", "author": "Mary Jane", "text": "line one\nline two"},
                    {
                        "id": "c2",
                        "author": "QA",
                        "text": "reply",
                        "parent_id": "c1",
                        "resolved": True,
                    },
                ],
            },
            {
                "id": "t1",
                "type": "table",
                "rows": [[{"runs": [{"text": "cell"}]}]],
                "comments": [{"id": "ct", "author": "IA", "text": "na tabela"}],
            },
            {
                "id": "br1",
                "type": "break",
                "break_kind": "page",
                "comments": [{"id": "cb", "author": "IA", "text": "na quebra"}],
            },
            {
                "id": "im1",
                "type": "image",
                "alt": "Figura",
                "comments": [{"id": "ci", "author": "IA", "text": "na imagem"}],
            },
        ],
    )
    _save, rtf = export_rtf(cli, doc, folder, "d3")
    check_balanced(rtf, "D3", errors)
    for banned in ("[Mary Jane:", "[IA:", "[QA:"):
        err = forbid(rtf, banned, "D3 plain comment")
        if err:
            errors.append(err)
    err = need(rtf, "{\\*\\atnid MJ}", "D3 initials")
    if err:
        errors.append(err)
    err = need(rtf, "{\\*\\atnauthor Mary Jane}", "D3 author")
    if err:
        errors.append(err)
    err = need(rtf, "\\par ", "D3 comment newline")
    if err:
        errors.append(err)
    if "line one" not in rtf or "line two" not in rtf:
        errors.append("D3: comment lines were dropped")
    err = need(rtf, "{\\*\\aiocid c1}", "D3 id")
    if err:
        errors.append(err)
    err = need(rtf, "{\\*\\aiocparent c1}", "D3 parent")
    if err:
        errors.append(err)
    err = need(rtf, "{\\*\\aiocresolved 1}", "D3 resolved")
    if err:
        errors.append(err)
    # The root comment has no parent and is not resolved, so those words appear once.
    if rtf.count("\\aiocparent") != 1:
        errors.append("D3: aiocparent count is " + str(rtf.count("\\aiocparent")))
    if rtf.count("\\aiocresolved") != 1:
        errors.append("D3: aiocresolved count is " + str(rtf.count("\\aiocresolved")))
    err = need(rtf, "{\\*\\atrfstart ", "D3 anchor start")
    if err:
        errors.append(err)
    err = need(rtf, "\\chatn{\\*\\annotation", "D3 annotation")
    if err:
        errors.append(err)
    carriers = rtf.count("{\\*\\aiocarrier}")
    if carriers != 3:
        errors.append("D3: expected 3 carrier paragraphs, saw " + str(carriers))
    for needle in ("na tabela", "na quebra", "na imagem"):
        err = need(rtf, needle, "D3 carrier text")
        if err:
            errors.append(err)
    # N is unique. atrfstart numbers must not repeat.
    nums = re.findall(r"\\atrfstart (\d+)", rtf)
    if len(nums) < 5 or len(nums) != len(set(nums)):
        errors.append("D3: annotation ids are not unique: " + repr(nums))
    return errors


def test_d4(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    doc = base_doc(
        "wave28 d4",
        [
            {
                "id": "sh-rect",
                "type": "shape",
                "shape": "rect",
                "fill": "#B5651D",
                "stroke": "#2A2420",
                "width_mm": 30,
                "height_mm": 10,
                "z_index": 4,
                "runs": [{"text": "Caixa"}],
            },
            {
                "id": "sh-ell",
                "type": "shape",
                "shape": "ellipse",
                "width_mm": 0,
                "height_mm": 0,
                "group": "g1",
            },
            {
                "id": "sh-line",
                "type": "shape",
                "shape": "line",
                "fill": "",
                "stroke": "",
                "width_mm": 20,
                "height_mm": 5,
                "connect_to": "sh-rect",
            },
            {"id": "q1", "type": "qr", "qr": "https://example.test", "runs": [{"text": "https://example.test"}]},
            {"id": "f1", "type": "form", "form": "dropdown", "runs": [{"text": "escolha"}]},
        ],
    )
    save, rtf = export_rtf(cli, doc, folder, "d4")
    check_balanced(rtf, "D4", errors)
    err = forbid("\n".join(warnings_of(save)), "shapes/forms exported as plain text", "D4")
    if err:
        errors.append(err)
    joined = "\n".join(warnings_of(save))
    if "QR" not in joined or "dropdown" not in joined:
        errors.append("D4: QR/dropdown warning does not cite both: " + repr(warnings_of(save)))
    if not any("sh-ell" in w for w in warnings_of(save)):
        errors.append("D4: group warning does not cite sh-ell: " + repr(warnings_of(save)))
    if not any("sh-line" in w for w in warnings_of(save)):
        errors.append("D4: connector warning does not cite sh-line: " + repr(warnings_of(save)))
    err = need(rtf, "{\\sp{\\sn shapeType}{\\sv 1}}", "D4 rect")
    if err:
        errors.append(err)
    err = need(rtf, "{\\sp{\\sn shapeType}{\\sv 3}}", "D4 ellipse")
    if err:
        errors.append(err)
    err = need(rtf, "{\\sp{\\sn shapeType}{\\sv 20}}", "D4 line")
    if err:
        errors.append(err)
    err = need(rtf, "\\shpright" + str(twips(30)), "D4 rect width")
    if err:
        errors.append(err)
    err = need(rtf, "\\shpbottom" + str(twips(10)), "D4 rect height")
    if err:
        errors.append(err)
    err = need(rtf, "\\shpright" + str(twips(80)), "D4 zero width fallback")
    if err:
        errors.append(err)
    err = need(rtf, "\\shpbottom" + str(twips(40)), "D4 zero height fallback")
    if err:
        errors.append(err)
    err = need(rtf, "\\shpz4", "D4 z index")
    if err:
        errors.append(err)
    # #B5651D is COLORREF BGR 181 + 101*256 + 29*65536.
    err = need(rtf, "{\\sp{\\sn fillColor}{\\sv 1926581}}", "D4 fill")
    if err:
        errors.append(err)
    err = need(rtf, "{\\sp{\\sn lineColor}{\\sv 2106410}}", "D4 stroke")
    if err:
        errors.append(err)
    err = need(rtf, "{\\sp{\\sn fFilled}{\\sv 0}}", "D4 no fill")
    if err:
        errors.append(err)
    err = need(rtf, "{\\sp{\\sn fLine}{\\sv 0}}", "D4 no stroke")
    if err:
        errors.append(err)
    err = need(rtf, "{\\shptxt ", "D4 shape text")
    if err:
        errors.append(err)
    err = need(rtf, "Caixa", "D4 shape text body")
    if err:
        errors.append(err)
    lids = re.findall(r"\\shplid(\d+)", rtf)
    if len(lids) != 3 or len(set(lids)) != 3:
        errors.append("D4: shplid values are not unique: " + repr(lids))
    err = need(rtf, "\\shpwr1", "D4 wrap")
    if err:
        errors.append(err)
    return errors


def group_after(rtf: str, marker: str) -> str:
    at = rtf.find(marker)
    if at < 0:
        return ""
    depth = 0
    i = at
    while i < len(rtf):
        ch = rtf[i]
        if ch == "\\":
            i += 2
            continue
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return rtf[at : i + 1]
        i += 1
    return rtf[at:]


def test_d9(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    stamp = 1700000000000
    hidden = base_doc(
        "wave28 d9 hidden",
        [
            {
                "id": "pH",
                "type": "paragraph",
                "section_hidden": True,
                "runs": [{"text": "Hid"}],
            }
        ],
        [
            {
                "id": "rev-h-ins",
                "author": "Ada",
                "kind": "insert",
                "block_id": "pH",
                "start": 0,
                "end": 3,
                "text": "Hid",
                "timestamp": stamp,
            },
            {
                "id": "rev-h-del",
                "author": "Ada",
                "kind": "delete",
                "block_id": "pH",
                "start": 0,
                "end": 0,
                "text": "Old",
                "timestamp": stamp,
                "deleted_runs": [{"text": "Old"}],
            },
        ],
    )
    save, rtf = export_rtf(cli, hidden, folder, "d9-hidden")
    check_balanced(rtf, "D9 hidden", errors)
    if not any("pH" in w and "\\v" in w for w in warnings_of(save)):
        errors.append("D9: hidden-text warning does not cite pH: " + repr(warnings_of(save)))
    vanished = group_after(rtf, "{\\v")
    if "\\revised" not in vanished or "\\deleted" not in vanished:
        errors.append("D9: \\v group does not contain the revision markup")

    # Live text still carries the token (redact does not erase it). prior_runs are the
    # snapshot from before the flag, so they have the token and redact is absent.
    redacted = base_doc(
        "wave28 d9 redact",
        [
            {
                "id": "pR",
                "type": "paragraph",
                "runs": [{"text": "SECRETTOKEN", "redact": True}],
                "comments": [{"id": "cR", "author": "QA", "text": "note SECRETTOKEN"}],
            }
        ],
        [
            {
                "id": "rev-redel",
                "author": "Ada",
                "kind": "delete",
                "block_id": "pR",
                "start": 0,
                "end": 0,
                "text": "SECRETTOKEN",
                "timestamp": stamp,
                "deleted_runs": [{"text": "SECRETTOKEN", "redact": True, "bold": True}],
            },
            {
                "id": "rev-refmt",
                "author": "Ada",
                "kind": "format",
                "block_id": "pR",
                "start": 0,
                "end": 11,
                "timestamp": stamp,
                "payload": "{\"redact\":true}",
                "prior_runs": [{"text": "SECRETTOKEN"}],
            },
        ],
    )
    _save2, rtf2 = export_rtf(cli, redacted, folder, "d9-redact")
    check_balanced(rtf2, "D9 redact", errors)
    deleted = group_after(rtf2, "{\\deleted")
    if not deleted:
        errors.append("D9: redacted deletion was not written")
    elif "SECRETTOKEN" in deleted:
        errors.append("D9: redacted deletedRuns leaked into \\deleted")
    fmt = group_after(rtf2, "{\\*\\aiorevfmt")
    if not fmt:
        errors.append("D9: redacted format revision has no aiorevfmt")
    elif "SECRETTOKEN" in fmt:
        errors.append("D9: redacted priorRuns leaked into aiorevfmt")
    ann = group_after(rtf2, "{\\*\\annotation")
    if not ann:
        errors.append("D9: comment was not written as an annotation")
    elif "SECRETTOKEN" in ann:
        errors.append("D9: redacted text leaked into the comment")

    # What Session::redactRuns actually stores. The live run keeps the text and
    # gains redact; the format revision's prior_runs keep the text without the flag.
    steps = [
        {"tool": "document_new", "args": {"title": "wave28 redactRuns", "force": True}},
        {"tool": "track_changes", "args": {"on": True}},
        {"tool": "insert_at_caret", "args": {"id": "p1", "offset": 0, "text": "SECRETTOKEN"}},
        {"tool": "redact", "args": {"id": "p1", "start": 0, "end": 11}},
        {"tool": "document_get", "args": {}},
    ]
    got = play(cli, steps, folder)
    results = got.get("results") or []
    doc = (results[-1] or {}).get("document") or {}
    runs = ((doc.get("body") or [{}])[0].get("runs") or [{}])
    live = runs[0] if runs else {}
    if live.get("text") != "SECRETTOKEN" or not live.get("redact"):
        errors.append("D9: redactRuns did not keep the live text with redact: " + repr(live))
    formats = [r for r in (doc.get("revisions") or []) if r.get("kind") == "format"]
    if not formats:
        errors.append("D9: redactRuns with track_changes stored no format revision")
    else:
        prior = (formats[-1].get("prior_runs") or [{}])[0]
        if prior.get("text") != "SECRETTOKEN" or prior.get("redact"):
            errors.append("D9: redactRuns prior_runs are not the unmarked original: " + repr(prior))
    return errors


def test_interop(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    soffice = shutil.which("soffice")
    if not soffice:
        errors.append("interop: soffice is not on PATH")
        return errors
    stamp = 1700000000000
    doc = base_doc(
        "wave28 interop",
        [
            {
                "id": "p1",
                "type": "paragraph",
                "runs": [{"text": "Hello World"}],
                "comments": [{"id": "c1", "author": "Ada", "text": "Note text"}],
            },
            {"id": "sh-rect", "type": "shape", "shape": "rect", "fill": "#B5651D", "width_mm": 30, "height_mm": 10},
            {
                "id": "sh-ell",
                "type": "shape",
                "shape": "ellipse",
                "stroke": "#2A2420",
                "width_mm": 20,
                "height_mm": 12,
                "runs": [{"text": "Oval"}],
            },
            {"id": "sh-line", "type": "shape", "shape": "line", "stroke": "#2A2420", "width_mm": 25, "height_mm": 4},
        ],
        [
            {
                "id": "rev-ins",
                "author": "Ada",
                "kind": "insert",
                "block_id": "p1",
                "start": 6,
                "end": 11,
                "text": "World",
                "timestamp": stamp,
            },
            {
                "id": "rev-del",
                "author": "Ada",
                "kind": "delete",
                "block_id": "p1",
                "start": 5,
                "end": 5,
                "text": "X",
                "timestamp": stamp,
                "deleted_runs": [{"text": "X"}],
            },
        ],
    )
    _save, rtf_text = export_rtf(cli, doc, folder, "interop")
    rtf_path = folder / "interop.rtf"
    rtf_path.write_text(rtf_text, encoding="utf-8")
    profile = Path(tempfile.mkdtemp(prefix="wave28-lo-"))
    outdir = folder / "odt"
    outdir.mkdir(exist_ok=True)
    try:
        proc = subprocess.run(
            [
                soffice,
                "--headless",
                "--nolockcheck",
                "--nologo",
                "--norestore",
                "-env:UserInstallation=file://" + str(profile),
                "--convert-to",
                "odt",
                "--outdir",
                str(outdir),
                str(rtf_path),
            ],
            capture_output=True,
            text=True,
            timeout=180,
            check=False,
        )
    finally:
        shutil.rmtree(profile, ignore_errors=True)
    odt = outdir / "interop.odt"
    if proc.returncode != 0 or not odt.is_file():
        errors.append("interop: soffice did not write ODT (" + str(proc.returncode) + ")")
        return errors
    with zipfile.ZipFile(odt) as zf:
        xml = zf.read("content.xml").decode("utf-8", errors="replace")
    for needle in (
        "text:insertion",
        "text:deletion",
        "office:annotation",
        'draw:type="rectangle"',
        'draw:type="ellipse"',
        "draw:line",
    ):
        err = need(xml, needle, "interop")
        if err:
            errors.append(err)
    if "<dc:creator>Ada</dc:creator>" not in xml:
        errors.append("interop: annotation or revision is missing creator Ada")
    return errors


CHECKS = {
    "d1": test_d1,
    "d2": test_d2,
    "d3": test_d3,
    "d4": test_d4,
    "d9": test_d9,
    "interop": test_interop,
}


def run_checks(cli: Path, names: list[str]) -> list[str]:
    errors: list[str] = []
    with tempfile.TemporaryDirectory(prefix="wave28-rtf-out-") as tmp:
        folder = Path(tmp)
        for name in names:
            try:
                found = CHECKS[name](cli, folder)
            except SystemExit as exc:
                found = [name + ": " + str(exc)]
            for item in found:
                print("FAIL " + item)
                errors.append(item)
            if not found:
                print("ok   " + name)
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", default=None)
    parser.add_argument("--check", choices=sorted(CHECKS))
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        bad = judge_fail_case()
        if bad:
            print("FAIL " + bad)
            return 1
        print("ok   wave28 judge fail-case")
    cli = find_cli(args.cli)
    if cli is None:
        print("FAIL missing aioffice-cli (build linux preset or pass --cli)")
        return 1
    names = [args.check] if args.check else [name for name in CHECKS if name != "interop"]
    errors = run_checks(cli, names)
    if errors:
        print("FAIL wave28 rtf writer (" + str(len(errors)) + ")")
        return 1
    print("ok   wave28 rtf writer")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
