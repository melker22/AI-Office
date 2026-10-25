#!/usr/bin/env python3
"""D11/D12: real zip inflation budget, and PDF publish that does not touch a hostile path.

Zip cases lower the caps through the environment. Values above the built-in
256 MiB / 1 GiB ceilings are ignored by the unpacker. The PDF case renders
into a directory that cannot accept a new inode, so a publish that opens the
destination early truncates the existing file.
"""

from __future__ import annotations

import importlib.util
import json
import os
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import wave23_support as w23

REPO = Path(__file__).resolve().parent.parent


def fail(msg: str) -> None:
    print(f"FAIL {msg}")
    raise SystemExit(1)


def load_fixtures():
    path = REPO / "qa" / "wave23-io-fixtures.py"
    spec = importlib.util.spec_from_file_location("wave23_io_fixtures", path)
    if spec is None or spec.loader is None:
        fail(f"cannot load {path}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def base_env(runtime: Path, extra: dict | None = None) -> dict:
    env = w23.child_env(runtime, extra=extra)
    if not extra or "AIOFFICE_ZIP_ENTRY_CAP" not in extra:
        env.pop("AIOFFICE_ZIP_ENTRY_CAP", None)
    if not extra or "AIOFFICE_ZIP_TOTAL_CAP" not in extra:
        env.pop("AIOFFICE_ZIP_TOTAL_CAP", None)
    return env


def call(cli: str, env: dict, tool: str, args: dict, timeout: float) -> dict:
    try:
        proc = w23.run_cli(cli, ["call", tool, json.dumps(args)], env, timeout=timeout)
    except FileNotFoundError:
        fail(f"CLI not found: {cli}")
    except subprocess.TimeoutExpired:
        fail(f"{tool} timed out after {timeout}s; a hostile path must fail immediately")
    text = proc.stdout.decode()
    try:
        obj = json.loads(text)
    except json.JSONDecodeError:
        fail(f"{tool} stdout is not JSON: {text[:400]} stderr={proc.stderr.decode()[:300]}")
    if not isinstance(obj, dict):
        fail(f"{tool} returned {type(obj).__name__}")
    return obj


def expect_error(obj: dict, error: str, what: str) -> None:
    if obj.get("ok") is not False or obj.get("error") != error:
        fail(f"{what}: want ok=false error={error!r}, got {json.dumps(obj)[:500]}")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_wave23_io_hostile.py /path/to/aioffice-cli", file=sys.stderr)
        return 2
    cli = sys.argv[1]
    if not Path(cli).is_file():
        print(f"missing {cli}", file=sys.stderr)
        return 2
    if os.geteuid() == 0:
        fail("refusing to run as root: directory mode would not block the publish")

    fixtures = load_fixtures()
    runtime_cm = w23.make_runtime()
    runtime = Path(runtime_cm.name)
    try:
        _zip_budget(cli, runtime, fixtures)
        _pdf_fifo(cli, runtime)
        _pdf_publish_keeps_existing(cli, runtime)
    finally:
        runtime_cm.cleanup()
    print("ok   zip real budget and atomic pdf publish")
    return 0


def _zip_budget(cli: str, runtime: Path, fixtures) -> None:
    env = base_env(
        runtime,
        {
            "AIOFFICE_ZIP_ENTRY_CAP": str(fixtures.ZIP_ENTRY_CAP),
            "AIOFFICE_ZIP_TOTAL_CAP": str(fixtures.ZIP_TOTAL_CAP),
        },
    )
    chunk = b"\0" * fixtures.ZIP_UNDERDECLARED_CHUNK
    cases = {
        "underdeclared.docx": fixtures.zip_entries(
            [("a/1.bin", chunk, 0), ("a/2.bin", chunk, 0), ("a/3.bin", chunk, 0)]
        ),
        "underdeclared-entry.docx": fixtures.zip_entries(
            [("word/huge.bin", b"\0" * (fixtures.ZIP_ENTRY_CAP + 50_000), 0)]
        ),
    }
    small = fixtures.zip_entries([("pad.bin", b"\0" * 1000, 0)])
    with tempfile.TemporaryDirectory(prefix="aio-zip-budget-") as raw:
        dest = Path(raw)
        for name, blob in cases.items():
            (dest / name).write_bytes(blob)
            obj = call(cli, env, "document_open", {"path": str(dest / name)}, timeout=20)
            expect_error(obj, "archive entry too large", name)
        (dest / "underdeclared-small.docx").write_bytes(small)
        obj = call(
            cli, env, "document_open", {"path": str(dest / "underdeclared-small.docx")}, timeout=20
        )
        expect_error(obj, "DOCX missing word/document.xml", "underdeclared-small.docx")
    print("ok   zip entries that under-declare uncompressed size")


def _pdf_fifo(cli: str, runtime: Path) -> None:
    env = base_env(runtime)
    with tempfile.TemporaryDirectory(prefix="aio-pdf-fifo-") as raw:
        fifo = Path(raw) / "out.pdf"
        os.mkfifo(fifo, 0o600)
        obj = call(cli, env, "document_export_pdf", {"path": str(fifo)}, timeout=8)
        expect_error(obj, f"Cannot write {fifo}", "fifo pdf")
    print("ok   document_export_pdf rejects a fifo")


def _pdf_publish_keeps_existing(cli: str, runtime: Path) -> None:
    env = base_env(runtime)
    sentinel = b"SENTINEL-EXISTING-PDF\n"
    with tempfile.TemporaryDirectory(prefix="aio-pdf-publish-") as raw:
        parent = Path(raw) / "locked"
        parent.mkdir()
        target = parent / "existing.pdf"
        target.write_bytes(sentinel)
        parent.chmod(stat.S_IRUSR | stat.S_IXUSR | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
        try:
            obj = call(cli, env, "document_export_pdf", {"path": str(target)}, timeout=25)
            expect_error(obj, f"Cannot write {target}", "unwritable directory")
            got = target.read_bytes()
            if got != sentinel:
                fail(f"existing pdf was modified ({len(got)} bytes, starts {got[:24]!r})")
        finally:
            parent.chmod(stat.S_IRWXU)
    print("ok   failed pdf publish leaves the existing file untouched")


if __name__ == "__main__":
    raise SystemExit(main())
