#!/usr/bin/env python3
"""Wave 20 RTF export QA: playbook save, RTF markers, optional LibreOffice ODT round-trip."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parent.parent
DEFAULT_TIMEOUT = 300
SOFFICE_TIMEOUT = 120


def headless_env() -> dict[str, str]:
    env = os.environ.copy()
    env["AIOFFICE_HEADLESS"] = "1"
    env["QT_QPA_PLATFORM"] = "offscreen"
    return env


def run_cmd(
    argv: list[str],
    *,
    timeout: int,
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


def play(cli: Path, playbook: Path) -> None:
    proc = run_cmd(
        [str(cli), "play", str(playbook)],
        timeout=DEFAULT_TIMEOUT,
        env=headless_env(),
    )
    raw = proc.stdout.strip()
    if not raw:
        raise SystemExit(f"play failed ({proc.returncode}): {proc.stderr.strip() or 'empty stdout'}")
    try:
        out = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"play bad JSON: {exc}\n{raw[:400]}") from exc
    if not out.get("ok"):
        raise SystemExit(f"play: {out.get('error', out)}")


def load_expect(playbook: Path) -> dict[str, Any]:
    expect_path = playbook.with_suffix(".rtf.json")
    if not expect_path.is_file():
        raise SystemExit(f"missing expectation file: {expect_path}")
    with expect_path.open(encoding="utf-8") as handle:
        return json.load(handle)


def read_odt_text(odt_path: Path) -> str:
    with zipfile.ZipFile(odt_path) as zf:
        content = zf.read("content.xml").decode("utf-8", errors="replace")
    return re.sub(r"<[^>]+>", " ", content)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--playbook", type=Path, required=True)
    args = parser.parse_args()

    playbook = args.playbook if args.playbook.is_absolute() else REPO / args.playbook
    expect = load_expect(playbook)
    play(args.cli, playbook)

    rtf_path = Path(expect["rtf"])
    if not rtf_path.is_file():
        raise SystemExit(f"RTF not written: {rtf_path}")

    rtf_text = rtf_path.read_text(encoding="utf-8", errors="replace")
    for needle in expect.get("rtf_contains") or []:
        if needle not in rtf_text:
            raise SystemExit(f"RTF missing marker: {needle!r}")

    soffice = shutil.which("soffice")
    odt_snippets = expect.get("odt_text_contains") or []
    if odt_snippets:
        if not soffice:
            print("skip: soffice not on PATH (ODT text checks skipped)", file=sys.stderr)
        else:
            with tempfile.TemporaryDirectory(prefix="wave20-rtf-") as tmp:
                out_dir = Path(tmp)
                proc = run_cmd(
                    [
                        soffice,
                        "--headless",
                        "--convert-to",
                        "odt",
                        "--outdir",
                        str(out_dir),
                        str(rtf_path),
                    ],
                    timeout=SOFFICE_TIMEOUT,
                )
                if proc.returncode != 0:
                    raise SystemExit(
                        f"soffice convert failed ({proc.returncode}): {proc.stderr.strip() or proc.stdout.strip()}"
                    )
                odt_files = list(out_dir.glob("*.odt"))
                if not odt_files:
                    raise SystemExit("soffice produced no ODT")
                body = read_odt_text(odt_files[0])
                for snippet in odt_snippets:
                    if snippet not in body:
                        raise SystemExit(f"ODT text missing: {snippet!r}")

    print("ok")


if __name__ == "__main__":
    main()
