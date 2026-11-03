#!/usr/bin/env python3
"""Wave 27 note judges (T3: D1, D2 for DOCX/ODT/RTF + preview/PDF, D3).

Each check fails on the code from before the wave:

* d1 — a U+2029 from an external source in a non-note block is a hard line break,
  while a note block keeps it, and the word count treats it as a space.
* d2 — a note with three differently formatted paragraphs round-trips DOCX, ODT
  and RTF with the paragraph count and each paragraph's formatting, and the PDF
  paints each paragraph on its own line.
* d3 — `insert_footnote` / `insert_endnote` accept `paragraphs: [string]` (and
  `document_get` returns the note with U+2029 between the runs).

The script drives aioffice_cli through generated playbooks, writes under mktemp,
and never passes LD_PRELOAD to the CLI (CI runs as root with another glibc).

Run:  wave27-notes.py --check d1|d2|d3 --cli <aioffice-cli>
      wave27-notes.py --self-test
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parent.parent
TIMEOUT = 180
SEP = "\u2029"
LINE = "\n"


def child_env() -> dict[str, str]:
    env = os.environ.copy()
    # A test interposer must not reach the CLI (L22).
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


def play(cli: Path, steps: list[dict[str, Any]], folder: Path, name: str = "play.json") -> dict[str, Any]:
    playbook = folder / name
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
        raise SystemExit(f"timeout after {TIMEOUT}s: play {name}") from exc
    except FileNotFoundError as exc:
        raise SystemExit(f"cli not executable: {cli}") from exc
    raw = proc.stdout.strip()
    if not raw:
        raise SystemExit(f"play {name} failed ({proc.returncode}): {proc.stderr.strip() or 'empty stdout'}")
    try:
        out = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"play {name} bad JSON: {exc}\n{raw[:400]}") from exc
    if not out.get("ok"):
        raise SystemExit(f"play {name}: {out.get('error', out)}")
    return out


def last_doc(out: dict[str, Any]) -> dict[str, Any]:
    results = out.get("results") or []
    if not results:
        raise SystemExit("play returned no results")
    return results[-1].get("document") or {}


def blocks(doc: dict[str, Any]) -> list[dict[str, Any]]:
    return list(doc.get("body") or [])


def note_paragraphs(block: dict[str, Any]) -> list[list[dict[str, Any]]]:
    """Split a note block's runs on U+2029, mirroring Document::splitNoteParagraphs."""
    paragraphs: list[list[dict[str, Any]]] = [[]]
    for run in block.get("runs") or []:
        text = run.get("text") or ""
        start = 0
        for i in range(len(text) + 1):
            if i < len(text) and text[i] != SEP:
                continue
            if i > start:
                frag = dict(run)
                frag["text"] = text[start:i]
                paragraphs[-1].append(frag)
            if i < len(text):
                paragraphs.append([])
                start = i + 1
    return paragraphs


def para_text(para: list[dict[str, Any]]) -> str:
    return "".join((r.get("text") or "") for r in para)


def para_flag(para: list[dict[str, Any]], key: str) -> bool:
    return any(bool(r.get(key)) for r in para)


# --------------------------------------------------------------------------- D1

def check_d1(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    out = play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "wave27-d1", "force": True}},
            {
                "tool": "insert_blocks",
                "args": {
                    "after_id": "start",
                    "blocks": [
                        {"id": "ext", "type": "paragraph", "runs": [{"text": "A" + SEP + "B"}]},
                        {"id": "fn", "type": "footnote", "runs": [{"text": "one" + SEP + "two"}]},
                    ],
                },
            },
            {"tool": "document_get", "args": {}},
        ],
        folder,
        "d1.json",
    )
    results = out.get("results") or []
    doc = last_doc(out)
    body = blocks(doc)
    ext = next((b for b in body if b.get("id") == "ext"), None)
    fn = next((b for b in body if b.get("type") == "footnote"), None)
    if ext is None:
        errors.append("D1: the external paragraph is missing")
    else:
        text = "".join((r.get("text") or "") for r in ext.get("runs") or [])
        if text != "A" + LINE + "B":
            errors.append(f"D1: external U+2029 not turned into a line break: {text!r}")
    if fn is None:
        errors.append("D1: the footnote block is missing")
    else:
        text = "".join((r.get("text") or "") for r in fn.get("runs") or [])
        if SEP not in text:
            errors.append(f"D1: the note lost its U+2029 separator: {text!r}")
    # word count: "A B" (2) + "one two" (2); document_new's blank paragraph adds 0.
    words = results[-1].get("words") if results else None
    if words != 4:
        errors.append(f"D1: U+2029 is not a word separator (words={words!r}, want 4)")
    return errors


# --------------------------------------------------------------------------- D2

def roundtrip_note(cli: Path, folder: Path, ext: str, errors: list[str]) -> None:
    path = folder / f"note.{ext}"
    out = play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "wave27-d2", "force": True}},
            {
                "tool": "insert_blocks",
                "args": {
                    "after_id": "start",
                    "blocks": [
                        {
                            "id": "host",
                            "type": "paragraph",
                            "runs": [{"text": "Ref "}, {"text": "", "note_id": "fn1"}],
                        },
                        {
                            "id": "fn1",
                            "type": "footnote",
                            "runs": [
                                {"text": "P1", "bold": True},
                                {"text": SEP + "P2", "italic": True},
                                {"text": SEP + "P3", "underline": True},
                            ],
                        },
                    ],
                },
            },
            {"tool": "document_save", "args": {"path": str(path)}},
            {"tool": "document_open", "args": {"path": str(path)}},
            {"tool": "document_get", "args": {}},
        ],
        folder,
        f"d2-{ext}.json",
    )
    if not path.is_file() or path.stat().st_size == 0:
        errors.append(f"D2/{ext}: the file was not written ({path})")
        return
    doc = last_doc(out)
    note = next((b for b in blocks(doc) if b.get("type") == "footnote"), None)
    if note is None:
        errors.append(f"D2/{ext}: the footnote block did not come back")
        return
    paras = note_paragraphs(note)
    texts = [para_text(p) for p in paras]
    if texts != ["P1", "P2", "P3"]:
        errors.append(f"D2/{ext}: note paragraphs are {texts!r}, want ['P1','P2','P3']")
        return
    if not para_flag(paras[0], "bold") or para_flag(paras[0], "italic"):
        errors.append(f"D2/{ext}: paragraph 1 formatting is wrong ({paras[0]!r})")
    if not para_flag(paras[1], "italic") or para_flag(paras[1], "bold"):
        errors.append(f"D2/{ext}: paragraph 2 formatting is wrong ({paras[1]!r})")
    if not para_flag(paras[2], "underline"):
        errors.append(f"D2/{ext}: paragraph 3 formatting is wrong ({paras[2]!r})")


def check_pdf_preview(cli: Path, folder: Path, errors: list[str]) -> None:
    pdf = folder / "preview.pdf"
    out = play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "wave27-preview", "force": True}},
            {
                "tool": "insert_blocks",
                "args": {
                    "after_id": "start",
                    "blocks": [
                        {
                            "id": "host",
                            "type": "paragraph",
                            "runs": [{"text": "Ref "}, {"text": "", "note_id": "fn1"}],
                        },
                        {
                            "id": "fn1",
                            "type": "footnote",
                            "runs": [
                                {"text": "AlphaOne"},
                                {"text": SEP + "BetaTwo"},
                                {"text": SEP + "GammaThree"},
                            ],
                        },
                    ],
                },
            },
            {"tool": "document_export_pdf", "args": {"path": str(pdf)}},
        ],
        folder,
        "d2-preview.json",
    )
    if not out.get("results", [{}])[-1].get("ok"):
        errors.append("D2/preview: document_export_pdf failed")
        return
    if not pdf.is_file() or pdf.stat().st_size == 0:
        errors.append("D2/preview: no PDF was written")
        return
    tool = shutil.which("pdftotext")
    if tool is None:
        errors.append("D2/preview: pdftotext is missing (needed to read the painted lines)")
        return
    proc = subprocess.run(
        [tool, "-bbox", str(pdf), "-"],
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )
    if proc.returncode != 0:
        errors.append(f"D2/preview: pdftotext failed ({proc.returncode})")
        return
    ys: dict[str, float] = {}
    for line in proc.stdout.splitlines():
        if "<word " not in line:
            continue
        raw = re.search(r">([^<]*)</word>", line)
        y = re.search(r'yMin="([-0-9.]+)"', line)
        if raw is None or y is None:
            continue
        word = raw.group(1)
        for want in ("AlphaOne", "BetaTwo", "GammaThree"):
            if word == want:
                ys[want] = float(y.group(1))
    if len(ys) != 3:
        errors.append(f"D2/preview: did not find the three note paragraphs in the PDF ({sorted(ys)})")
        return
    if not (ys["AlphaOne"] < ys["BetaTwo"] < ys["GammaThree"]):
        errors.append(
            "D2/preview: the note paragraphs are not painted on separate lines "
            f"(AlphaOne={ys['AlphaOne']}, BetaTwo={ys['BetaTwo']}, GammaThree={ys['GammaThree']})"
        )


def check_d2(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    for ext in ("docx", "odt", "rtf"):
        try:
            roundtrip_note(cli, folder, ext, errors)
        except SystemExit as exc:
            errors.append(f"D2/{ext}: {exc}")
    try:
        check_pdf_preview(cli, folder, errors)
    except SystemExit as exc:
        errors.append(f"D2/preview: {exc}")
    return errors


# --------------------------------------------------------------------------- D3

def check_d3(cli: Path, folder: Path) -> list[str]:
    errors: list[str] = []
    out = play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "wave27-d3", "force": True}},
            {
                "tool": "insert_blocks",
                "args": {
                    "after_id": "start",
                    "blocks": [{"id": "host", "type": "paragraph", "runs": [{"text": "Ref"}]}],
                },
            },
            {"tool": "insert_footnote", "args": {"id": "host", "offset": 3, "paragraphs": ["P1", "P2", "P3"]}},
            {"tool": "insert_endnote", "args": {"id": "host", "offset": 3, "paragraphs": ["E1", "E2"]}},
            {"tool": "document_get", "args": {}},
        ],
        folder,
        "d3.json",
    )
    doc = last_doc(out)
    foot = next((b for b in blocks(doc) if b.get("type") == "footnote"), None)
    end = next((b for b in blocks(doc) if b.get("type") == "endnote"), None)
    if foot is None:
        errors.append("D3: insert_footnote did not create a footnote block")
    else:
        texts = [para_text(p) for p in note_paragraphs(foot)]
        if texts != ["P1", "P2", "P3"]:
            errors.append(f"D3: insert_footnote paragraphs came back as {texts!r}")
    if end is None:
        errors.append("D3: insert_endnote did not create an endnote block")
    else:
        texts = [para_text(p) for p in note_paragraphs(end)]
        if texts != ["E1", "E2"]:
            errors.append(f"D3: insert_endnote paragraphs came back as {texts!r}")
    return errors


# --------------------------------------------------------------------------- self-test

def judge_fail_case() -> str | None:
    # The judge must be able to fail: a note whose paragraphs are joined on one
    # line must not pass the split check.
    one_line = {"runs": [{"text": "P1" + SEP + "P2", "bold": True}]}
    if [para_text(p) for p in note_paragraphs(one_line)] != ["P1", "P2"]:
        return "note_paragraphs cannot split a separator"
    joined = {"runs": [{"text": "P1P2"}]}
    if [para_text(p) for p in note_paragraphs(joined)] != ["P1P2"]:
        return "note_paragraphs splits without a separator"
    return None


CHECKS = {"d1": check_d1, "d2": check_d2, "d3": check_d3}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", choices=sorted(CHECKS), default=None)
    parser.add_argument("--cli", default=None)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        bad = judge_fail_case()
        if bad:
            print(f"FAIL {bad}")
            return 1
        print("ok   wave27 judge fail-case")
        return 0

    if args.check is None:
        print("FAIL --check is required (or --self-test)")
        return 2
    cli = find_cli(args.cli)
    if cli is None:
        print("FAIL missing aioffice-cli (build linux preset or pass --cli)")
        return 1

    with tempfile.TemporaryDirectory(prefix=f"wave27-{args.check}-") as tmp:
        folder = Path(tmp)
        try:
            errors = CHECKS[args.check](cli, folder)
        except SystemExit as exc:
            errors = [str(exc)]
    for item in errors:
        print(f"FAIL {item}")
    if errors:
        print(f"FAIL wave27 notes {args.check} ({len(errors)})")
        return 1
    print(f"ok   wave27 notes {args.check}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
