#!/usr/bin/env python3
"""Round-trip checks: legacy wave12 dir/align, foreign fixtures, and exported packages without native JSON."""

from __future__ import annotations

import argparse
import glob
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_CLI = REPO / "build/linux/aioffice-cli"


def run_playbook(cli: Path, calls: list[dict]) -> list[dict]:
    """Run tools in one headless session via `aioffice play` (CLI `call` starts a new session each time)."""
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, dir=REPO / "qa") as tf:
        json.dump(calls, tf)
        playbook = Path(tf.name)
    try:
        try:
            proc = subprocess.run(
                [str(cli), "play", str(playbook)],
                capture_output=True,
                text=True,
                cwd=REPO,
                timeout=300,
            )
        except subprocess.TimeoutExpired as exc:
            raise SystemExit("aioffice-cli timed out after 300s") from exc
        raw = proc.stdout.strip()
        if not raw:
            raise SystemExit(f"play failed: {proc.stderr.strip() or proc.returncode}")
        try:
            out = json.loads(raw)
        except json.JSONDecodeError as e:
            raise SystemExit(f"play bad JSON: {e}\n{raw[:500]}") from e
        if not out.get("ok"):
            raise SystemExit(f"play: {out.get('error', out)}")
        results = out.get("results") or []
        if len(results) != len(calls):
            raise SystemExit(f"play result count {len(results)} != {len(calls)}")
        return results
    finally:
        playbook.unlink(missing_ok=True)


def document_from_result(res: dict) -> dict:
    doc = res.get("document")
    if isinstance(doc, dict):
        return doc
    return res


def plain_block(body: list, index: int) -> str:
    if index >= len(body):
        return ""
    runs = body[index].get("runs") or []
    return "".join(r.get("text") or "" for r in runs)


def run_props_match(got: dict, want: dict) -> bool:
    for key, val in want.items():
        if key == "text":
            if (got.get("text") or "") != val:
                return False
        elif key in ("bold", "italic", "underline", "strike", "superscript", "subscript"):
            if bool(got.get(key)) != bool(val):
                return False
        elif key in ("field", "field_arg", "xref_kind"):
            if (got.get(key) or "") != (val or ""):
                return False
    return True


def revision_key(r: dict) -> tuple:
    return (
        r.get("kind") or "",
        r.get("author") or "",
        int(r.get("block_index", -1)),
        int(r.get("start", -1)),
        int(r.get("end", -1)),
        r.get("text") or "",
    )


def match_revision(got: dict, want: dict, block_ids: list[str]) -> None:
    bi = want.get("block_index", 0)
    if bi >= len(block_ids):
        raise SystemExit(f"block_index {bi} out of range")
    if got.get("kind") != want.get("kind"):
        raise SystemExit(f"revision kind {got.get('kind')!r} != {want.get('kind')!r}")
    if (got.get("author") or "") != (want.get("author") or ""):
        raise SystemExit(f"revision author {got.get('author')!r} != {want.get('author')!r}")
    if got.get("block_id") != block_ids[bi]:
        raise SystemExit(f"revision block_id {got.get('block_id')!r} != {block_ids[bi]!r}")
    for field in ("start", "end"):
        if int(got.get(field, -999)) != int(want.get(field, -999)):
            raise SystemExit(f"revision {field} {got.get(field)} != {want.get(field)}")
    if "text" in want and (got.get("text") or "") != want["text"]:
        raise SystemExit(f"revision text {got.get('text')!r} != {want['text']!r}")
    if "payload" in want and (got.get("payload") or "") != want["payload"]:
        raise SystemExit(f"revision payload {got.get('payload')!r} != {want['payload']!r}")
    if "deleted_runs" in want:
        got_runs = got.get("deleted_runs") or []
        want_runs = want["deleted_runs"]
        if len(got_runs) != len(want_runs):
            raise SystemExit(f"deleted_runs len {len(got_runs)} != {len(want_runs)}")
        for g, w in zip(got_runs, want_runs):
            if not run_props_match(g, w):
                raise SystemExit(f"deleted_run mismatch: {g!r} vs {w!r}")


def find_revision_id(revs: list, block_ids: list[str], rej: dict) -> str:
    for r in revs:
        if r.get("kind") != rej.get("kind"):
            continue
        bi = int(rej.get("block_index", 0))
        if bi >= len(block_ids) or r.get("block_id") != block_ids[bi]:
            continue
        if int(r.get("start", -1)) != int(rej.get("start", -1)):
            continue
        if rej.get("author") and r.get("author") != rej.get("author"):
            continue
        return r["id"]
    raise SystemExit("reject_delete: revision not found")


def compare_expect(doc: dict, revs: list, expect_path: Path, fixture_path: Path, cli: Path) -> None:
    exp = json.loads(expect_path.read_text())
    body = doc.get("body") or []
    block_ids = [b.get("id") or "" for b in body]

    for want_b in exp.get("body") or []:
        bi = int(want_b["block_index"])
        if bi >= len(body):
            raise SystemExit(f"body block {bi} missing")
        if plain_block(body, bi) != want_b.get("plain", ""):
            raise SystemExit(
                f"block {bi} plain {plain_block(body, bi)!r} != {want_b.get('plain')!r}"
            )
        if "hide_if" in want_b and (body[bi].get("hide_if") or "") != want_b.get("hide_if"):
            raise SystemExit(
                f"block {bi} hide_if {(body[bi].get('hide_if') or '')!r} != {want_b.get('hide_if')!r}"
            )
        got_runs = body[bi].get("runs") or []
        want_runs = want_b.get("runs") or []
        if len(got_runs) != len(want_runs):
            raise SystemExit(f"block {bi} run count {len(got_runs)} != {len(want_runs)}")
        for g, w in zip(got_runs, want_runs):
            if not run_props_match(g, w):
                raise SystemExit(f"block {bi} run {g!r} != {w!r}")
        if "hide_if" in want_b and (body[bi].get("hide_if") or "") != want_b.get("hide_if"):
            raise SystemExit(
                f"block {bi} hide_if {body[bi].get('hide_if')!r} != {want_b.get('hide_if')!r}"
            )

    want_revs = exp.get("revisions") or []
    if len(revs) != len(want_revs):
        raise SystemExit(f"revision count {len(revs)} != {len(want_revs)}")
    got_sorted = sorted(revs, key=lambda r: (r.get("kind"), r.get("start"), r.get("author") or ""))
    want_sorted = sorted(want_revs, key=lambda r: (r.get("kind"), r.get("start"), r.get("author") or ""))
    for got, want in zip(got_sorted, want_sorted):
        match_revision(got, want, block_ids)

    rej = exp.get("reject_delete")
    if rej:
        fp = str(fixture_path.resolve())
        open_res, rev_res = run_playbook(
            cli,
            [
                {"tool": "document_open", "args": {"path": fp}},
                {"tool": "revision_list", "args": {}},
            ],
        )
        body0 = document_from_result(open_res).get("body") or []
        block_ids2 = [b.get("id") or "" for b in body0]
        rid = find_revision_id(rev_res.get("revisions") or [], block_ids2, rej)
        results = run_playbook(
            cli,
            [
                {"tool": "document_open", "args": {"path": fp}},
                {"tool": "revision_reject", "args": {"id": rid}},
                {"tool": "document_get", "args": {}},
            ],
        )
        body2 = document_from_result(results[-1]).get("body") or []
        for bi, plain in enumerate(exp.get("after_reject_plain") or []):
            if plain_block(body2, bi) != plain:
                raise SystemExit(f"after reject block {bi}: {plain_block(body2, bi)!r} != {plain!r}")

    accept_plain = exp.get("accept_all_plain")
    if accept_plain is not None:
        fp = str(fixture_path.resolve())
        results = run_playbook(
            cli,
            [
                {"tool": "document_open", "args": {"path": fp}},
                {"tool": "revision_accept_all", "args": {}},
                {"tool": "document_get", "args": {}},
            ],
        )
        body3 = document_from_result(results[-1]).get("body") or []
        for bi, plain in enumerate(accept_plain):
            if plain_block(body3, bi) != plain:
                raise SystemExit(f"after accept_all block {bi}: {plain_block(body3, bi)!r} != {plain!r}")


def strip_native_json(path: Path, out_path: Path) -> None:
    if path.suffix.lower() == ".docx":
        with zipfile.ZipFile(path, "r") as zin:
            names = [n for n in zin.namelist() if n != "word/aioffice.json"]
            buf = io.BytesIO()
            with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as zout:
                for name in names:
                    zout.writestr(name, zin.read(name))
        out_path.write_bytes(buf.getvalue())
    elif path.suffix.lower() == ".odt":
        content = zipfile.ZipFile(path).read("content.xml").decode("utf-8")
        content = re.sub(
            r"<meta:user-defined[^>]*meta:name=\"aioffice:extra\"[^>]*>.*?</meta:user-defined>",
            "",
            content,
            flags=re.DOTALL,
        )
        meta = zipfile.ZipFile(path).read("meta.xml").decode("utf-8") if "meta.xml" in zipfile.ZipFile(path).namelist() else ""
        if meta:
            meta = re.sub(
                r"<meta:user-defined[^>]*meta:name=\"aioffice:extra\"[^>]*>.*?</meta:user-defined>",
                "",
                meta,
                flags=re.DOTALL,
            )
        with zipfile.ZipFile(path, "r") as zin:
            buf = io.BytesIO()
            with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as zout:
                for info in zin.infolist():
                    data = zin.read(info.filename)
                    if info.filename == "content.xml":
                        data = content.encode("utf-8")
                    elif info.filename == "meta.xml" and meta:
                        data = meta.encode("utf-8")
                    if info.filename == "mimetype":
                        zout.writestr(info, data, compress_type=zipfile.ZIP_STORED)
                    else:
                        zout.writestr(info, data)
        out_path.write_bytes(buf.getvalue())
    else:
        raise SystemExit(f"strip_native_json: unsupported {path}")


def revisions_from_aio(path: Path) -> list[dict]:
    data = json.loads(path.read_text())
    revs = data.get("revisions") or []
    if not revs and "document" in data:
        revs = data["document"].get("revisions") or []
    return revs


def compare_revision_lists(got: list[dict], want: list[dict]) -> None:
    def norm(r: dict) -> dict:
        return {
            "kind": r.get("kind"),
            "author": r.get("author"),
            "start": r.get("start"),
            "end": r.get("end"),
            "text": r.get("text") or "",
            "payload": r.get("payload") or "",
            "deleted_runs": [
                {k: v for k, v in (dr or {}).items() if k in ("text", "bold", "italic", "underline", "strike")}
                for dr in (r.get("deleted_runs") or [])
            ],
        }

    g = sorted((norm(r) for r in got), key=lambda x: (x["kind"], x["start"], x["author"] or ""))
    w = sorted((norm(r) for r in want), key=lambda x: (x["kind"], x["start"], x["author"] or ""))
    if len(g) != len(w):
        raise SystemExit(f"foreign revision count {len(g)} != {len(w)}")
    for a, b in zip(g, w):
        for key in ("kind", "author", "start", "end", "text"):
            if a[key] != b[key]:
                raise SystemExit(f"foreign revision {key}: {a[key]!r} != {b[key]!r}")
        if len(a["deleted_runs"]) != len(b["deleted_runs"]):
            raise SystemExit(f"foreign deleted_runs len mismatch")
        for dr_g, dr_w in zip(a["deleted_runs"], b["deleted_runs"]):
            if not run_props_match(dr_g, dr_w):
                raise SystemExit(f"foreign deleted_runs: {dr_g!r} != {dr_w!r}")


def foreign_roundtrip(cli: Path, aio_path: Path, fmt: str, tmp: Path) -> None:
    want = revisions_from_aio(aio_path)
    exported = tmp / f"exported.{fmt}"
    stripped = tmp / f"foreign.{fmt}"
    aio = str(aio_path.resolve())
    run_playbook(
        cli,
        [
            {"tool": "document_open", "args": {"path": aio}},
            {"tool": "document_save", "args": {"path": str(exported)}},
        ],
    )
    strip_native_json(exported, stripped)
    results = run_playbook(
        cli,
        [
            {"tool": "document_open", "args": {"path": str(stripped)}},
            {"tool": "revision_list", "args": {}},
        ],
    )
    got = results[-1].get("revisions") or []
    compare_revision_lists(got, want)
    print(f"foreign {fmt} round-trip OK", aio_path.name)


def expectation_beside(fixture: Path) -> Path:
    candidates = []
    for suf in (".revisions.json", ".fields.json"):
        candidates.append(fixture.with_suffix(suf))
        candidates.append(Path(str(fixture) + suf))
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SystemExit(f"missing expectation JSON beside {fixture}")


def fixture_test(cli: Path, fixture: Path) -> None:
    expect = expectation_beside(fixture)
    fp = str(fixture.resolve())
    open_res, get_res, rev_res = run_playbook(
        cli,
        [
            {"tool": "document_open", "args": {"path": fp}},
            {"tool": "document_get", "args": {}},
            {"tool": "revision_list", "args": {}},
        ],
    )
    doc = document_from_result(get_res)
    revs = rev_res.get("revisions") or []
    compare_expect(doc, revs, expect, fixture, cli)
    print("fixture OK", fixture.name)


def legacy_wave_check(base: str) -> None:
    def types(path):
        with open(path) as f:
            d = json.load(f)
        doc = d.get("document") or d
        body = doc.get("body") or []
        page = (doc.get("meta") or {}).get("page") or doc.get("page") or {}
        return [b.get("type") for b in body], page, body, doc

    docs = {}
    for name in ("from_json", "from_odt", "from_docx"):
        t, p, body, doc = types(os.path.join(base, f"{name}.json"))
        docs[name] = doc
        print(name, t, p)
        for need in ("table", "list-item", "image", "break"):
            if need not in t:
                raise SystemExit(f"{name} missing {need}: {t}")
    print("round-trip types OK")

    def ctl_attrs(body):
        out = []
        for i, b in enumerate(body):
            d = b.get("dir") or ""
            a = b.get("align") or ""
            if d or a in ("start", "end"):
                out.append((i, d, a))
        return out

    base_body = docs["from_json"].get("body") or []
    ctl = ctl_attrs(base_body)
    if ctl:
        for name in ("from_odt", "from_docx"):
            body = docs[name].get("body") or []
            for i, want_dir, want_align in ctl:
                if i >= len(body):
                    raise SystemExit(f"{name} body shorter than source at index {i}")
                got = body[i]
                if (got.get("dir") or "") != want_dir:
                    raise SystemExit(f"{name} body[{i}] dir mismatch")
                if want_align and (got.get("align") or "") != want_align:
                    raise SystemExit(f"{name} body[{i}] align mismatch")
        print("round-trip dir/align OK")

    base_revs = docs["from_json"].get("revisions") or []
    base_bm = len(docs["from_json"].get("bookmarks") or [])
    base_rev = len(base_revs)
    need_ins = any(r.get("kind") == "insert" for r in base_revs)
    need_del = any(r.get("kind") == "delete" for r in base_revs)
    if base_bm or base_rev:
        for name in ("from_odt", "from_docx"):
            if base_bm and len(docs[name].get("bookmarks") or []) != base_bm:
                raise SystemExit(f"{name} bookmark count mismatch")
            if base_rev and len(docs[name].get("revisions") or []) != base_rev:
                raise SystemExit(f"{name} revision count mismatch")
        print("round-trip bookmarks/revisions OK")

        def native_xml(path, member):
            with zipfile.ZipFile(path) as z:
                return z.read(member).decode("utf-8", "replace")

        office_files = glob.glob(os.path.join(base, "*.odt")) + glob.glob(os.path.join(base, "*.docx"))
        for path in office_files:
            if path.endswith(".odt"):
                xml = native_xml(path, "content.xml")
                if base_bm and "text:bookmark-start" not in xml:
                    raise SystemExit(f"{path} missing native ODT bookmark markup")
                if need_ins and "text:change-start" not in xml and "text:changed-region" not in xml:
                    raise SystemExit(f"{path} missing native ODT insert markup")
                print("native ODT markup OK", path)
            elif path.endswith(".docx"):
                xml = native_xml(path, "word/document.xml")
                if base_bm and "w:bookmarkStart" not in xml:
                    raise SystemExit(f"{path} missing native DOCX bookmark markup")
                if need_ins and "<w:ins" not in xml:
                    raise SystemExit(f"{path} missing native DOCX insert markup")
                if need_del and "<w:del" not in xml:
                    raise SystemExit(f"{path} missing native DOCX delete markup")
                print("native DOCX markup OK", path)


def main() -> None:
    ap = argparse.ArgumentParser(description="AI Office round-trip QA helper")
    ap.add_argument("--cli", type=Path, default=DEFAULT_CLI)
    ap.add_argument("--fixture", type=Path, action="append", help="Foreign fixture (.docx/.odt)")
    ap.add_argument(
        "--foreign",
        nargs=2,
        metavar=("AIO.json", "FORMAT"),
        action="append",
        help="Export AIO, strip native JSON, reimport; FORMAT is odt or docx",
    )
    ap.add_argument(
        "legacy_base",
        nargs="?",
        help="Legacy mode: directory with from_json.json (wave12 roundtrip artifacts)",
    )
    args = ap.parse_args()

    if not args.cli.is_file():
        raise SystemExit(f"CLI not found: {args.cli} (build aioffice_cli first)")

    ran = False
    if args.fixture:
        for fx in args.fixture:
            fixture_test(args.cli, fx)
        ran = True

    if args.foreign:
        with tempfile.TemporaryDirectory(prefix="aioffice-foreign-") as td:
            for aio, fmt in args.foreign:
                foreign_roundtrip(args.cli, Path(aio), fmt.lower(), Path(td))
        ran = True

    if args.legacy_base:
        legacy_wave_check(args.legacy_base)
        ran = True

    if not ran:
        ap.print_help()
        raise SystemExit(2)


if __name__ == "__main__":
    main()
