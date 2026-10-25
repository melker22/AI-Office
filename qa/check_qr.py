#!/usr/bin/env python3
"""D17 judge: zbarimg reads the page-preview PNG of a QR block.

Payloads of 1, 17, 100 and 200 bytes must come back unchanged. A payload past
version 10, level M (214 bytes) must be {ok:false} with a clear error.

zbar is added to shell.nix by wave 24 T5. Until that lands, this script calls
`nix-shell -p zbar` when zbarimg is not already on PATH. LD_PRELOAD is dropped
before every child (L22: CI is root and uses a different glibc).
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PLAYBOOK = REPO / "qa" / "wave24-qr.json"
OVER_ERROR = "QR payload exceeds version 10 level M capacity"
# Byte lengths 1, 17, 100, 200. Lower case forces byte mode (not alphanumeric).
CASES: list[bytes] = [
    b"q",
    b"abcdefghijklmnopq",
    b"m" * 100,
    b"M" * 200,
]


def child_env() -> dict[str, str]:
    env = os.environ.copy()
    # An inherited interposer is built for another glibc and breaks nix zbarimg.
    env.pop("LD_PRELOAD", None)
    env["AIOFFICE_HEADLESS"] = "1"
    env["QT_QPA_PLATFORM"] = "offscreen"
    return env


def run_cmd(
    argv: list[str],
    *,
    timeout: int,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[bytes]:
    try:
        return subprocess.run(
            argv,
            capture_output=True,
            timeout=timeout,
            cwd=REPO,
            env=env if env is not None else child_env(),
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise SystemExit(f"timeout after {timeout}s: {' '.join(argv)}") from exc
    except FileNotFoundError as exc:
        raise SystemExit(f"command not found: {argv[0]}") from exc


def play(cli: Path, steps: list[dict]) -> dict:
    with tempfile.NamedTemporaryFile(
        "w", suffix=".json", prefix="wave24-qr-", dir=None, delete=False, encoding="utf-8"
    ) as handle:
        json.dump(steps, handle)
        path = Path(handle.name)
    try:
        proc = run_cmd([str(cli), "play", str(path)], timeout=120)
    finally:
        path.unlink(missing_ok=True)
    raw = proc.stdout.decode("utf-8", errors="replace").strip()
    if not raw:
        err = proc.stderr.decode("utf-8", errors="replace").strip()
        raise SystemExit(f"play failed ({proc.returncode}): {err or 'empty stdout'}")
    try:
        out = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"play bad JSON: {exc}\n{raw[:400]}") from exc
    return out


def decode_pngs(paths: list[Path]) -> list[bytes]:
    """Return one raw payload per PNG. Missing zbarimg is a failure, not a skip."""
    env = child_env()
    zbar = shutil.which("zbarimg")
    if zbar:
        decoded: list[bytes] = []
        for path in paths:
            proc = run_cmd([zbar, "--raw", "-q", str(path)], timeout=60, env=env)
            if proc.returncode != 0:
                err = proc.stderr.decode("utf-8", errors="replace").strip()
                raise SystemExit(
                    f"zbarimg failed on {path.name} (exit {proc.returncode}): {err or 'no symbol'}"
                )
            decoded.append(_one_symbol(proc.stdout, path.name))
        return decoded

    nix = shutil.which("nix-shell")
    if not nix:
        raise SystemExit("zbarimg not on PATH and nix-shell is not available")
    # One shell for every PNG. -q keeps the "scanned N" banner off stdout.
    # zbarimg exits non-zero when a symbol is missing; keep going so each PNG
    # reports its own result. The last echo is the shell's exit status.
    script_lines = ["set +e"]
    for i, path in enumerate(paths):
        script_lines.append(f"echo '@@@{i}'")
        script_lines.append(f"zbarimg --raw -q {shlex.quote(str(path))}")
    script_lines.append("exit 0")
    proc = run_cmd([nix, "-p", "zbar", "--run", "\n".join(script_lines)], timeout=180, env=env)
    text = proc.stdout
    if proc.returncode != 0 and b"@@@" not in text:
        err = proc.stderr.decode("utf-8", errors="replace").strip()
        raise SystemExit(f"zbarimg failed (exit {proc.returncode}): {err or 'no symbol'}")
    decoded: list[bytes] = []
    for i, path in enumerate(paths):
        marker = f"@@@{i}\n".encode()
        start = text.find(marker)
        if start < 0:
            raise SystemExit(f"zbarimg missing result for {path.name}")
        start += len(marker)
        end = text.find(b"@@@", start)
        chunk = text[start:] if end < 0 else text[start:end]
        decoded.append(_one_symbol(chunk, path.name))
    return decoded


def _one_symbol(raw: bytes, name: str) -> bytes:
    if raw.endswith(b"\n"):
        raw = raw[:-1]
    if b"\n" in raw:
        raise SystemExit(f"zbarimg found more than one symbol in {name}")
    if not raw:
        raise SystemExit(f"zbarimg returned an empty payload for {name}")
    return raw


def preview_steps(payload: bytes) -> list[dict]:
    return [
        {"tool": "document_new", "args": {"title": "Wave24 QR", "force": True}},
        {
            "tool": "insert_qr",
            "args": {
                "after_id": "end",
                "qr": payload.decode("ascii"),
                # The block is the full text column wide; width_mm is its height.
                # 160 mm is nearly square on A4, so page_preview does not stretch the symbol.
                "width_mm": 160,
            },
            "expect": {"ok": True},
        },
        {"tool": "page_preview", "args": {"page": 1, "zoom": 2}},
    ]


def check(cli: Path, playbook: Path) -> list[str]:
    failures: list[str] = []
    if not playbook.is_file():
        return [f"missing playbook {playbook}"]
    book = play(cli, json.loads(playbook.read_text(encoding="utf-8")))
    if not book.get("ok"):
        failures.append(f"playbook {playbook.name}: {book.get('error', book)}")

    pngs: list[Path] = []
    tmp = tempfile.TemporaryDirectory(prefix="wave24-qr-")
    try:
        for i, payload in enumerate(CASES):
            out = play(cli, preview_steps(payload))
            if not out.get("ok"):
                failures.append(f"{len(payload)} bytes: play {out.get('error', out)}")
                continue
            results = out.get("results") or []
            if len(results) < 3 or not isinstance(results[2], dict):
                failures.append(f"{len(payload)} bytes: page_preview missing")
                continue
            b64 = results[2].get("png_base64")
            if not isinstance(b64, str) or not b64:
                failures.append(f"{len(payload)} bytes: png_base64 missing")
                continue
            path = Path(tmp.name) / f"qr-{len(payload)}.png"
            path.write_bytes(base64.b64decode(b64))
            pngs.append(path)
        if pngs and len(pngs) == len(CASES):
            try:
                got = decode_pngs(pngs)
            except SystemExit as exc:
                failures.append(str(exc))
            else:
                for payload, actual in zip(CASES, got):
                    if actual != payload:
                        failures.append(
                            f"{len(payload)} bytes: zbarimg got {actual!r}, want {payload!r}"
                        )
    finally:
        tmp.cleanup()
    return failures


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--playbook", type=Path, default=PLAYBOOK)
    args = parser.parse_args(argv)
    cli = args.cli
    if not cli.is_file():
        print(f"aioffice-cli not found: {cli}", file=sys.stderr)
        return 1
    for payload in CASES:
        if len(payload) not in (1, 17, 100, 200):
            print(f"bad case length {len(payload)}", file=sys.stderr)
            return 1
    failures = check(cli, args.playbook)
    if failures:
        for item in failures:
            print(item, file=sys.stderr)
        return 1
    print("ok   wave24 qr (zbarimg)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
