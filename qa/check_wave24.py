#!/usr/bin/env python3
"""Wave 24 DOCX/ODT fidelity judges (D1–D16): import fixtures and export playbooks."""

from __future__ import annotations

import argparse
import base64
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


def play(cli: Path, playbook: Path) -> list[dict]:
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
    with playbook.open(encoding="utf-8") as handle:
        calls = json.load(handle)
    if len(results) != len(calls):
        raise SystemExit(f"play result count {len(results)} != {len(calls)}")
    return results


def run_playbook_calls(cli: Path, calls: list[dict]) -> list[dict]:
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, dir=REPO / "qa") as tf:
        json.dump(calls, tf)
        playbook = Path(tf.name)
    try:
        return play(cli, playbook)
    finally:
        playbook.unlink(missing_ok=True)


def load_expect(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"invalid expectations {path}: {exc}") from exc
    if not isinstance(data, dict):
        raise SystemExit(f"invalid expectations {path}: root must be object")
    return data


def expect_path_for(target: Path) -> Path:
    for suffix in (".wave24.json",):
        candidate = target.with_suffix(suffix)
        if candidate.is_file():
            return candidate
    alt = Path(str(target) + ".wave24.json")
    if alt.is_file():
        return alt
    raise SystemExit(f"missing expectation file for {target}")


def plain_block(body: list, index: int) -> str:
    if index >= len(body):
        return ""
    runs = body[index].get("runs") or []
    return "".join(r.get("text") or "" for r in runs)


def run_matches(got: dict, want: dict) -> list[str]:
    errors: list[str] = []
    for key, val in want.items():
        if key == "text":
            if (got.get("text") or "") != val:
                errors.append(f"run text {got.get('text')!r} != {val!r}")
        elif key in ("bold", "italic", "underline", "strike", "bidi"):
            if bool(got.get(key)) != bool(val):
                errors.append(f"run {key} {got.get(key)!r} != {val!r}")
        elif key in ("size_pt", "font", "color", "highlight"):
            if (got.get(key) or "") != (val or ""):
                errors.append(f"run {key} {got.get(key)!r} != {val!r}")
    return errors


def check_body(doc: dict, exp: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    body = doc.get("body") or []
    for want_b in exp.get("body") or []:
        bi = int(want_b["block_index"])
        if bi >= len(body):
            errors.append(f"body block {bi} missing (have {len(body)})")
            continue
        block = body[bi]
        if "type" in want_b and (block.get("type") or "") != want_b["type"]:
            errors.append(f"block {bi} type {block.get('type')!r} != {want_b['type']!r}")
        if "plain" in want_b and plain_block(body, bi) != want_b["plain"]:
            errors.append(
                f"block {bi} plain {plain_block(body, bi)!r} != {want_b['plain']!r}"
            )
        want_runs = want_b.get("runs") or []
        got_runs = block.get("runs") or []
        if want_runs and len(got_runs) != len(want_runs):
            errors.append(f"block {bi} run count {len(got_runs)} != {len(want_runs)}")
        for ri, wr in enumerate(want_runs):
            if ri >= len(got_runs):
                break
            errors.extend(f"block {bi} run {ri}: {e}" for e in run_matches(got_runs[ri], wr))
        if "caption" in want_b and (block.get("caption") or "") != want_b["caption"]:
            errors.append(f"block {bi} caption {block.get('caption')!r} != {want_b['caption']!r}")
        if "nested" in want_b:
            nested = block.get("nested") or block.get("rows")
            if not nested and want_b["nested"]:
                errors.append(f"block {bi} missing nested table")
    bc = exp.get("block_types") or []
    if bc:
        types = [b.get("type") for b in body]
        for need in bc:
            if need not in types:
                errors.append(f"body missing block type {need!r}; got {types}")
    if "block_count_min" in exp:
        need = int(exp["block_count_min"])
        if len(body) < need:
            errors.append(f"body length {len(body)} < {need}")
    return errors


def check_page(doc: dict, exp: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    page = exp.get("page") or {}
    if not page:
        return errors
    meta = doc.get("meta") or {}
    pg = meta.get("page") or doc.get("page") or {}
    for key in ("width_mm", "height_mm", "size"):
        if key in page:
            got = pg.get(key)
            want = page[key]
            if isinstance(want, (int, float)) and isinstance(got, (int, float)):
                if abs(float(got) - float(want)) > float(exp.get("page_tolerance_mm", 1.0)):
                    errors.append(f"page.{key} {got} != {want} (±{exp.get('page_tolerance_mm', 1)})")
            elif (got or "") != want:
                errors.append(f"page.{key} {got!r} != {want!r}")
    return errors


def check_meta(doc: dict, exp: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    meta_want = exp.get("meta") or {}
    if not meta_want:
        return errors
    meta = doc.get("meta") or {}
    for key, val in meta_want.items():
        if (meta.get(key) or "") != val:
            errors.append(f"meta.{key} {meta.get(key)!r} != {val!r}")
    return errors


def read_zip_member(path: Path, member: str) -> str:
    with zipfile.ZipFile(path) as zf:
        return zf.read(member).decode("utf-8", errors="replace")


def check_export_xml(path: Path, fmt: str, rules: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if fmt == "docx":
        xml = read_zip_member(path, "word/document.xml")
        for part in rules.get("docx_parts") or []:
            xml += "\n" + read_zip_member(path, part)
    elif fmt == "odt":
        xml = read_zip_member(path, "content.xml")
        styles = rules.get("odt_styles")
        if styles:
            try:
                xml += "\n" + read_zip_member(path, "styles.xml")
            except KeyError:
                errors.append("ODT export missing styles.xml")
    else:
        raise SystemExit(f"unknown export format {fmt!r}")

    for needle in rules.get("must_contain") or []:
        if needle not in xml:
            errors.append(f"{fmt} export missing substring {needle!r}")
    for needle in rules.get("must_not_contain") or []:
        if needle in xml:
            errors.append(f"{fmt} export must not contain {needle!r}")
    for pattern in rules.get("must_match") or []:
        if not re.search(pattern, xml, re.DOTALL):
            errors.append(f"{fmt} export missing regex {pattern!r}")
    if rules.get("unique_bookmark_ids"):
        ids = re.findall(r'w:bookmarkStart[^>]+w:id="(\d+)"', xml)
        if len(ids) != len(set(ids)):
            errors.append(f"DOCX bookmark ids not unique: {ids}")
    if rules.get("odt_png_min"):
        with zipfile.ZipFile(path) as zf:
            pngs = [n for n in zf.namelist() if n.lower().endswith(".png")]
        if len(pngs) < int(rules["odt_png_min"]):
            errors.append(f"ODT export expected ≥{rules['odt_png_min']} PNG, got {len(pngs)}")
    return errors


def export_and_check(cli: Path, doc_path: Path | None, exp: dict[str, Any], tmp: Path) -> list[str]:
    errors: list[str] = []
    export_rules = exp.get("export") or {}
    if not export_rules:
        return errors
    calls: list[dict] = []
    if doc_path:
        calls.append({"tool": "document_open", "args": {"path": str(doc_path.resolve())}})
    else:
        raise SystemExit("export checks require a document path")
    for fmt in ("docx", "odt"):
        rules = export_rules.get(fmt)
        if not rules:
            continue
        out = tmp / f"wave24-export.{fmt}"
        calls.append({"tool": "document_save", "args": {"path": str(out)}})
        run_playbook_calls(cli, calls)
        errors.extend(check_export_xml(out, fmt, rules))
        calls = calls[:1]
    return errors


def check_fixture(cli: Path, fixture: Path, exp: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    fp = str(fixture.resolve())
    results = run_playbook_calls(
        cli,
        [
            {"tool": "document_open", "args": {"path": fp}},
            {"tool": "document_get", "args": {}},
        ],
    )
    doc = results[-1].get("document") or results[-1]
    errors.extend(check_body(doc, exp))
    errors.extend(check_page(doc, exp))
    errors.extend(check_meta(doc, exp))
    with tempfile.TemporaryDirectory(prefix="aioffice-wave24-") as td:
        errors.extend(export_and_check(cli, fixture, exp, Path(td)))
    return errors


def check_playbook_case(cli: Path, playbook: Path, exp: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    calls = json.loads(playbook.read_text(encoding="utf-8"))
    aio_path: Path | None = None
    if exp.get("export_after_play"):
        with tempfile.TemporaryDirectory(prefix="aioffice-wave24-") as td:
            aio_path = Path(td) / "doc.aio.json"
            calls = [*calls, {"tool": "document_save", "args": {"path": str(aio_path)}}]
            results = run_playbook_calls(cli, calls)
            if exp.get("document_get_after"):
                doc_res = None
                for call, res in zip(calls, results):
                    if call.get("tool") == "document_get":
                        doc_res = res.get("document") or res
                if doc_res is not None:
                    errors.extend(check_body(doc_res, exp))
                    errors.extend(check_page(doc_res, exp))
                    errors.extend(check_meta(doc_res, exp))
            errors.extend(export_and_check(cli, aio_path, exp, Path(td)))
            return errors
    results = play(cli, playbook)
    if exp.get("document_get_after"):
        doc_res = None
        for call, res in zip(calls, results):
            if call.get("tool") == "document_get":
                doc_res = res.get("document") or res
        if doc_res is not None:
            errors.extend(check_body(doc_res, exp))
            errors.extend(check_page(doc_res, exp))
            errors.extend(check_meta(doc_res, exp))
    return errors


def self_test() -> int:
    ok = True

    def expect_fail(fn, needle: str) -> None:
        nonlocal ok
        try:
            fn()
            print(f"FAIL self-test expected SystemExit containing {needle!r}")
            ok = False
        except SystemExit as exc:
            if needle not in str(exc):
                print(f"FAIL self-test message {exc!r} missing {needle!r}")
                ok = False
            else:
                print(f"ok   fail case ({needle})")

    expect_fail(lambda: load_expect(Path("/nonexistent.wave24.json")), "invalid expectations")

    doc = {
        "body": [{"type": "paragraph", "runs": [{"text": "Hi", "size_pt": 12}]}],
        "meta": {"page": {"width_mm": 210, "size": "A4"}},
    }
    errs = check_body(doc, {"body": [{"block_index": 0, "plain": "Hi", "runs": [{"text": "Hi", "size_pt": 14}]}]})
    if not errs or "size_pt" not in errs[0]:
        print(f"FAIL self-test check_body fail case: {errs}")
        ok = False
    else:
        print("ok   check_body detects run size_pt mismatch")

    good_errs = check_body(doc, {"body": [{"block_index": 0, "plain": "Hi", "runs": [{"text": "Hi", "size_pt": 12}]}]})
    if good_errs:
        print(f"FAIL self-test check_body pass case: {good_errs}")
        ok = False
    else:
        print("ok   check_body pass case")

    xml = '<w:bookmarkStart w:id="1"/><w:bookmarkStart w:id="1"/>'
    ids = re.findall(r'w:bookmarkStart[^>]+w:id="(\d+)"', xml)
    if len(ids) == len(set(ids)):
        print("FAIL self-test bookmark duplicate detection")
        ok = False
    else:
        print("ok   unique bookmark id fail case")

    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, default=REPO / "build/linux/aioffice-cli")
    parser.add_argument("--fixture", type=Path, help="Foreign fixture (.docx/.odt)")
    parser.add_argument("--playbook", type=Path, help="qa/wave24-*.json playbook")
    parser.add_argument("--expect", type=Path, help="Override .wave24.json path")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    if not args.cli.is_file():
        raise SystemExit(f"missing cli: {args.cli} (build aioffice_cli first)")

    if args.fixture:
        target = args.fixture.resolve()
        exp_path = (args.expect or expect_path_for(target)).resolve()
        exp = load_expect(exp_path)
        errors = check_fixture(args.cli, target, exp)
    elif args.playbook:
        target = args.playbook.resolve()
        exp_path = (args.expect or expect_path_for(target)).resolve()
        exp = load_expect(exp_path)
        errors = check_playbook_case(args.cli, target, exp)
    else:
        parser.print_help()
        return 2

    decision = exp.get("decision", "?")
    if errors:
        for err in errors:
            print(f"FAIL [{decision}] {err}")
        return 1
    print(f"ok   [{decision}] {target.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
