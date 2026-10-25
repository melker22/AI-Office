#!/usr/bin/env python3
"""D11/D12: real zip inflation budget, and PDF publish that does not touch a hostile path.

Zip cases lower the caps through the environment. Values above the built-in
256 MiB / 1 GiB ceilings are ignored by the unpacker. The PDF case makes the
publish fail after rendering: the destination name is so long (251 bytes) that
QSaveFile's temporary sibling (name + ".XXXXXX") exceeds NAME_MAX, which fails
for root too (CI runs as root, where directory modes block nothing). A publish
that opened the destination early would truncate the existing file.
"""

from __future__ import annotations

import importlib.util
import json
import os
import shutil
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

    fixtures = load_fixtures()
    runtime_cm = w23.make_runtime()
    runtime = Path(runtime_cm.name)
    try:
        _zip_budget(cli, runtime, fixtures)
        _pdf_fifo(cli, runtime)
        _pdf_publish_keeps_existing(cli, runtime)
        _fsync_parent(cli, runtime)
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


def _compile_fsync_interpose(runtime: Path) -> Path:
    gcc = shutil.which("gcc")
    if not gcc:
        fail("gcc is required to build the fsync interposer")
    src = REPO / "qa" / "fsync_interpose.c"
    so = runtime / "fsync_interpose.so"
    proc = subprocess.run(
        [gcc, "-shared", "-fPIC", "-o", str(so), str(src), "-ldl"],
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0 or not so.is_file():
        fail(f"fsync interposer build failed: {proc.stderr[-400:]}")
    return so


def _save_blank(cli: str, runtime: Path, dest: Path, extra: dict) -> dict:
    env = base_env(runtime, extra)
    return call(cli, env, "document_save", {"path": str(dest)}, timeout=20)


def _fsync_parent(cli: str, runtime: Path) -> None:
    """After a successful commit, the parent directory must be fsynced.

    EIO on that fsync is a write error. EINVAL is ignored (filesystems that
    do not support directory fsync). The injection is LD_PRELOAD, so it fails
    for root the same way it fails for anyone else.
    """
    so = _compile_fsync_interpose(runtime)
    with tempfile.TemporaryDirectory(prefix="aio-fsync-") as raw:
        parent = Path(raw).resolve()
        dest = parent / "saved.aio.json"
        log = parent / "fsync.log"
        common = {
            "LD_PRELOAD": str(so),
            "AIOFFICE_FSYNC_LOG": str(log),
            "AIOFFICE_FSYNC_FAIL_DIR": str(parent),
        }

        log.write_text("", encoding="utf-8")
        einval = _save_blank(cli, runtime, dest, {**common, "AIOFFICE_FSYNC_FAIL_ERRNO": "EINVAL"})
        if einval.get("ok") is not True:
            fail(f"EINVAL directory fsync must be ignored, got {json.dumps(einval)[:500]}")
        if str(parent) not in log.read_text(encoding="utf-8").splitlines():
            fail(f"EINVAL save did not fsync {parent}; log={log.read_text()[:400]!r}")
        print("ok   directory fsync EINVAL is ignored")

        log.write_text("", encoding="utf-8")
        eio = _save_blank(
            cli, runtime, parent / "eio.aio.json", {**common, "AIOFFICE_FSYNC_FAIL_ERRNO": "EIO"}
        )
        err = str(eio.get("error") or "")
        if eio.get("ok") is not False or "Cannot write" not in err:
            fail(f"EIO directory fsync must fail the save, got {json.dumps(eio)[:500]}")
        print("ok   directory fsync EIO fails the save")

        log.write_text("", encoding="utf-8")
        ok_save = _save_blank(
            cli,
            runtime,
            parent / "ok.aio.json",
            {"LD_PRELOAD": str(so), "AIOFFICE_FSYNC_LOG": str(log)},
        )
        if ok_save.get("ok") is not True:
            fail(f"save without injected fsync error failed: {json.dumps(ok_save)[:500]}")
        if str(parent) not in log.read_text(encoding="utf-8").splitlines():
            fail(f"successful save did not fsync {parent}; log={log.read_text()[:400]!r}")
        print("ok   atomic write fsyncs the parent directory")


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
        name_max = os.pathconf(raw, "PC_NAME_MAX")
        # The target fits; QSaveFile's "<name>.XXXXXX" temporary does not.
        target = Path(raw) / ("p" * (name_max - 4 - 4) + ".pdf")
        target.write_bytes(sentinel)
        obj = call(cli, env, "document_export_pdf", {"path": str(target)}, timeout=25)
        expect_error(obj, f"Cannot write {target}", "temporary sibling cannot be created")
        got = target.read_bytes()
        if got != sentinel:
            fail(f"existing pdf was modified ({len(got)} bytes, starts {got[:24]!r})")
    print("ok   failed pdf publish leaves the existing file untouched")


if __name__ == "__main__":
    raise SystemExit(main())
