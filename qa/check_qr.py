#!/usr/bin/env python3
"""Wave 24 D17: QR payloads decode with zbarimg (page preview, PDF, ODT).

Payloads of 1, 17, 100 and 200 bytes must round-trip unchanged from page_preview
PNG. A payload past version 10, level M must be {ok:false} with a clear error
(see qa/wave24-qr.json). With --playbook, the playbook expect is checked first;
without --playbook, PNG/PDF/ODT export paths are judged for --payload.

zbar is in shell.nix; until CI has it on PATH, `nix-shell -p zbar` is used for
multi-PNG batch decode. LD_PRELOAD is dropped before every child (L22).
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
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PLAYBOOK = REPO / "qa" / "wave24-qr.json"
DEFAULT_PAYLOAD = "W24-D17-VERIFY"
DEFAULT_TIMEOUT = 300
TOOL_TIMEOUT = 60
# Byte lengths 1, 17, 100, 200. Lower case forces byte mode (not alphanumeric).
CASES: list[bytes] = [
    b"q",
    b"abcdefghijklmnopq",
    b"m" * 100,
    b"M" * 200,
]


def child_env() -> dict[str, str]:
    env = os.environ.copy()
    env.pop("LD_PRELOAD", None)
    env["AIOFFICE_HEADLESS"] = "1"
    env["QT_QPA_PLATFORM"] = "offscreen"
    return env


def run_cmd(
    argv: list[str],
    *,
    timeout: int,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
    text: bool = False,
) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(
            argv,
            capture_output=True,
            text=text,
            timeout=timeout,
            cwd=cwd or REPO,
            env=env if env is not None else child_env(),
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise SystemExit(f"timeout after {timeout}s: {' '.join(argv)}") from exc
    except FileNotFoundError as exc:
        raise SystemExit(f"command not found: {argv[0]}") from exc


def play_document(cli: Path, steps: list[dict], *, timeout: int = 120) -> dict:
    with tempfile.NamedTemporaryFile(
        "w", suffix=".json", prefix="wave24-qr-", dir=None, delete=False, encoding="utf-8"
    ) as handle:
        json.dump(steps, handle)
        path = Path(handle.name)
    try:
        proc = run_cmd([str(cli), "play", str(path)], timeout=timeout)
    finally:
        path.unlink(missing_ok=True)
    raw = proc.stdout.decode("utf-8", errors="replace").strip()
    if not raw:
        err = proc.stderr.decode("utf-8", errors="replace").strip()
        raise SystemExit(f"play failed ({proc.returncode}): {err or 'empty stdout'}")
    try:
        return json.loads(raw)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"play bad JSON: {exc}\n{raw[:400]}") from exc


def play_calls(cli: Path, calls: list[dict], *, timeout: int = DEFAULT_TIMEOUT) -> list[dict]:
    out = play_document(cli, calls, timeout=timeout)
    if not out.get("ok"):
        raise SystemExit(f"play: {out.get('error', out)}")
    results = out.get("results") or []
    if len(results) != len(calls):
        raise SystemExit(f"play result count {len(results)} != {len(calls)}")
    return results


def zbarimg_path() -> str | None:
    return shutil.which("zbarimg")


def zbarimg_is_missing(resolved: str | None, exc: BaseException | None = None) -> bool:
    if exc is not None and isinstance(exc, FileNotFoundError):
        return True
    return resolved is None


def _one_symbol(raw: bytes, name: str) -> bytes:
    if raw.endswith(b"\n"):
        raw = raw[:-1]
    if b"\n" in raw:
        raise SystemExit(f"zbarimg found more than one symbol in {name}")
    if not raw:
        raise SystemExit(f"zbarimg returned an empty payload for {name}")
    return raw


def decode_pngs(paths: list[Path]) -> list[bytes]:
    """Return one raw payload per PNG. Missing zbarimg tries nix-shell -p zbar."""
    env = child_env()
    zbar = zbarimg_path()
    if zbar:
        decoded: list[bytes] = []
        for path in paths:
            proc = run_cmd([zbar, "--raw", "-q", str(path)], timeout=TOOL_TIMEOUT, env=env)
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


def decode_with_zbar(image_path: Path) -> str:
    resolved = zbarimg_path()
    if zbarimg_is_missing(resolved):
        got = decode_pngs([image_path])
        return got[0].decode("utf-8", errors="replace")
    try:
        proc = run_cmd(
            [resolved, "--raw", "--quiet", str(image_path)],
            timeout=TOOL_TIMEOUT,
            text=True,
        )
    except SystemExit as exc:
        if "command not found" in str(exc):
            raise SystemExit("zbarimg not on PATH: wave 24 QR judge requires zbar (shell.nix)") from exc
        raise
    if proc.returncode != 0:
        err = (proc.stderr or proc.stdout or "").strip()
        raise SystemExit(f"zbarimg failed ({proc.returncode}) on {image_path.name}: {err or 'no output'}")
    payload = (proc.stdout or "").strip()
    if not payload:
        raise SystemExit(f"zbarimg returned empty payload for {image_path.name}")
    return payload


def assert_payload(got: str, want: str, *, label: str) -> None:
    if got != want:
        raise SystemExit(f"QR payload mismatch ({label}): got {got!r}, expected {want!r}")


def preview_steps(payload: bytes) -> list[dict]:
    return [
        {"tool": "document_new", "args": {"title": "Wave24 QR", "force": True}},
        {
            "tool": "insert_qr",
            "args": {
                "after_id": "end",
                "qr": payload.decode("ascii"),
                "width_mm": 160,
            },
            "expect": {"ok": True},
        },
        {"tool": "page_preview", "args": {"page": 1, "zoom": 2}},
    ]


def check_playbook(cli: Path, playbook: Path) -> list[str]:
    failures: list[str] = []
    if not playbook.is_file():
        return [f"missing playbook {playbook}"]
    book = play_document(cli, json.loads(playbook.read_text(encoding="utf-8")), timeout=DEFAULT_TIMEOUT)
    if not book.get("ok"):
        failures.append(f"playbook {playbook.name}: {book.get('error', book)}")

    pngs: list[Path] = []
    tmp = tempfile.TemporaryDirectory(prefix="wave24-qr-")
    try:
        for payload in CASES:
            out = play_document(cli, preview_steps(payload))
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
        text=True,
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
    results = play_calls(cli, calls)
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
            if "zbarimg not on PATH" not in str(exc) and "nix-shell" not in str(exc):
                print(f"FAIL self-test missing zbar message: {exc}")
                ok = False
            else:
                print("ok   missing zbarimg fail case")
    else:
        print("skip golden PNG (fixture not generated yet)")

    return 0 if ok else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, default=None)
    parser.add_argument("--playbook", type=Path, default=None)
    parser.add_argument("--payload", default=DEFAULT_PAYLOAD)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    if args.playbook is not None:
        if args.cli is None:
            print("--cli is required with --playbook", file=sys.stderr)
            return 1
        cli = args.cli.resolve()
        if not cli.is_file():
            print(f"aioffice-cli not found: {cli}", file=sys.stderr)
            return 1
        for payload in CASES:
            if len(payload) not in (1, 17, 100, 200):
                print(f"bad case length {len(payload)}", file=sys.stderr)
                return 1
        failures = check_playbook(cli, args.playbook)
        if failures:
            for item in failures:
                print(item, file=sys.stderr)
            return 1
        print("ok   wave24 qr (zbarimg)")
        return 0

    cli = (args.cli or REPO / "build/linux/aioffice-cli").resolve()
    if not cli.is_file():
        raise SystemExit(f"missing cli: {cli} (build aioffice_cli first)")

    with tempfile.TemporaryDirectory(prefix="aioffice-qr-") as td:
        run_qr_export_checks(cli, args.payload, Path(td))
    print(f"ok   QR decode verified for payload {args.payload!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
