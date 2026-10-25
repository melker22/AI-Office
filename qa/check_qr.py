#!/usr/bin/env python3
"""Wave 24 D17: exported QR must decode with zbarimg to the exact payload (PNG, PDF, ODT)."""

from __future__ import annotations

import argparse
import base64
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
DEFAULT_PAYLOAD = "W24-D17-VERIFY"
DEFAULT_TIMEOUT = 300
TOOL_TIMEOUT = 60


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


def zbarimg_path() -> str | None:
    return shutil.which("zbarimg")


def zbarimg_is_missing(resolved: str | None, exc: BaseException | None = None) -> bool:
    if exc is not None and isinstance(exc, FileNotFoundError):
        return True
    return resolved is None


def decode_with_zbar(image_path: Path) -> str:
    resolved = zbarimg_path()
    if zbarimg_is_missing(resolved):
        raise SystemExit("zbarimg not on PATH: wave 24 QR judge requires zbar (shell.nix)")
    try:
        proc = run_cmd(
            [resolved, "--raw", "--quiet", str(image_path)],
            timeout=TOOL_TIMEOUT,
        )
    except FileNotFoundError as exc:
        raise SystemExit("zbarimg not on PATH: wave 24 QR judge requires zbar (shell.nix)") from exc
    if proc.returncode != 0:
        err = (proc.stderr or proc.stdout or "").strip()
        raise SystemExit(f"zbarimg failed ({proc.returncode}) on {image_path.name}: {err or 'no output'}")
    payload = proc.stdout.strip()
    if not payload:
        raise SystemExit(f"zbarimg returned empty payload for {image_path.name}")
    return payload


def assert_payload(got: str, want: str, *, label: str) -> None:
    if got != want:
        raise SystemExit(f"QR payload mismatch ({label}): got {got!r}, expected {want!r}")


def play(cli: Path, calls: list[dict]) -> list[dict]:
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, dir=REPO / "qa") as tf:
        json.dump(calls, tf)
        playbook = Path(tf.name)
    try:
        proc = run_cmd([str(cli), "play", str(playbook)], timeout=DEFAULT_TIMEOUT, env=headless_env())
        raw = proc.stdout.strip()
        if not raw:
            raise SystemExit(f"play failed ({proc.returncode}): {proc.stderr.strip() or 'empty stdout'}")
        out = json.loads(raw)
        if not out.get("ok"):
            raise SystemExit(f"play: {out.get('error', out)}")
        results = out.get("results") or []
        if len(results) != len(calls):
            raise SystemExit(f"play result count {len(results)} != {len(calls)}")
        return results
    finally:
        playbook.unlink(missing_ok=True)


def png_from_preview(results: list[dict]) -> bytes:
    for res in reversed(results):
        if not isinstance(res, dict):
            continue
        previews = res.get("previews")
        if isinstance(previews, list) and previews:
            b64 = previews[0].get("png_base64") or previews[0].get("png")
            if isinstance(b64, str) and b64:
                return base64.b64decode(b64)
        b64 = res.get("png_base64") or res.get("png")
        if isinstance(b64, str) and b64:
            return base64.b64decode(b64)
    raise SystemExit("page_preview did not return png_base64")


def pdftoppm_path() -> str | None:
    return shutil.which("pdftoppm")


def png_from_pdf(pdf: Path, out_dir: Path) -> Path:
    tool = pdftoppm_path()
    if not tool:
        raise SystemExit("pdftoppm not on PATH: need poppler-utils for PDF QR judge")
    prefix = out_dir / "page"
    proc = run_cmd(
        [tool, "-png", "-singlefile", "-f", "1", "-l", "1", str(pdf), str(prefix)],
        timeout=TOOL_TIMEOUT,
    )
    png = Path(f"{prefix}.png")
    if proc.returncode != 0 or not png.is_file():
        err = (proc.stderr or proc.stdout or "").strip()
        raise SystemExit(f"pdftoppm failed: {err or proc.returncode}")
    return png


def pngs_from_odt(odt: Path, out_dir: Path) -> list[Path]:
    found: list[Path] = []
    with zipfile.ZipFile(odt) as zf:
        for name in zf.namelist():
            if not name.lower().endswith(".png"):
                continue
            target = out_dir / Path(name).name
            target.write_bytes(zf.read(name))
            found.append(target)
    if not found:
        raise SystemExit(f"ODT {odt.name} contains no PNG (expected rasterized QR block)")
    return found


def run_qr_export_checks(cli: Path, payload: str, work: Path) -> None:
    png_path = work / "preview.png"
    odt_path = work / "export.odt"
    pdf_path = work / "export.pdf"

    calls = [
        {"tool": "document_new", "args": {"title": "Wave24 QR", "force": True}},
        {"tool": "insert_qr", "args": {"after_id": "end", "qr": payload}},
        {"tool": "page_preview", "args": {"page": 1, "zoom": 2}},
        {"tool": "document_save", "args": {"path": str(odt_path)}},
        {"tool": "document_export_pdf", "args": {"path": str(pdf_path)}},
    ]
    results = play(cli, calls)
    png_path.write_bytes(png_from_preview(results))

    assert_payload(decode_with_zbar(png_path), payload, label="page_preview PNG")

    pdf_png = png_from_pdf(pdf_path, work)
    assert_payload(decode_with_zbar(pdf_png), payload, label="PDF export")

    for idx, odt_png in enumerate(pngs_from_odt(odt_path, work)):
        assert_payload(decode_with_zbar(odt_png), payload, label=f"ODT PNG #{idx}")


def self_test() -> int:
    ok = True

    if zbarimg_is_missing(None):
        print("ok   zbarimg_is_missing(None)")
    else:
        print("FAIL self-test zbarimg_is_missing(None)")
        ok = False

    if zbarimg_is_missing("/usr/bin/zbarimg"):
        print("FAIL self-test present zbarimg path treated as missing")
        ok = False
    elif not zbarimg_is_missing("/usr/bin/zbarimg", FileNotFoundError("zbarimg")):
        print("FAIL self-test FileNotFoundError should mean zbarimg is missing")
        ok = False
    else:
        print("ok   zbarimg absence is FileNotFoundError or no PATH entry")

    try:
        assert_payload("WRONG", "RIGHT", label="selftest")
        print("FAIL self-test assert_payload should exit")
        ok = False
    except SystemExit as exc:
        if "payload mismatch" not in str(exc) or "WRONG" not in str(exc):
            print(f"FAIL self-test assert_payload message: {exc}")
            ok = False
        else:
            print("ok   payload mismatch fail case")

    golden = REPO / "qa/fixtures/wave24-zbar-golden.png"
    resolved = zbarimg_path()
    if resolved and golden.is_file():
        try:
            got = decode_with_zbar(golden)
            assert_payload(got, "SELFTEST", label="golden PNG")
            print("ok   zbarimg decodes golden fixture")
        except SystemExit as exc:
            print(f"FAIL self-test golden decode: {exc}")
            ok = False
    elif not resolved:
        try:
            decode_with_zbar(golden if golden.is_file() else Path("/nonexistent.png"))
            print("FAIL self-test decode should exit when zbarimg missing")
            ok = False
        except SystemExit as exc:
            if "zbarimg not on PATH" not in str(exc):
                print(f"FAIL self-test missing zbar message: {exc}")
                ok = False
            else:
                print("ok   missing zbarimg fail case")
    else:
        print("skip golden PNG (fixture not generated yet)")

    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, default=REPO / "build/linux/aioffice-cli")
    parser.add_argument("--payload", default=DEFAULT_PAYLOAD)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    cli = args.cli.resolve()
    if not cli.is_file():
        raise SystemExit(f"missing cli: {cli} (build aioffice_cli first)")

    with tempfile.TemporaryDirectory(prefix="aioffice-qr-") as td:
        run_qr_export_checks(cli, args.payload, Path(td))
    print(f"ok   QR decode verified for payload {args.payload!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
