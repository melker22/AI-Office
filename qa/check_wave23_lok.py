#!/usr/bin/env python3
"""D16: LibreOffice convertTo writes in a private directory, then moves.

AIOFFICE_SOFFICE points at a fake soffice that records --outdir and writes the
file soffice would write there. A sibling named after the input stem must survive.
A binary that cannot be executed must fail immediately, not as a 120s timeout.
"""

from __future__ import annotations

import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import wave23_support as w23

FAKE = r"""#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
args = sys.argv[1:]
outdir = None
infile = None
i = 0
while i < len(args):
    if args[i] == "--outdir" and i + 1 < len(args):
        outdir = args[i + 1]
        i += 2
        continue
    if args[i] == "--convert-to" and i + 1 < len(args):
        i += 2
        continue
    if not args[i].startswith("-"):
        infile = args[i]
    i += 1
log = os.environ.get("AIOFFICE_FAKE_SOFFICE_LOG")
if log:
    Path(log).write_text(json.dumps({"outdir": outdir, "argv": args, "infile": infile}))
ext = os.environ.get("AIOFFICE_FAKE_SOFFICE_EXT", "doc")
if outdir and infile:
    dest = Path(outdir) / (Path(infile).stem + "." + ext)
    dest.write_bytes(b"CONVERTED")
raise SystemExit(int(os.environ.get("AIOFFICE_FAKE_SOFFICE_EXIT", "0")))
"""


def fail(msg: str) -> None:
    print(f"FAIL {msg}")
    raise SystemExit(1)


def write_fake(directory: Path) -> Path:
    path = directory / "fake-soffice.py"
    path.write_text(FAKE, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IEXEC)
    return path


def _compile_interpose(runtime: Path) -> Path:
    gcc = shutil.which("gcc")
    if not gcc:
        fail("gcc is required to build the placeConverted interposer")
    src = Path(__file__).resolve().parent / "place_convert_interpose.c"
    so = runtime / "place_convert_interpose.so"
    proc = subprocess.run(
        [gcc, "-shared", "-fPIC", "-o", str(so), str(src), "-ldl"],
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0 or not so.is_file():
        fail(f"interposer build failed: {proc.stderr[-400:]}")
    return so


_PUBLISH_VERBS = (
    "fail-rename",
    "fail-renameat",
    "fail-renameat2",
    "fail-dest-open",
    "link-dest",
    "linkat-dest",
)


def _publish_attempt_on_dest(trace: str, dest: Path) -> bool:
    """True when the interposer logged a write, link, or rename onto dest."""
    want = str(dest.resolve())
    for line in trace.splitlines():
        verb, sep, path = line.partition(" ")
        if not sep or verb not in _PUBLISH_VERBS or not path:
            continue
        if path == want or os.path.realpath(path) == want:
            return True
    return False


def _tmpfile_mode_is_forwarded(so: Path, runtime: Path) -> None:
    """O_TMPFILE carries a mode even without O_CREAT. The interposer must forward it."""
    if not hasattr(os, "O_TMPFILE"):
        fail("O_TMPFILE is required to prove the interposer forwards mode")
    script = runtime / "tmpfile_mode.py"
    script.write_text(
        "import os, stat, sys\n"
        "fd = os.open(sys.argv[1], os.O_RDWR | os.O_TMPFILE, 0o600)\n"
        "mode = stat.S_IMODE(os.fstat(fd).st_mode)\n"
        "os.close(fd)\n"
        "if mode != 0o600:\n"
        "    raise SystemExit('mode %o' % mode)\n",
        encoding="utf-8",
    )
    env = os.environ.copy()
    env["LD_PRELOAD"] = str(so)
    for key in (
        "AIOFFICE_PLACE_FAIL_DEST",
        "AIOFFICE_FAIL_FIRST_EXPORT_PDF",
        "AIOFFICE_EXPORT_PDF_SEQ",
        "AIOFFICE_PLACE_LOG",
    ):
        env.pop(key, None)
    proc = subprocess.run(
        [sys.executable, str(script), str(runtime)],
        capture_output=True,
        text=True,
        env=env,
        check=False,
    )
    if proc.returncode != 0:
        fail(
            "O_TMPFILE mode was not forwarded: "
            f"rc={proc.returncode} stdout={proc.stdout[-200:]!r} stderr={proc.stderr[-300:]!r}"
        )
    print("ok   O_TMPFILE mode is forwarded")


def _publish_failure_keeps_existing(cli: str, runtime: Path, fake: Path) -> None:
    """An existing destination must survive a failed publish of converted bytes.

    Native PDF is forced to fail (first export.pdf create returns EIO) so
    LibreOffice placeConverted runs against the existing file. Renames onto
    that path fail. The old code deletes the destination before the retry.
    """
    so = _compile_interpose(runtime)
    _tmpfile_mode_is_forwarded(so, runtime)
    dest = runtime / "kept.pdf"
    dest.write_bytes(b"ORIGINAL")
    log = runtime / "place-log.txt"
    env = w23.child_env(
        runtime,
        extra={
            "AIOFFICE_SOFFICE": str(fake),
            "AIOFFICE_FAKE_SOFFICE_EXT": "pdf",
            "AIOFFICE_FAKE_SOFFICE_EXIT": "0",
            "LD_PRELOAD": str(so),
            "AIOFFICE_PLACE_FAIL_DEST": str(dest.resolve()),
            "AIOFFICE_FAIL_FIRST_EXPORT_PDF": "1",
            "AIOFFICE_EXPORT_PDF_SEQ": str(runtime / "export-pdf-seq.txt"),
            "AIOFFICE_PLACE_LOG": str(log),
            "AIOFFICE_FAKE_SOFFICE_LOG": str(runtime / "publish-soffice.json"),
        },
    )
    proc = w23.run_cli(
        cli,
        ["call", "document_export_pdf", json.dumps({"path": str(dest)})],
        env,
        timeout=20,
    )
    text = proc.stdout.decode()
    trace = log.read_text(encoding="utf-8") if log.is_file() else ""
    soffice_log = runtime / "publish-soffice.json"
    if not soffice_log.is_file():
        fail(f"fake soffice did not run; stdout={text[:400]!r} log={trace!r}")
    try:
        recorded = json.loads(soffice_log.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        fail(f"fake soffice log is not JSON: {exc}")
    if not recorded.get("argv"):
        fail(f"fake soffice log has no argv: {recorded!r}")
    if not _publish_attempt_on_dest(trace, dest):
        fail(f"interposer did not record a publish attempt on {dest}; log={trace!r}")
    if not dest.exists():
        fail(f"publish removed the existing pdf; stdout={text[:400]!r} log={trace!r}")
    if not stat.S_ISREG(dest.stat().st_mode):
        fail(f"existing pdf is no longer a regular file; log={trace!r}")
    if dest.read_bytes() != b"ORIGINAL":
        fail(
            f"existing pdf changed ({dest.read_bytes()[:40]!r}); stdout={text[:400]!r} log={trace!r}"
        )
    if proc.returncode == 0:
        fail(f"failed publish was reported as success: {text[:400]} log={trace!r}")
    print("ok   failed publish leaves the existing file untouched")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_wave23_lok.py /path/to/aioffice-cli", file=sys.stderr)
        return 2
    cli = sys.argv[1]
    if not Path(cli).is_file():
        print(f"missing {cli}", file=sys.stderr)
        return 2

    runtime_cm = w23.make_runtime()
    runtime = Path(runtime_cm.name)
    try:
        fake = write_fake(runtime)
        dest_dir = runtime / "dest"
        dest_dir.mkdir()
        sibling = dest_dir / "tmp.doc"
        sibling.write_bytes(b"KEEP-SIBLING")
        output = dest_dir / "report.doc"
        output.write_bytes(b"ORIGINAL")
        log = runtime / "soffice-log.json"
        env = w23.child_env(
            runtime,
            extra={
                "AIOFFICE_SOFFICE": str(fake),
                "AIOFFICE_FAKE_SOFFICE_LOG": str(log),
                "AIOFFICE_FAKE_SOFFICE_EXT": "doc",
                "AIOFFICE_FAKE_SOFFICE_EXIT": "0",
            },
        )
        proc = w23.run_cli(
            cli,
            ["call", "document_save", json.dumps({"path": str(output)})],
            env,
            timeout=30,
        )
        if proc.returncode != 0:
            fail(f"save .doc failed: {proc.stdout.decode()[:400]} {proc.stderr.decode()[:200]}")
        if not log.is_file():
            fail("fake soffice was not started")
        recorded = json.loads(log.read_text(encoding="utf-8"))
        outdir = recorded.get("outdir") or ""
        if os.path.realpath(outdir) == os.path.realpath(dest_dir):
            fail(f"soffice --outdir is the destination directory: {outdir}")
        if sibling.read_bytes() != b"KEEP-SIBLING":
            fail(f"sibling was clobbered: {sibling.read_bytes()[:40]!r}")
        if output.read_bytes() != b"CONVERTED":
            fail(f"output was not the converted file: {output.read_bytes()[:40]!r}")
        print("ok   convertTo uses a private directory")

        # A failing soffice must not replace the destination or the sibling.
        output.write_bytes(b"ORIGINAL")
        sibling.write_bytes(b"KEEP-SIBLING")
        log.unlink()
        env_fail = w23.child_env(
            runtime,
            extra={
                "AIOFFICE_SOFFICE": str(fake),
                "AIOFFICE_FAKE_SOFFICE_LOG": str(log),
                "AIOFFICE_FAKE_SOFFICE_EXT": "doc",
                "AIOFFICE_FAKE_SOFFICE_EXIT": "1",
            },
        )
        proc = w23.run_cli(
            cli,
            ["call", "document_save", json.dumps({"path": str(output)})],
            env_fail,
            timeout=30,
        )
        if proc.returncode == 0:
            fail("failing soffice was reported as success")
        if sibling.read_bytes() != b"KEEP-SIBLING":
            fail("failing soffice clobbered the sibling")
        if output.read_bytes() != b"ORIGINAL":
            fail("failing soffice replaced the destination")
        print("ok   failed conversion leaves the destination untouched")

        _publish_failure_keeps_existing(cli, runtime, fake)

        junk = runtime / "not-a-program"
        junk.write_bytes(b"not a program\n")
        junk.chmod(0o755)
        doc = runtime / "in.doc"
        doc.write_bytes(b"doc")
        env_start = w23.child_env(runtime, extra={"AIOFFICE_SOFFICE": str(junk)})
        started = time.monotonic()
        proc = w23.run_cli(
            cli,
            ["call", "document_open", json.dumps({"path": str(doc)})],
            env_start,
            timeout=15,
        )
        elapsed = time.monotonic() - started
        text = proc.stdout.decode()
        if proc.returncode == 0:
            fail(f"unexecutable soffice opened the file: {text[:300]}")
        if "timed out" in text:
            fail(f"failed start reported as a timeout after {elapsed:.1f}s: {text[:300]}")
        if "failed to start" not in text:
            fail(f"want 'failed to start' in {text[:300]!r}")
        if elapsed > 10:
            fail(f"failed start took {elapsed:.1f}s")
        print(f"ok   soffice failed to start in {elapsed:.2f}s")
    finally:
        runtime_cm.cleanup()
    print("ok   libreoffice convertTo")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
