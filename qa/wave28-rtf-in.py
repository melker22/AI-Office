#!/usr/bin/env python3
"""Wave 28 RTF import judges (D5, D6, D7, D8, and the read half of D9).

Hand-written RTF only. No opaque binary, no fixed /tmp path, and the CLI
child does not inherit LD_PRELOAD. Full round-trip through the new writer
closes in t4-regress; this judge does not save back out as RTF.

ctest: wave28_rtf_in
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Optional

REPO = Path(__file__).resolve().parent.parent
TIMEOUT = 90
BULK_TIMEOUT = 180

# 2024-01-15 00:00 UTC. Weekday bits are 0 so the value stays easy to read.
# A second value sets weekday Saturday (6) so the integer is above 2^31-1.
DTTM_JAN15 = 130119680
DTTM_FEB31 = 130217984
DTTM_JAN20_WDAY6 = (20 << 11) | (1 << 16) | (124 << 20) | (6 << 29)


def child_env() -> dict[str, str]:
    env = os.environ.copy()
    env.pop("LD_PRELOAD", None)
    env["AIOFFICE_HEADLESS"] = "1"
    env["QT_QPA_PLATFORM"] = "offscreen"
    return env


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def rtf_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace("{", "\\{").replace("}", "\\}")


def utc_ms(year: int, month: int, day: int, hour: int = 0, minute: int = 0) -> int:
    dt = datetime(year, month, day, hour, minute, tzinfo=timezone.utc)
    return int(dt.timestamp() * 1000)


def find_cli(explicit: Optional[str]) -> Path:
    if explicit:
        path = Path(explicit)
        if path.is_file():
            return path
        raise SystemExit("cli not found: %s" % explicit)
    env = os.environ.get("AIOFFICE_CLI")
    if env and Path(env).is_file():
        return Path(env)
    candidate = REPO / "build" / "linux" / "aioffice-cli"
    if candidate.is_file():
        return candidate
    raise SystemExit("aioffice-cli not found; pass --cli")


def run_play(cli: Path, steps: list[dict[str, Any]], folder: Path, timeout: int) -> dict[str, Any]:
    playbook = folder / "play.json"
    playbook.write_text(json.dumps(steps), encoding="utf-8")
    try:
        proc = subprocess.run(
            [str(cli), "play", str(playbook)],
            cwd=str(REPO),
            env=child_env(),
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise AssertionError("timeout after %ss" % timeout) from exc
    raw = (proc.stdout or "").strip()
    if not raw:
        raise AssertionError("empty stdout rc=%s stderr=%s" % (proc.returncode, (proc.stderr or "")[:400]))
    try:
        return json.loads(raw)
    except json.JSONDecodeError as exc:
        raise AssertionError("bad JSON: %s" % raw[:400]) from exc


def write_rtf(folder: Path, name: str, body: str) -> Path:
    path = folder / name
    path.write_bytes(body.encode("ascii"))
    return path


def open_validated(cli: Path, folder: Path, name: str, body: str, timeout: int = TIMEOUT) -> dict[str, Any]:
    path = write_rtf(folder, name, body)
    out = run_play(
        cli,
        [
            {"tool": "document_open", "args": {"path": str(path)}},
            {"tool": "validate", "args": {}},
        ],
        folder,
        timeout,
    )
    expect(out.get("ok") is True, "%s open/validate failed: %s" % (name, out.get("error", out)))
    results = out.get("results") or []
    expect(len(results) >= 2, "%s short results" % name)
    doc = (results[0] or {}).get("document") or {}
    valid = results[1] or {}
    expect(valid.get("valid") is True, "%s validate: %s" % (name, valid.get("errors")))
    return doc


def open_error(cli: Path, folder: Path, name: str, payload: bytes, timeout: int) -> str:
    path = folder / name
    path.write_bytes(payload)
    out = run_play(
        cli,
        [{"tool": "document_open", "args": {"path": str(path)}}],
        folder,
        timeout,
    )
    expect(out.get("ok") is False, "%s expected open to fail, got %s" % (name, out.get("ok")))
    err = out.get("error")
    expect(isinstance(err, str) and err, "%s missing error: %s" % (name, out))
    return err


def plain_runs(runs: Any) -> str:
    if not isinstance(runs, list):
        return ""
    parts = []
    for run in runs:
        if isinstance(run, dict):
            parts.append(str(run.get("text") or ""))
    return "".join(parts)


def body_plain(doc: dict[str, Any]) -> str:
    parts = []
    for block in doc.get("body") or []:
        if not isinstance(block, dict):
            continue
        parts.append(plain_runs(block.get("runs")))
        for row in block.get("rows") or []:
            if not isinstance(row, list):
                continue
            for cell in row:
                if isinstance(cell, dict):
                    parts.append(plain_runs(cell.get("runs")))
    return "\n".join(parts)


def revisions(doc: dict[str, Any]) -> list[dict[str, Any]]:
    raw = doc.get("revisions") or []
    return [r for r in raw if isinstance(r, dict)]


def blocks_of(doc: dict[str, Any], kind: str) -> list[dict[str, Any]]:
    return [b for b in (doc.get("body") or []) if isinstance(b, dict) and b.get("type") == kind]


def self_test() -> None:
    expect(DTTM_JAN15 == (15 << 11) | (1 << 16) | (124 << 20), "jan15 dttm drifted")
    expect(DTTM_JAN20_WDAY6 > 2**31 - 1, "weekday DTTM must exceed signed 32-bit")
    expect(utc_ms(2024, 1, 15) == 1705276800000, "utc ms drifted: %s" % utc_ms(2024, 1, 15))
    escaped = rtf_escape('{"a":1}')
    expect(escaped == '\\{"a":1\\}', "rtf escape drifted: %r" % escaped)
    failed = False
    try:
        expect(False, "boom")
    except AssertionError as exc:
        failed = str(exc) == "boom"
    expect(failed, "judge did not fail a false expect")
    sample = {"body": [{"type": "paragraph", "runs": [{"text": "A"}, {"text": "B", "hidden": True}]}]}
    expect(body_plain(sample) == "AB", "plain helper drifted")
    expect("hidden" not in json.dumps({"text": "keep"}), "absence check drifted")


def check_d5_insert(cli: Path, folder: Path) -> None:
    # \'3b is a literal semicolon. A raw ';' ends the revtbl entry.
    body = (
        "{\\rtf1\\ansi\\ansicpg1252\\pard"
        "{\\*\\revtbl{Unknown;}{Ann\\'3bBob;}}"
        "{\\revised\\revauth1 Hello}"
        "\\par}"
    )
    doc = open_validated(cli, folder, "d5-insert.rtf", body)
    revs = revisions(doc)
    expect(len(revs) == 1, "insert count %s" % revs)
    rev = revs[0]
    expect(rev.get("id") == "rev1", "insert id %s" % rev.get("id"))
    expect(rev.get("kind") == "insert", "insert kind %s" % rev.get("kind"))
    expect(rev.get("author") == "Ann;Bob", "author %r" % rev.get("author"))
    expect(rev.get("start") == 0 and rev.get("end") == 5, "insert range %s" % rev)
    text = plain_runs((blocks_of(doc, "paragraph") or [{}])[0].get("runs"))
    expect(text == "Hello", "insert live text %r" % text)
    expect((doc.get("meta") or {}).get("track_changes") is True, "track_changes not set")


def check_d5_contiguous(cli: Path, folder: Path) -> None:
    body = (
        "{\\rtf1\\ansi\\ansicpg1252\\pard"
        "{\\*\\revtbl{Unknown;}{Ann;}}"
        "{\\revised\\revauth1\\revdttm130119680 Hello {\\b World}}"
        "{\\revised\\revauth1\\revdttm130119680!}"
        "\\par}"
    )
    doc = open_validated(cli, folder, "d5-contiguous.rtf", body)
    revs = revisions(doc)
    expect(len(revs) == 1, "contiguous split into %s" % len(revs))
    rev = revs[0]
    expect(rev.get("kind") == "insert", rev.get("kind"))
    expect(rev.get("author") == "Ann", rev.get("author"))
    expect(rev.get("start") == 0 and rev.get("end") == 12, "range %s-%s" % (rev.get("start"), rev.get("end")))
    expect(rev.get("timestamp") == utc_ms(2024, 1, 15), "timestamp %s" % rev.get("timestamp"))
    runs = (blocks_of(doc, "paragraph") or [{}])[0].get("runs") or []
    expect(plain_runs(runs) == "Hello World!", "text %r" % plain_runs(runs))
    bold = [r for r in runs if isinstance(r, dict) and r.get("bold") is True]
    expect(len(bold) == 1 and bold[0].get("text") == "World", "bold slice %s" % runs)


def check_d5_split(cli: Path, folder: Path) -> None:
    body = (
        "{\\rtf1\\ansi\\ansicpg1252\\pard"
        "{\\*\\revtbl{Unknown;}{Ann;}{Bea;}}"
        "{\\revised\\revauth1 AA}{\\revised\\revauth2 BB}"
        "\\par}"
    )
    doc = open_validated(cli, folder, "d5-split.rtf", body)
    revs = revisions(doc)
    expect(len(revs) == 2, "split count %s" % len(revs))
    expect([r.get("id") for r in revs] == ["rev1", "rev2"], [r.get("id") for r in revs])
    expect(revs[0].get("author") == "Ann" and revs[0].get("text") == "AA", revs[0])
    expect(revs[1].get("author") == "Bea" and revs[1].get("end") == 4, revs[1])
    expect(plain_runs((blocks_of(doc, "paragraph") or [{}])[0].get("runs")) == "AABB", body_plain(doc))


def check_d5_delete(cli: Path, folder: Path) -> None:
    body = (
        "{\\rtf1\\ansi\\ansicpg1252\\pard"
        "{\\*\\revtbl{Unknown;}{Ann;}}"
        "LIVE{\\deleted\\revauthdel1\\revdttmdel130119680 plain {\\b bold}} tail"
        "\\par}"
    )
    doc = open_validated(cli, folder, "d5-delete.rtf", body)
    live = plain_runs((blocks_of(doc, "paragraph") or [{}])[0].get("runs"))
    expect(live == "LIVE tail", "delete leaked into live %r" % live)
    expect("plain" not in live and "bold" not in live, live)
    revs = revisions(doc)
    expect(len(revs) == 1, revs)
    rev = revs[0]
    expect(rev.get("kind") == "delete", rev.get("kind"))
    expect(rev.get("start") == rev.get("end") == 4, "point %s %s" % (rev.get("start"), rev.get("end")))
    expect(rev.get("author") == "Ann", rev.get("author"))
    expect(rev.get("timestamp") == utc_ms(2024, 1, 15), rev.get("timestamp"))
    deleted = rev.get("deleted_runs") or []
    joined = plain_runs(deleted)
    # The space after \revdttmdelN is the control delimiter, not deleted text.
    expect(joined == rev.get("text") == "plain bold", "deleted text %r / %r" % (joined, rev.get("text")))
    bold = [r for r in deleted if isinstance(r, dict) and r.get("bold") is True]
    expect(len(bold) == 1 and bold[0].get("text") == "bold", deleted)


def check_d5_delete_paragraphs(cli: Path, folder: Path) -> None:
    body = (
        "{\\rtf1\\ansi\\ansicpg1252\\pard"
        "{\\*\\revtbl{Unknown;}{Ann;}}"
        "{\\deleted\\revauthdel1 gone}\\par"
        "{\\deleted\\revauthdel1 more}\\par}"
    )
    doc = open_validated(cli, folder, "d5-delparas.rtf", body)
    paras = blocks_of(doc, "paragraph")
    expect(len(paras) == 2, "para count %s" % len(paras))
    for para in paras:
        expect("gone" not in plain_runs(para.get("runs")) and "more" not in plain_runs(para.get("runs")), para)
    revs = revisions(doc)
    expect(len(revs) == 2, revs)
    expect({r.get("block_id") for r in revs} == {paras[0].get("id"), paras[1].get("id")}, revs)
    texts = sorted(str(r.get("text") or "") for r in revs)
    expect(texts == ["gone", "more"], texts)
    for rev in revs:
        expect(rev.get("kind") == "delete" and rev.get("start") == rev.get("end"), rev)


def check_d5_format_third_party(cli: Path, folder: Path) -> None:
    body = (
        "{\\rtf1\\ansi\\ansicpg1252\\pard"
        "{\\*\\revtbl{Unknown;}{Ann;}}"
        "{\\crauth1\\crdate130119680 styled}"
        "\\par}"
    )
    path = write_rtf(folder, "d5-format-bare.rtf", body)
    out = run_play(
        cli,
        [
            {"tool": "document_open", "args": {"path": str(path)}},
            {"tool": "validate", "args": {}},
            {
                "tool": "revision_reject",
                "args": {"id": "rev1"},
                "expect": {"ok": False, "error": "format revision has no prior formatting"},
            },
        ],
        folder,
        TIMEOUT,
    )
    expect(out.get("ok") is True, "bare format play: %s" % out.get("error", out))
    doc = (out["results"][0] or {}).get("document") or {}
    valid = out["results"][1] or {}
    expect(valid.get("valid") is True, valid.get("errors"))
    revs = revisions(doc)
    expect(len(revs) == 1 and revs[0].get("kind") == "format", revs)
    expect(revs[0].get("id") == "rev1", revs[0].get("id"))
    expect(not revs[0].get("prior_runs"), "third-party prior_runs %s" % revs[0].get("prior_runs"))
    expect(plain_runs((blocks_of(doc, "paragraph") or [{}])[0].get("runs")) == "styled", body_plain(doc))


def check_d5_format_payload(cli: Path, folder: Path) -> None:
    payload = {
        "payload": {"bold": True},
        "priorRuns": [
            {"text": "keep", "bold": True},
            {"text": "SECRET", "redact": True},
        ],
    }
    blob = rtf_escape(json.dumps(payload, separators=(",", ":")))
    body = (
        "{\\rtf1\\ansi\\ansicpg1252\\pard"
        "{\\*\\revtbl{Unknown;}{Ann;}}"
        "{\\crauth1\\crdate130119680{\\*\\aiorevfmt " + blob + "}styled}"
        "\\par}"
    )
    path = write_rtf(folder, "d5-format-json.rtf", body)
    out = run_play(
        cli,
        [
            {"tool": "document_open", "args": {"path": str(path)}},
            {"tool": "validate", "args": {}},
            {"tool": "revision_reject", "args": {"id": "rev1"}},
            {"tool": "document_get", "args": {}},
        ],
        folder,
        TIMEOUT,
    )
    expect(out.get("ok") is True, "format json play: %s" % out.get("error", out))
    opened = (out["results"][0] or {}).get("document") or {}
    expect((out["results"][1] or {}).get("valid") is True, out["results"][1])
    revs = revisions(opened)
    expect(len(revs) == 1 and revs[0].get("kind") == "format", revs)
    prior = revs[0].get("prior_runs") or []
    prior_text = plain_runs(prior)
    expect("keep" in prior_text, "prior lost keep: %s" % prior)
    dumped = json.dumps(opened)
    expect("SECRET" not in dumped, "redacted prior text leaked into the document")
    expect(revs[0].get("payload"), "payload missing: %s" % revs[0].get("payload"))
    after = (out["results"][3] or {}).get("document") or {}
    expect("keep" in body_plain(after), "reject did not restore prior text: %r" % body_plain(after))
    expect("SECRET" not in json.dumps(after), "reject restored redacted text")


def check_d5_table(cli: Path, folder: Path) -> None:
    body = (
        "{\\rtf1\\ansi\\ansicpg1252"
        "\\pard\\intbl live {\\deleted\\revauthdel1 secret}{\\revised\\revauth1 added}\\cell\\row"
        "\\pard After\\par}"
    )
    doc = open_validated(cli, folder, "d5-table.rtf", body)
    tables = blocks_of(doc, "table")
    expect(len(tables) == 1, "tables %s" % len(tables))
    cell = plain_runs(tables[0]["rows"][0][0].get("runs"))
    expect("live" in cell and "added" in cell, "cell text %r" % cell)
    expect("secret" not in cell, "deleted entered the cell: %r" % cell)
    expect(revisions(doc) == [], "cell revisions were kept: %s" % revisions(doc))
    expect("secret" not in body_plain(doc), body_plain(doc))


def check_d6_comment(cli: Path, folder: Path) -> None:
    body = (
        "{\\rtf1\\ansi\\ansicpg1252\\pard "
        "Hello "
        "{\\*\\atnid JD}{\\*\\atnauthor Jane Doe}\\chatn"
        "{\\*\\annotation{\\*\\atnref 1}{\\*\\aiocid c7}{\\*\\aiocparent c1}{\\*\\aiocresolved 1}"
        "\\pard\\plain A note\\par Second}"
        "\\par}"
    )
    doc = open_validated(cli, folder, "d6-comment.rtf", body)
    paras = blocks_of(doc, "paragraph")
    expect(len(paras) == 1, "comment became a block: %s" % [p.get("id") for p in paras])
    expect(plain_runs(paras[0].get("runs")) == "Hello ", "annotation leaked: %r" % plain_runs(paras[0].get("runs")))
    expect("A note" not in body_plain(doc) and "Second" not in body_plain(doc), body_plain(doc))
    comments = paras[0].get("comments") or []
    expect(len(comments) == 1, comments)
    comment = comments[0]
    expect(comment.get("id") == "c7", comment.get("id"))
    expect(comment.get("author") == "Jane Doe", comment.get("author"))
    expect(comment.get("text") == "A note\nSecond", "comment text %r" % comment.get("text"))
    expect(comment.get("resolved") is True, comment.get("resolved"))
    expect("parent_id" not in comment, "missing parent kept: %s" % comment.get("parent_id"))


def check_d6_thread(cli: Path, folder: Path) -> None:
    body = (
        "{\\rtf1\\ansi\\ansicpg1252\\pard "
        "{\\*\\atnauthor Ann}\\chatn{\\*\\annotation{\\*\\aiocid c7}Root}\\par"
        "{\\*\\atnid XY}\\chatn{\\*\\annotation{\\*\\aiocparent c7}Reply}\\par}"
    )
    doc = open_validated(cli, folder, "d6-thread.rtf", body)
    paras = blocks_of(doc, "paragraph")
    expect(len(paras) == 2, len(paras))
    first = (paras[0].get("comments") or [{}])[0]
    second = (paras[1].get("comments") or [{}])[0]
    expect(first.get("id") == "c7" and first.get("author") == "Ann", first)
    expect(second.get("id") == "c8", "auto id %s" % second.get("id"))
    expect(second.get("author") == "XY", "initials fallback %s" % second.get("author"))
    expect(second.get("parent_id") == "c7", second.get("parent_id"))
    expect("Root" not in body_plain(doc) and "Reply" not in body_plain(doc), body_plain(doc))


def check_d6_carrier_and_cell(cli: Path, folder: Path) -> None:
    body = (
        "{\\rtf1\\ansi\\ansicpg1252"
        "\\pard\\intbl Alpha\\cell "
        "{\\*\\atnauthor Ed}\\chatn{\\*\\annotation{\\*\\aiocid c4}SecretNote}\\cell\\row"
        "\\pard {\\*\\aiocarrier}{\\*\\atnauthor Ann}\\chatn{\\*\\annotation{\\*\\aiocid c9}On table}\\par"
        "\\page"
        "\\pard {\\*\\aiocarrier}{\\*\\atnauthor Bo}\\chatn{\\*\\annotation{\\*\\aiocid c5}On break}\\par}"
    )
    doc = open_validated(cli, folder, "d6-carrier.rtf", body)
    tables = blocks_of(doc, "table")
    breaks = blocks_of(doc, "break")
    paras = blocks_of(doc, "paragraph")
    expect(len(tables) == 1 and len(breaks) == 1, "blocks t=%s br=%s p=%s" % (len(tables), len(breaks), len(paras)))
    expect(paras == [], "carrier became a paragraph: %s" % paras)
    table_comments = tables[0].get("comments") or []
    ids = sorted(c.get("id") for c in table_comments)
    expect(ids == ["c4", "c9"], ids)
    by_id = {c.get("id"): c for c in table_comments}
    expect(by_id["c4"].get("author") == "Ed" and by_id["c4"].get("text") == "SecretNote", by_id["c4"])
    expect(by_id["c9"].get("text") == "On table", by_id["c9"])
    cell = plain_runs(tables[0]["rows"][0][1].get("runs"))
    expect("SecretNote" not in cell and "Alpha" in plain_runs(tables[0]["rows"][0][0].get("runs")), cell)
    break_comments = breaks[0].get("comments") or []
    expect(len(break_comments) == 1 and break_comments[0].get("text") == "On break", break_comments)
    expect("SecretNote" not in body_plain(doc) and "On table" not in body_plain(doc), body_plain(doc))


def check_d7_shapes(cli: Path, folder: Path) -> None:
    body = (
        "{\\rtf1\\ansi\\ansicpg1252\\pard Before\\par"
        "{\\shp{\\*\\shpinst\\shpleft0\\shptop0\\shpright2268\\shpbottom1134\\shpz3"
        "{\\sp{\\sn shapeType}{\\sv 1}}"
        "{\\sp{\\sn fFilled}{\\sv 1}}{\\sp{\\sn fillColor}{\\sv 255}}"
        "{\\sp{\\sn fLine}{\\sv 1}}{\\sp{\\sn lineColor}{\\sv 16711680}}"
        "{\\shptxt Hello}}{\\shprslt leaked}}"
        "{\\shp{\\*\\shpinst{\\sp{\\sn shapeType}{\\sv 3}}"
        "{\\sp{\\sn fFilled}{\\sv 0}}{\\sp{\\sn fillColor}{\\sv 255}}"
        "\\shpleft0\\shptop0\\shpright2268\\shpbottom1134"
        "{\\shptxt Ell}}}"
        "{\\shp{\\*\\shpinst{\\sp{\\sn shapeType}{\\sv 20}}"
        "{\\sp{\\sn fLine}{\\sv 0}}{\\sp{\\sn lineColor}{\\sv 255}}"
        "\\shpleft0\\shpright2268\\shptop0\\shpbottom1134}}"
        "{\\shp{\\*\\shpinst{\\sp{\\sn shapeType}{\\sv 5}}}{\\shprslt Fallback}}"
        "{\\shpgrp{\\shp{\\*\\shpinst{\\sp{\\sn shapeType}{\\sv 1}}{\\shptxt InGroup}}}}"
        "{\\shp{\\*\\shpinst{\\sp{\\sn shapeType}{\\sv 1}}{\\shptxt Pict}"
        "{\\pict\\pngblip 89504e470d0a}}}"
        "\\par After\\par}"
    )
    doc = open_validated(cli, folder, "d7-shapes.rtf", body)
    shapes = blocks_of(doc, "shape")
    expect(len(shapes) == 4, "shape count %s body=%s" % (len(shapes), [b.get("type") for b in doc.get("body") or []]))
    rect, ell, line, pict_host = shapes
    expect(rect.get("id") == "sh1", rect.get("id"))
    expect(rect.get("shape") == "rect", rect.get("shape"))
    expect(rect.get("width_mm") == 40, rect.get("width_mm"))
    expect(abs(float(rect.get("height_mm") or 0) - 20.0) < 0.001, rect.get("height_mm"))
    expect(rect.get("z_index") == 3, rect.get("z_index"))
    expect(rect.get("fill") == "#FF0000", rect.get("fill"))
    expect(rect.get("stroke") == "#0000FF", rect.get("stroke"))
    expect(plain_runs(rect.get("runs")) == "Hello", rect.get("runs"))
    expect(ell.get("shape") == "ellipse", ell.get("shape"))
    expect("fill" not in ell, "fFilled 0 kept fill %s" % ell.get("fill"))
    expect(plain_runs(ell.get("runs")) == "Ell", ell.get("runs"))
    expect(line.get("shape") == "line", line.get("shape"))
    expect("stroke" not in line, "fLine 0 kept stroke %s" % line.get("stroke"))
    expect(plain_runs(pict_host.get("runs")) == "Pict", pict_host.get("runs"))
    types = [b.get("type") for b in doc.get("body") or []]
    expect("image" not in types, "pict inside shp became an image: %s" % types)
    blob = body_plain(doc)
    expect("Fallback" in blob, "unknown shape dropped shprslt: %r" % blob)
    expect("leaked" not in blob and "InGroup" not in blob, blob)
    paras = blocks_of(doc, "paragraph")
    texts = [plain_runs(p.get("runs")) for p in paras]
    expect(texts[0] == "Before" and texts[-1] == "After", texts)


def check_d8_caps(cli: Path, folder: Path) -> None:
    depth = open_error(cli, folder, "d8-depth.rtf", b"{" * 65 + b"}" * 65, TIMEOUT)
    expect("RTF group nesting depth exceeds 64" in depth, depth)
    blocks = ("{\\rtf1\\ansi\\pard " + ("\\par " * 100001) + "}").encode("ascii")
    over = open_error(cli, folder, "d8-blocks.rtf", blocks, BULK_TIMEOUT)
    expect("RTF document exceeds 100000 blocks" in over, over)
    huge = b"{" + (b"x" * (32 * 1024 * 1024))
    big = open_error(cli, folder, "d8-size.rtf", huge, BULK_TIMEOUT)
    expect("RTF file exceeds 33554432 bytes" in big, big)


def check_d8_author_date(cli: Path, folder: Path) -> None:
    body = (
        "{\\rtf1\\ansi\\ansicpg1252\\pard"
        "{\\*\\revtbl{Unknown;}{Ann;}}"
        "{\\revised\\revauth9\\revdttm%d Out}\\par"
        "{\\revised\\revauth1\\revdttm%d Bad}\\par"
        "{\\revised\\revauth1\\revdttm%d Good}\\par"
        "{\\revised\\revauth1\\revdttm%d Wide}\\par}"
    ) % (DTTM_JAN15, DTTM_FEB31, DTTM_JAN15, DTTM_JAN20_WDAY6)
    doc = open_validated(cli, folder, "d8-author-date.rtf", body)
    revs = revisions(doc)
    expect(len(revs) == 4, revs)
    expect(revs[0].get("author") == "Unknown", revs[0].get("author"))
    expect(revs[0].get("timestamp") == utc_ms(2024, 1, 15), revs[0].get("timestamp"))
    expect(revs[1].get("author") == "Ann" and revs[1].get("timestamp") == 0, revs[1])
    expect(revs[2].get("timestamp") == utc_ms(2024, 1, 15), revs[2].get("timestamp"))
    expect(revs[3].get("timestamp") == utc_ms(2024, 1, 20), "wide dttm %s" % revs[3].get("timestamp"))
    bare = open_validated(
        cli,
        folder,
        "d8-no-revtbl.rtf",
        "{\\rtf1\\ansi\\pard{\\revised\\revauth0 Alone}\\par}",
    )
    alone = revisions(bare)
    expect(len(alone) == 1 and alone[0].get("author") == "Unknown", alone)


def check_d8_bad_json_and_clamp(cli: Path, folder: Path) -> None:
    bare = (
        "{\\rtf1\\ansi\\ansicpg1252\\pard"
        "{\\*\\revtbl{Unknown;}{Ann;}}"
        "{\\crauth1\\crdate130119680{\\*\\aiorevfmt not-json}styled}"
        "\\par}"
    )
    path = write_rtf(folder, "d8-bad-json.rtf", bare)
    out = run_play(
        cli,
        [
            {"tool": "document_open", "args": {"path": str(path)}},
            {"tool": "validate", "args": {}},
            {
                "tool": "revision_reject",
                "args": {"id": "rev1"},
                "expect": {"ok": False, "error": "format revision has no prior formatting"},
            },
        ],
        folder,
        TIMEOUT,
    )
    expect(out.get("ok") is True, out.get("error", out))
    doc = (out["results"][0] or {}).get("document") or {}
    expect((out["results"][1] or {}).get("valid") is True, out["results"][1])
    revs = revisions(doc)
    expect(len(revs) == 1 and not revs[0].get("prior_runs"), revs)
    clamped = (
        "{\\rtf1\\ansi\\ansicpg1252"
        "{\\shp{\\*\\shpinst\\shpleft1440\\shpright0\\shptop0\\shpbottom100000000"
        "{\\sp{\\sn shapeType}{\\sv 1}}{\\shptxt Big}}}"
        "\\par}"
    )
    doc = open_validated(cli, folder, "d8-clamp.rtf", clamped)
    shapes = blocks_of(doc, "shape")
    expect(len(shapes) == 1, shapes)
    expect("width_mm" not in shapes[0], "negative width kept %s" % shapes[0].get("width_mm"))
    expect(abs(float(shapes[0].get("height_mm") or 0) - 10000.0) < 0.001, shapes[0].get("height_mm"))


def check_d9_hidden(cli: Path, folder: Path) -> None:
    body = (
        "{\\rtf1\\ansi\\ansicpg1252\\pard"
        "{\\*\\revtbl{Unknown;}{Ann;}}"
        "{\\revised\\revauth1 visible {\\v hidden}}"
        "{\\deleted\\revauthdel1 {\\v secret}}"
        "\\par}"
    )
    doc = open_validated(cli, folder, "d9-hidden.rtf", body)
    runs = (blocks_of(doc, "paragraph") or [{}])[0].get("runs") or []
    visible = [r for r in runs if isinstance(r, dict) and r.get("text") == "visible "]
    hidden = [r for r in runs if isinstance(r, dict) and r.get("text") == "hidden"]
    expect(len(visible) == 1 and visible[0].get("hidden") is not True, visible)
    expect(len(hidden) == 1 and hidden[0].get("hidden") is True, "hidden run %s" % runs)
    live = plain_runs(runs)
    expect("secret" not in live, "deleted hidden text is live: %r" % live)
    revs = revisions(doc)
    deletes = [r for r in revs if r.get("kind") == "delete"]
    expect(len(deletes) == 1, revs)
    deleted = deletes[0].get("deleted_runs") or []
    secret = [r for r in deleted if isinstance(r, dict) and "secret" in str(r.get("text") or "")]
    expect(len(secret) == 1 and secret[0].get("hidden") is True, deleted)
    expect(deletes[0].get("text") == "secret", deletes[0].get("text"))


def check_d9_redact_overlap(cli: Path, folder: Path) -> None:
    """Session::redactRuns over an existing insert. Reader-independent."""
    out = run_play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "redact"}},
            {
                "tool": "replace_range",
                "args": {
                    "ids": ["p1"],
                    "blocks": [{"id": "p1", "type": "paragraph", "runs": [{"text": "Hello world"}]}],
                },
            },
            {"tool": "track_changes", "args": {"on": True}},
            {"tool": "insert_at_caret", "args": {"id": "p1", "offset": 6, "text": "secret "}},
            {"tool": "apply_character_format", "args": {"ids": ["p1"], "start": 0, "end": 18, "bold": True}},
            {"tool": "redact", "args": {"ids": ["p1"], "start": 6, "end": 12}},
            {"tool": "validate", "args": {}},
            {"tool": "document_get", "args": {}},
        ],
        folder,
        TIMEOUT,
    )
    expect(out.get("ok") is True, "redact play: %s" % out.get("error", out))
    expect((out["results"][6] or {}).get("valid") is True, out["results"][6])
    doc = (out["results"][7] or {}).get("document") or {}
    revs = revisions(doc)
    inserts = [r for r in revs if r.get("kind") == "insert"]
    formats = [r for r in revs if r.get("kind") == "format"]
    expect(len(inserts) == 1, revs)
    expect(inserts[0].get("start") == 6 and inserts[0].get("end") == 13, inserts[0])
    expect(not inserts[0].get("deleted_runs"), inserts[0])
    bold = [r for r in formats if r.get("start") == 0 and r.get("end") == 18]
    expect(len(bold) == 1, formats)
    redacted = [r for r in formats if r.get("start") == 6 and r.get("end") == 12]
    expect(len(redacted) == 1, formats)
    prior = redacted[0].get("prior_runs") or []
    expect(plain_runs(prior) == "secret", prior)
    for run in prior:
        expect(not (run.get("redact") is True and run.get("text")), "prior stored redacted text: %s" % run)
    live_redact = False
    for block in doc.get("body") or []:
        for run in block.get("runs") or []:
            if isinstance(run, dict) and run.get("redact") is True and "secret" in str(run.get("text") or ""):
                live_redact = True
    expect(live_redact, "live redact run missing: %s" % doc.get("body"))
    for rev in revs:
        for run in rev.get("deleted_runs") or []:
            expect("secret" not in str(run.get("text") or ""), rev)


CHECKS: list[tuple[str, Callable[[Path, Path], None]]] = [
    ("D5 insert revtbl", check_d5_insert),
    ("D5 contiguous", check_d5_contiguous),
    ("D5 split ids", check_d5_split),
    ("D5 delete", check_d5_delete),
    ("D5 delete paragraphs", check_d5_delete_paragraphs),
    ("D5 format without aiorevfmt", check_d5_format_third_party),
    ("D5 aiorevfmt", check_d5_format_payload),
    ("D5 table cell", check_d5_table),
    ("D6 comment", check_d6_comment),
    ("D6 thread", check_d6_thread),
    ("D6 carrier", check_d6_carrier_and_cell),
    ("D7 shapes", check_d7_shapes),
    ("D8 caps", check_d8_caps),
    ("D8 author and DTTM", check_d8_author_date),
    ("D8 json and clamp", check_d8_bad_json_and_clamp),
    ("D9 hidden", check_d9_hidden),
    ("D9 redact overlap", check_d9_redact_overlap),
]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", default=None)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--check", default=None, help="substring of one check name")
    args = parser.parse_args()
    self_test()
    if args.self_test:
        print("wave28 rtf-in self-test ok")
        return 0
    cli = find_cli(args.cli)
    failures = []
    with tempfile.TemporaryDirectory(prefix="wave28-rtf-in-") as tmp:
        folder = Path(tmp)
        for name, fn in CHECKS:
            if args.check and args.check not in name:
                continue
            try:
                fn(cli, folder)
                print("ok %s" % name)
            except AssertionError as exc:
                failures.append("%s: %s" % (name, exc))
                print("FAIL %s: %s" % (name, exc))
            except Exception as exc:
                failures.append("%s: %s" % (name, exc))
                print("FAIL %s: %s" % (name, exc))
    if failures:
        print("%s failed" % len(failures))
        return 1
    print("wave28 rtf-in ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
