#!/usr/bin/env python3
"""Wave 20 RTF QA: aio→rtf→aio round-trip and foreign/hostile fixture import."""

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
DEFAULT_TIMEOUT = 300


def headless_env() -> dict[str, str]:
    env = os.environ.copy()
    env["AIOFFICE_HEADLESS"] = "1"
    env["QT_QPA_PLATFORM"] = "offscreen"
    return env


def run_cmd(
    argv: list[str],
    *,
    timeout: int = DEFAULT_TIMEOUT,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            argv,
            capture_output=True,
            text=True,
            timeout=timeout,
            cwd=cwd or REPO,
            env=env or os.environ.copy(),
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise SystemExit(f"timeout after {timeout}s: {' '.join(argv)}") from exc


def play_calls(cli: Path, calls: list[dict]) -> list[dict]:
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, dir=REPO / "qa") as tf:
        json.dump(calls, tf)
        playbook = Path(tf.name)
    try:
        proc = run_cmd([str(cli), "play", str(playbook)], env=headless_env())
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
        if len(results) != len(calls):
            raise SystemExit(f"play result count {len(results)} != {len(calls)}")
        return results
    finally:
        playbook.unlink(missing_ok=True)


def play_file(cli: Path, playbook: Path) -> None:
    proc = run_cmd([str(cli), "play", str(playbook)], env=headless_env())
    raw = proc.stdout.strip()
    if not raw:
        raise SystemExit(f"play failed ({proc.returncode}): {proc.stderr.strip() or 'empty stdout'}")
    try:
        out = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"play bad JSON: {exc}\n{raw[:400]}") from exc
    if not out.get("ok"):
        raise SystemExit(f"play: {out.get('error', out)}")


def load_expectations(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"invalid expectations {path}: {exc}") from exc
    if not isinstance(data, dict):
        raise SystemExit(f"invalid expectations {path}: root must be an object")
    return data


def _cell_text(cell: object) -> str:
    if not isinstance(cell, dict):
        return ""
    text = "".join((r.get("text") or "") for r in (cell.get("runs") or []))
    for nrow in cell.get("nested") or []:
        if isinstance(nrow, list):
            for nested in nrow:
                text += _cell_text(nested)
    return text


def plain_block(body: list, index: int) -> str:
    """Plain text of one block. Tables use cell text, same as Block::plainText."""
    if index >= len(body):
        return ""
    block = body[index]
    rows = block.get("rows") if (block.get("type") or "") == "table" else None
    if isinstance(rows, list) and rows:
        parts: list[str] = []
        for row in rows:
            if not isinstance(row, list):
                continue
            for cell in row:
                parts.append(_cell_text(cell))
        return " ".join(parts)
    runs = block.get("runs") or []
    return "".join(r.get("text") or "" for r in runs)


def run_props_match(got: dict, want: dict) -> bool:
    for key, val in want.items():
        if key == "text":
            if (got.get("text") or "") != val:
                return False
        elif key in ("bold", "italic", "underline", "strike", "superscript", "subscript"):
            if bool(got.get(key)) != bool(val):
                return False
        elif key in ("field", "field_arg", "lang"):
            if (got.get(key) or "") != (val or ""):
                return False
    return True


def compare_body(doc: dict, exp: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    body = doc.get("body") or []
    for want_b in exp.get("body") or []:
        bi = int(want_b["block_index"])
        if bi >= len(body):
            errors.append(f"body block {bi} missing")
            continue
        if "type" in want_b and (body[bi].get("type") or "") != want_b["type"]:
            errors.append(f"block {bi} type {(body[bi].get('type') or '')!r} != {want_b['type']!r}")
        if "plain" in want_b and plain_block(body, bi) != want_b.get("plain", ""):
            errors.append(f"block {bi} plain {plain_block(body, bi)!r} != {want_b.get('plain')!r}")
        if "hide_if" in want_b and (body[bi].get("hide_if") or "") != want_b.get("hide_if"):
            errors.append(
                f"block {bi} hide_if {(body[bi].get('hide_if') or '')!r} != {want_b.get('hide_if')!r}"
            )
        if "dir" in want_b and (body[bi].get("dir") or "") != want_b.get("dir"):
            errors.append(f"block {bi} dir {(body[bi].get('dir') or '')!r} != {want_b.get('dir')!r}")
        got_runs = body[bi].get("runs") or []
        want_runs = want_b.get("runs") or []
        if want_runs and len(got_runs) != len(want_runs):
            errors.append(f"block {bi} run count {len(got_runs)} != {len(want_runs)}")
        else:
            for g, w in zip(got_runs, want_runs):
                if not run_props_match(g, w):
                    errors.append(f"block {bi} run {g!r} != {w!r}")
    for needle in exp.get("plain_contains") or []:
        if isinstance(needle, str):
            joined = "\n".join(plain_block(body, i) for i in range(len(body)))
            if needle not in joined:
                errors.append(f"plain text missing {needle!r}")
    return errors


def try_open(cli: Path, path: Path) -> tuple[bool, str, dict | None]:
    proc = run_cmd(
        [str(cli), "call", "document_open", json.dumps({"path": str(path.resolve())})],
        env=headless_env(),
        timeout=60,
    )
    raw = proc.stdout.strip()
    if not raw:
        return False, proc.stderr.strip() or f"exit {proc.returncode}", None
    try:
        data = json.loads(raw)
    except json.JSONDecodeError:
        return False, f"bad JSON: {raw[:200]}", None
    if data.get("ok"):
        return True, "", data
    return False, str(data.get("error") or data), data


def roundtrip(cli: Path, playbook: Path, expect_path: Path) -> list[str]:
    exp = load_expectations(expect_path)
    rtf = Path(exp.get("rtf") or "")
    if not rtf:
        raise SystemExit(f"{expect_path}: missing rtf path")
    if not rtf.is_absolute():
        rtf = (REPO / rtf).resolve()
    play_file(cli, playbook)
    if not rtf.is_file():
        return [f"RTF not found after export: {rtf}"]
    results = play_calls(
        cli,
        [
            {"tool": "document_open", "args": {"path": str(rtf)}},
            {"tool": "document_get", "args": {}},
        ],
    )
    doc = results[-1].get("document") or {}
    return compare_body(doc, exp)


def fixture_import(cli: Path, fixture: Path, expect_path: Path) -> list[str]:
    exp = load_expectations(expect_path)
    if exp.get("open_error"):
        ok, err, _ = try_open(cli, fixture)
        if ok:
            return ["expected open_error but document_open succeeded"]
        want = exp.get("error_contains") or exp.get("error_substring")
        if isinstance(want, str) and want and want.lower() not in err.lower():
            return [f"open error {err!r} does not contain {want!r}"]
        return []

    results = play_calls(
        cli,
        [
            {"tool": "document_open", "args": {"path": str(fixture.resolve())}},
            {"tool": "document_get", "args": {}},
        ],
    )
    doc = results[-1].get("document") or {}
    return compare_body(doc, exp)


def self_test() -> int:
    ok = True
    body = [{"type": "paragraph", "runs": [{"text": "A", "bold": True}]}]
    exp = {"body": [{"block_index": 0, "plain": "A", "runs": [{"text": "A", "bold": True}]}]}
    errs = compare_body({"body": body}, exp)
    if errs:
        print("FAIL self-test compare_body pass case", errs)
        ok = False
    else:
        print("ok   compare_body pass case")

    bad = compare_body({"body": body}, {"body": [{"block_index": 0, "plain": "B"}]})
    if not bad or "plain" not in bad[0]:
        print("FAIL self-test compare_body fail case")
        ok = False
    else:
        print(f"ok   compare_body fail case ({bad[0]})")

    table = {
        "body": [
            {
                "type": "table",
                "rows": [
                    [{"runs": [{"text": "Header A"}]}, {"runs": [{"text": "Header B"}]}],
                    [{"runs": [{"text": "Cell one"}]}, {"runs": [{"text": "Cell two"}]}],
                ],
            }
        ]
    }
    table_errs = compare_body(table, {"plain_contains": ["Header A", "Cell one"]})
    if table_errs:
        print("FAIL self-test table plain_contains", table_errs)
        ok = False
    else:
        print("ok   table plain_contains")

    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, default=REPO / "build/linux/aioffice-cli")
    parser.add_argument("--playbook", type=Path, help="build + save RTF playbook")
    parser.add_argument("--expect", type=Path, help=".rtf.json expectations")
    parser.add_argument("--fixture", type=Path, help="foreign or hostile .rtf fixture")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    cli = args.cli.resolve()
    if not cli.is_file():
        raise SystemExit(f"missing cli: {cli}")

    if args.fixture:
        fixture = args.fixture.resolve()
        expect = (args.expect or fixture.with_suffix(".rtf.json")).resolve()
        if not expect.is_file():
            raise SystemExit(f"missing expectations: {expect}")
        errors = fixture_import(cli, fixture, expect)
    elif args.playbook:
        playbook = args.playbook.resolve()
        expect = (args.expect or playbook.with_name(playbook.stem + ".rtf.json")).resolve()
        if not expect.is_file():
            raise SystemExit(f"missing expectations: {expect}")
        errors = roundtrip(cli, playbook, expect)
    else:
        raise SystemExit("usage: check_rtf.py --playbook … | --fixture … | --self-test")

    if errors:
        for err in errors:
            print(f"FAIL {err}")
        return 1
    label = args.fixture.name if args.fixture else args.playbook.name
    print(f"ok   {label}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
