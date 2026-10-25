#!/usr/bin/env python3
"""D16: LibreOffice convertTo writes in a private directory, then moves.

AIOFFICE_SOFFICE points at a fake soffice that records --outdir and writes the
file soffice would write there. A sibling named after the input stem must survive.
A binary that cannot be executed must fail immediately, not as a 120s timeout.
"""

from __future__ import annotations

import json
import os
import stat
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
