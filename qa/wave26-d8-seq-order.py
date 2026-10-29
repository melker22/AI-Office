#!/usr/bin/env python3
"""Wave 26: a nested-table seq is numbered before the same table's caption.

DOCX writes the nested w:tbl and then the caption paragraph. document_get
must show those same numbers. A category reference still names the caption
seq when the caption has one.
"""

from __future__ import annotations

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
SEQ = {"format": "arabic", "name": "Figura"}
SEQ_ARG = json.dumps(SEQ, separators=(",", ":"))


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


def seq_field_text(runs: list[dict[str, Any]], where: str) -> str:
    fields = [r for r in runs if r.get("field") == "seq"]
    if len(fields) != 1:
        raise SystemExit(f"{where}: expected one seq run, got {fields}")
    return str(fields[0].get("text") or "")


def docx_seq_results(doc_xml: str) -> list[str]:
    # fldSimple stores the instruction and the cached result in document order.
    found = re.findall(
        r'<w:fldSimple w:instr="([^"]*)">(.*?)</w:fldSimple>',
        doc_xml,
        flags=re.DOTALL,
    )
    numbers: list[str] = []
    for instr, inner in found:
        if "SEQ" not in instr.upper():
            continue
        texts = re.findall(r"<w:t[^>]*>([^<]*)</w:t>", inner)
        if not texts:
            raise SystemExit(f"SEQ field has no result text: {instr!r}")
        numbers.append(texts[0])
    return numbers


def main() -> int:
    cli_arg = None
    if "--cli" in sys.argv:
        cli_arg = sys.argv[sys.argv.index("--cli") + 1]
    elif len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
        cli_arg = sys.argv[1]
    cli = find_cli(cli_arg)
    errors: list[str] = []
    with tempfile.TemporaryDirectory(prefix="wave26-d8-seq-") as tmp_s:
        tmp = Path(tmp_s)
        docx = tmp / "out.docx"
        steps: list[dict[str, Any]] = [
            {"tool": "document_new", "args": {"title": "Wave26 seq order", "force": True}},
            {
                "tool": "insert_field",
                "args": {"id": "p1", "offset": 0, "kind": "seq", "arg": SEQ},
            },
            {
                "tool": "insert_blocks",
                "args": {
                    "after_id": "p1",
                    "blocks": [
                        {
                            "id": "t1",
                            "type": "table",
                            "header": False,
                            "rows": [
                                [
                                    {
                                        "runs": [{"text": "cell"}],
                                        "nested": [
                                            [
                                                {
                                                    "runs": [
                                                        {"field": "seq", "field_arg": SEQ_ARG}
                                                    ]
                                                }
                                            ]
                                        ],
                                    }
                                ]
                            ],
                        }
                    ],
                },
            },
            {
                "tool": "insert_caption",
                "args": {"id": "t1", "seq": SEQ, "text": " caption"},
            },
            {
                "tool": "insert_blocks",
                "args": {
                    "after_id": "t1",
                    "blocks": [
                        {
                            "id": "pAfter",
                            "type": "paragraph",
                            "runs": [{"field": "seq", "field_arg": SEQ_ARG}],
                        },
                        {
                            "id": "pXref",
                            "type": "paragraph",
                            "runs": [
                                {
                                    "field": "xref",
                                    "field_arg": "t1",
                                    "xref_kind": "category_number",
                                }
                            ],
                        },
                    ],
                },
            },
            {"tool": "document_get", "args": {}},
            {"tool": "document_save", "args": {"path": str(docx), "embed_json": False}},
        ]
        results = play(cli, steps, tmp)
        doc = (results[5].get("document") or {})
        body = doc.get("body") or []
        if len(body) < 4:
            raise SystemExit(f"expected 4 blocks, got {len(body)}")
        leading = seq_field_text(body[0].get("runs") or [], "leading paragraph")
        table = body[1]
        nested_runs = (
            (((table.get("rows") or [[{}]])[0][0].get("nested") or [[{}]])[0][0].get("runs") or [])
        )
        nested = seq_field_text(nested_runs, "nested cell")
        caption = seq_field_text(table.get("caption_runs") or [], "caption")
        after = seq_field_text(body[2].get("runs") or [], "following paragraph")
        xref_runs = [r for r in (body[3].get("runs") or []) if r.get("field") == "xref"]
        if len(xref_runs) != 1:
            raise SystemExit(f"expected one category xref, got {xref_runs}")
        xref = str(xref_runs[0].get("text") or "")
        with zipfile.ZipFile(docx) as zf:
            doc_xml = zf.read("word/document.xml").decode("utf-8", errors="replace")
        exported = docx_seq_results(doc_xml)
        shown = [leading, nested, caption, after]
        print(f"document_get seqs={shown} xref={xref!r} docx={exported}")
        if not (nested.isdigit() and caption.isdigit()):
            errors.append(f"seq texts are not numbers: nested={nested!r} caption={caption!r}")
        elif int(nested) >= int(caption):
            errors.append(
                f"nested seq {nested} is not before caption seq {caption} in document order"
            )
        if exported != shown:
            errors.append(f"DOCX SEQ results {exported} != document_get {shown}")
        if xref != caption:
            errors.append(f"category reference {xref!r} is not the caption seq {caption!r}")

    if errors:
        print("wave26 seq order FAIL", file=sys.stderr)
        for err in errors:
            print(f"  {err}", file=sys.stderr)
        return 1
    print("wave26 seq order ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
