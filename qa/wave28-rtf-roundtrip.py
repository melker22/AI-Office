#!/usr/bin/env python3
"""Wave 28 D14: writer then reader, one RTF round-trip.

Opens a JSON document, saves RTF, and opens that RTF. The judge checks the
three revision kinds (including a format change split by a delete and a second
format change on the inserted text), a comment reply that is resolved, a
comment that stays on the table, a footnote insert, and rect/ellipse/line.
Revision ids are regenerated. The timestamp is already on a UTC minute so the
DTTM grid does not move it. 12 mm is not an integer twip, so the ellipse
height is accepted within 0.02 mm. Shape text does not keep the \\par mark.

The CLI child does not inherit LD_PRELOAD. Files stay in a private temp dir.

Run:  wave28-rtf-roundtrip.py --cli <aioffice-cli>
      wave28-rtf-roundtrip.py --self-test --cli <aioffice-cli>
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path
from typing import Any, Callable

REPO = Path(__file__).resolve().parent.parent
TIMEOUT = 90
# 2024-01-15 00:00 UTC. Already on a minute, so import must keep this value.
STAMP = 1705276800000


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


def play(cli: Path, steps: list[dict[str, Any]], folder: Path) -> dict[str, Any]:
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
        raise SystemExit("timeout after %ss" % TIMEOUT) from exc
    except FileNotFoundError as exc:
        raise SystemExit("cli not executable: %s" % cli) from exc
    raw = (proc.stdout or "").strip()
    if not raw:
        raise SystemExit(
            "play failed (%s): %s" % (proc.returncode, (proc.stderr or "").strip() or "empty stdout")
        )
    try:
        out = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise SystemExit("play bad JSON: %s" % raw[:400]) from exc
    if not out.get("ok"):
        raise SystemExit("play: %s" % out.get("error", out))
    return out


def block_text(block: dict[str, Any]) -> str:
    parts: list[str] = []
    for run in block.get("runs") or []:
        if isinstance(run, dict):
            parts.append(run.get("text") or "")
    return "".join(parts)


def char_bold(block: dict[str, Any]) -> list[bool]:
    flags: list[bool] = []
    for run in block.get("runs") or []:
        if not isinstance(run, dict):
            continue
        flags.extend([bool(run.get("bold"))] * len(run.get("text") or ""))
    return flags


def payload_of(rev: dict[str, Any]) -> Any:
    raw = rev.get("payload")
    if not isinstance(raw, str) or not raw:
        return None
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return None


def run_style(run: dict[str, Any]) -> tuple[str, bool, bool, bool]:
    return (
        run.get("text") or "",
        bool(run.get("bold")),
        bool(run.get("italic")),
        bool(run.get("underline")),
    )


def close_mm(value: Any, want: float, tol: float) -> bool:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return False
    return abs(float(value) - want) <= tol


def comment_by_id(block: dict[str, Any], cid: str) -> dict[str, Any] | None:
    for comment in block.get("comments") or []:
        if isinstance(comment, dict) and comment.get("id") == cid:
            return comment
    return None


def matching(revs: list[dict[str, Any]], **want: Any) -> list[dict[str, Any]]:
    found: list[dict[str, Any]] = []
    for rev in revs:
        if all(rev.get(key) == value for key, value in want.items()):
            found.append(rev)
    return found


def check_document(doc: dict[str, Any]) -> list[str]:
    """Semantic round-trip. Each message is a case the fixture would miss."""
    errors: list[str] = []
    body = [b for b in (doc.get("body") or []) if isinstance(b, dict)]
    kinds = [b.get("type") for b in body]
    want_kinds = ["paragraph", "paragraph", "footnote", "table", "shape", "shape", "shape"]
    if kinds != want_kinds:
        errors.append("D14 body: %s" % kinds)
        return errors

    host, note_host, note, table, rect, ellipse, line = body
    if block_text(host) != "Hello World":
        errors.append("D14 host text: %r" % block_text(host))
    bold = char_bold(host)
    if bold != [False] * 6 + [True] * 5:
        errors.append("D14 host bold: %s" % bold)
    if block_text(note_host) != "See 1":
        errors.append("D14 note host: %r" % block_text(note_host))
    note_ids = [
        run.get("note_id")
        for run in (note_host.get("runs") or [])
        if isinstance(run, dict) and run.get("note_id")
    ]
    if note_ids != [note.get("id")]:
        errors.append("D14 note anchor: %s vs %s" % (note_ids, note.get("id")))
    if block_text(note) != "Nota":
        errors.append("D14 note text: %r" % block_text(note))

    host_id = host.get("id")
    note_id = note.get("id")
    revs = [r for r in (doc.get("revisions") or []) if isinstance(r, dict)]
    if len(revs) != 5:
        errors.append("D14 revision count: %s" % len(revs))

    def one(label: str, found: list[dict[str, Any]]) -> dict[str, Any] | None:
        if len(found) != 1:
            errors.append("D14 %s count: %s" % (label, len(found)))
            return None
        rev = found[0]
        if rev.get("author") != "Ada":
            errors.append("D14 %s author: %s" % (label, rev.get("author")))
        if rev.get("timestamp") != STAMP:
            errors.append("D14 %s timestamp: %s" % (label, rev.get("timestamp")))
        return rev

    fmt_hello = one(
        "format Hello",
        matching(revs, kind="format", block_id=host_id, start=0, end=6),
    )
    if fmt_hello is not None:
        if fmt_hello.get("text") not in ("", None):
            errors.append("D14 format Hello text: %r" % fmt_hello.get("text"))
        if payload_of(fmt_hello) != {"bold": True}:
            errors.append("D14 format Hello payload: %r" % fmt_hello.get("payload"))
        prior = fmt_hello.get("prior_runs") or []
        if len(prior) != 1 or run_style(prior[0]) != ("Hello ", False, True, False):
            errors.append("D14 format Hello prior: %s" % prior)

    deleted = one(
        "delete",
        matching(revs, kind="delete", block_id=host_id, start=5, end=5),
    )
    if deleted is not None:
        if deleted.get("text") != "XY":
            errors.append("D14 delete text: %r" % deleted.get("text"))
        runs = deleted.get("deleted_runs") or []
        styles = [run_style(r) for r in runs if isinstance(r, dict)]
        if styles != [("X", True, False, False), ("Y", False, True, False)]:
            errors.append("D14 deleted_runs: %s" % runs)

    fmt_world = one(
        "format World",
        matching(revs, kind="format", block_id=host_id, start=6, end=11),
    )
    if fmt_world is not None:
        if payload_of(fmt_world) != {"italic": True}:
            errors.append("D14 format World payload: %r" % fmt_world.get("payload"))
        prior = fmt_world.get("prior_runs") or []
        if len(prior) != 1 or run_style(prior[0]) != ("World", False, False, False):
            errors.append("D14 format World prior: %s" % prior)

    inserted = one(
        "insert World",
        matching(revs, kind="insert", block_id=host_id, start=6, end=11),
    )
    if inserted is not None and inserted.get("text") != "World":
        errors.append("D14 insert text: %r" % inserted.get("text"))

    note_ins = one(
        "insert note",
        matching(revs, kind="insert", block_id=note_id, start=0, end=4),
    )
    if note_ins is not None and note_ins.get("text") != "Nota":
        errors.append("D14 note insert: %r" % note_ins.get("text"))

    covered = set()
    for rev in (fmt_hello, deleted, fmt_world, inserted, note_ins):
        if rev is not None:
            covered.add(id(rev))
    extra = [r for r in revs if id(r) not in covered]
    if extra:
        errors.append("D14 extra revisions: %s" % extra)

    c1 = comment_by_id(host, "c1")
    c2 = comment_by_id(host, "c2")
    if c1 is None or c2 is None or len(host.get("comments") or []) != 2:
        errors.append("D14 host comments: %s" % host.get("comments"))
    else:
        if c1.get("author") != "Mary Jane" or c1.get("text") != "line one\nline two":
            errors.append("D14 c1: %s" % c1)
        if c1.get("resolved") is not False or "parent_id" in c1:
            errors.append("D14 c1 is not a root: %s" % c1)
        if c2.get("author") != "QA" or c2.get("text") != "reply":
            errors.append("D14 c2: %s" % c2)
        if c2.get("parent_id") != "c1" or c2.get("resolved") is not True:
            errors.append("D14 c2 reply: %s" % c2)

    ct = comment_by_id(table, "ct")
    if ct is None or len(table.get("comments") or []) != 1:
        errors.append("D14 table comments: %s" % table.get("comments"))
    elif ct.get("author") != "IA" or ct.get("text") != "na tabela" or ct.get("resolved") is not False:
        errors.append("D14 ct: %s" % ct)
    rows = table.get("rows") or []
    cell = ""
    if rows and rows[0] and isinstance(rows[0][0], dict):
        cell = "".join((r.get("text") or "") for r in (rows[0][0].get("runs") or []) if isinstance(r, dict))
    if cell != "cell":
        errors.append("D14 cell: %r" % cell)
    for block in (note_host, note, rect, ellipse, line):
        if block.get("comments"):
            errors.append("D14 comment on %s: %s" % (block.get("type"), block.get("comments")))

    def shape(block: dict[str, Any], kind: str, width: float, height: float, tol: float) -> None:
        if block.get("shape") != kind:
            errors.append("D14 shape %s: %s" % (kind, block.get("shape")))
        if not close_mm(block.get("width_mm"), width, 0):
            errors.append("D14 %s width: %s" % (kind, block.get("width_mm")))
        if not close_mm(block.get("height_mm"), height, tol):
            errors.append("D14 %s height: %s" % (kind, block.get("height_mm")))

    shape(rect, "rect", 30, 10, 0)
    shape(ellipse, "ellipse", 20, 12, 0.02)
    shape(line, "line", 25, 4, 0)
    if rect.get("z_index") != 4:
        errors.append("D14 z: %s" % rect.get("z_index"))
    if (rect.get("fill") or "").upper() != "#B5651D" or (rect.get("stroke") or "").upper() != "#2A2420":
        errors.append("D14 rect paint: %s %s" % (rect.get("fill"), rect.get("stroke")))
    if (ellipse.get("fill") or "").upper() != "#FF0000" or ellipse.get("stroke"):
        errors.append("D14 ellipse paint: %s %s" % (ellipse.get("fill"), ellipse.get("stroke")))
    if (line.get("stroke") or "").upper() != "#0000FF" or line.get("fill"):
        errors.append("D14 line paint: %s %s" % (line.get("fill"), line.get("stroke")))
    if block_text(rect) != "Caixa" or block_text(ellipse) != "Oval" or block_text(line) != "":
        errors.append(
            "D14 shape text: %r %r %r" % (block_text(rect), block_text(ellipse), block_text(line))
        )
    return errors


def source_document() -> dict[str, Any]:
    return {
        "schema": 1,
        "meta": {"title": "wave28 roundtrip", "author": "Ada"},
        "body": [
            {
                "id": "p1",
                "type": "paragraph",
                "runs": [{"text": "Hello "}, {"text": "World", "bold": True}],
                "comments": [
                    {"id": "c1", "author": "Mary Jane", "text": "line one\nline two"},
                    {"id": "c2", "author": "QA", "text": "reply", "parent_id": "c1", "resolved": True},
                ],
            },
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
                "comments": [{"id": "ct", "author": "IA", "text": "na tabela"}],
            },
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
                "fill": "#FF0000",
                "width_mm": 20,
                "height_mm": 12,
                "runs": [{"text": "Oval"}],
            },
            {
                "id": "sh-line",
                "type": "shape",
                "shape": "line",
                "stroke": "#0000FF",
                "width_mm": 25,
                "height_mm": 4,
            },
        ],
        "revisions": [
            {
                "id": "rev-ins",
                "author": "Ada",
                "kind": "insert",
                "block_id": "p1",
                "start": 6,
                "end": 11,
                "text": "World",
                "timestamp": STAMP,
            },
            {
                "id": "rev-fmt-w",
                "author": "Ada",
                "kind": "format",
                "block_id": "p1",
                "start": 6,
                "end": 11,
                "timestamp": STAMP,
                "payload": "{\"italic\":true}",
                "prior_runs": [{"text": "World"}],
            },
            {
                "id": "rev-del",
                "author": "Ada",
                "kind": "delete",
                "block_id": "p1",
                "start": 5,
                "end": 5,
                "text": "XY",
                "timestamp": STAMP,
                "deleted_runs": [
                    {"text": "X", "bold": True},
                    {"text": "Y", "italic": True},
                ],
            },
            {
                "id": "rev-fmt-h",
                "author": "Ada",
                "kind": "format",
                "block_id": "p1",
                "start": 0,
                "end": 6,
                "timestamp": STAMP,
                "payload": "{\"bold\":true}",
                "prior_runs": [{"text": "Hello ", "italic": True}],
            },
            {
                "id": "rev-note",
                "author": "Ada",
                "kind": "insert",
                "block_id": "fn1",
                "start": 0,
                "end": 4,
                "text": "Nota",
                "timestamp": STAMP,
            },
        ],
    }


def expected_document() -> dict[str, Any]:
    """What a correct import of source_document() looks like. Ids are regenerated."""
    doc = source_document()
    doc["body"][0]["id"] = "p1"
    doc["body"][1]["id"] = "p2"
    doc["body"][1]["runs"] = [{"text": "See "}, {"text": "1", "note_id": "fn1"}]
    doc["body"][2]["id"] = "fn1"
    doc["body"][3]["id"] = "t1"
    doc["body"][4]["id"] = "sh1"
    doc["body"][5]["id"] = "sh2"
    doc["body"][5]["height_mm"] = 11.99
    doc["body"][6]["id"] = "sh3"
    for block in doc["body"]:
        for comment in block.get("comments") or []:
            comment.setdefault("resolved", False)
    for rev in doc["revisions"]:
        rev["id"] = "rev-imported"
        if rev["kind"] == "format":
            rev["text"] = ""
        if rev["kind"] == "format" and rev["start"] == 0:
            rev["prior_runs"] = [{"text": "Hello ", "bold": False, "italic": True, "underline": False}]
        if rev["kind"] == "format" and rev["start"] == 6:
            rev["prior_runs"] = [{"text": "World", "bold": False, "italic": False, "underline": False}]
        if rev["kind"] == "delete":
            rev["deleted_runs"] = [
                {"text": "X", "bold": True, "italic": False, "underline": False},
                {"text": "Y", "bold": False, "italic": True, "underline": False},
            ]
    host_runs = doc["body"][0]["runs"]
    host_runs[0] = {"text": "Hello ", "bold": False, "italic": False, "underline": False}
    host_runs[1] = {"text": "World", "bold": True, "italic": False, "underline": False}
    return doc


def judge_fail_case() -> str | None:
    good = check_document(expected_document())
    if good:
        return "judge rejected a complete round-trip: " + good[0]

    def mutate_drop_insert(doc: dict[str, Any]) -> None:
        doc["revisions"] = [
            r for r in doc["revisions"] if not (r["kind"] == "insert" and r["block_id"] == "p1")
        ]

    def mutate_merge_formats(doc: dict[str, Any]) -> None:
        kept = []
        for rev in doc["revisions"]:
            if rev["kind"] == "format" and rev["start"] == 6:
                continue
            if rev["kind"] == "format" and rev["start"] == 0:
                rev["end"] = 11
            kept.append(rev)
        doc["revisions"] = kept

    def mutate_shape_par(doc: dict[str, Any]) -> None:
        doc["body"][4]["runs"][0]["text"] = "Caixa\n"

    def mutate_drop_parent(doc: dict[str, Any]) -> None:
        doc["body"][0]["comments"][1].pop("parent_id", None)

    def mutate_drop_rect_width(doc: dict[str, Any]) -> None:
        doc["body"][4]["width_mm"] = 0

    cases: list[tuple[str, Callable[[dict[str, Any]], None]]] = [
        ("lost the insert", mutate_drop_insert),
        ("merged the two format changes", mutate_merge_formats),
        ("kept the shape paragraph mark", mutate_shape_par),
        ("dropped the comment parent", mutate_drop_parent),
        ("dropped the rectangle width", mutate_drop_rect_width),
    ]
    for name, mutate in cases:
        doc = expected_document()
        mutate(doc)
        if not check_document(doc):
            return "judge accepted a round-trip that " + name
    return None


def rtf_needles(rtf: str) -> list[str]:
    errors: list[str] = []
    for needle in (
        "{\\*\\revtbl",
        "\\revised",
        "\\deleted",
        "\\crauth",
        "{\\*\\aiorevfmt",
        "{\\*\\annotation",
        "{\\*\\aiocparent c1}",
        "{\\*\\aiocresolved 1}",
        "{\\*\\aiocarrier}",
        "{\\sp{\\sn shapeType}{\\sv 1}}",
        "{\\sp{\\sn shapeType}{\\sv 3}}",
        "{\\sp{\\sn shapeType}{\\sv 20}}",
    ):
        if needle not in rtf:
            errors.append("D14 RTF missing %s" % needle)
    if "[Mary Jane:" in rtf or "[IA:" in rtf:
        errors.append("D14 RTF wrote a comment as plain text")
    return errors


def run_roundtrip(cli: Path) -> list[str]:
    errors: list[str] = []
    with tempfile.TemporaryDirectory(prefix="wave28-rtf-rt-") as tmp:
        folder = Path(tmp)
        src = folder / "in.aio.json"
        dst = folder / "out.rtf"
        src.write_text(json.dumps(source_document()), encoding="utf-8")
        try:
            out = play(
                cli,
                [
                    {"tool": "document_open", "args": {"path": str(src)}},
                    {"tool": "document_save", "args": {"path": str(dst)}},
                    {"tool": "document_open", "args": {"path": str(dst)}},
                    {"tool": "validate", "args": {}},
                ],
                folder,
            )
        except SystemExit as exc:
            return ["D14 play: %s" % exc]
        results = out.get("results") or []
        if len(results) != 4:
            return ["D14 result count: %s" % len(results)]
        save = results[1] or {}
        if not save.get("ok"):
            return ["D14 save: %s" % save]
        warnings = [str(item) for item in (save.get("warnings") or [])]
        if warnings:
            errors.append("D14 save warnings: %s" % warnings)
        if not dst.is_file() or dst.stat().st_size == 0:
            return errors + ["D14 RTF was not written"]
        rtf = dst.read_text(encoding="utf-8", errors="replace")
        errors.extend(rtf_needles(rtf))
        opened = results[2] or {}
        if not opened.get("ok"):
            return errors + ["D14 reopen: %s" % opened.get("error", opened)]
        open_warnings = [str(item) for item in (opened.get("warnings") or [])]
        if open_warnings:
            errors.append("D14 open warnings: %s" % open_warnings)
        valid = results[3] or {}
        if valid.get("valid") is not True:
            errors.append("D14 validate: %s" % valid.get("errors", valid))
        doc = opened.get("document") or {}
        errors.extend(check_document(doc))
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", default=None)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        bad = judge_fail_case()
        if bad:
            print("FAIL " + bad)
            return 1
        print("ok   wave28 round-trip judge fail-case")
    cli = find_cli(args.cli)
    if cli is None:
        print("FAIL missing aioffice-cli (build linux preset or pass --cli)")
        return 1
    errors = run_roundtrip(cli)
    for item in errors:
        print("FAIL " + item)
    if errors:
        print("FAIL wave28 rtf round-trip (%s)" % len(errors))
        return 1
    print("ok   wave28 rtf round-trip")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
