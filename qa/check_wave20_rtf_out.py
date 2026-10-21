#!/usr/bin/env python3
"""Wave 20 RTF export QA.

Always checks brace balance and that {\\rtf1 is the root group through EOF,
then reimports with the native reader. LibreOffice RTF→ODT runs when soffice
is on PATH. Pass --interop (or set "interop": true in the .rtf.json) to
require soffice; a missing binary is a failure, not a skip.
"""

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

INTEROP_SOFFICE_MISSING = (
    "soffice not on PATH: interop RTF check requires LibreOffice"
)


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


def rtf_structure_error(text: str) -> str | None:
    """Return an error when braces are unbalanced or text sits outside {\\rtf1.

    \\\\, \\{ and \\} are escaped literals. \\'hh is one character. \\bin N
    skips N raw bytes so embedded braces do not count as groups.
    """
    n = len(text)
    i = 0
    if text.startswith("\ufeff"):
        i = 1
    while i < n and text[i] in " \t\r\n":
        i += 1
    if i >= n or text[i] != "{":
        if i < n:
            return "RTF text outside the root group"
        return "RTF missing {\\rtf1 root group"
    if not text.startswith("{\\rtf", i):
        return "RTF missing {\\rtf1 root group"

    depth = 0
    root_closed = False

    def outside(kind: str) -> str:
        if kind == "close":
            return f"RTF braces are not balanced (unexpected }} at offset {i})"
        return "RTF root group {\\rtf1 closes before end of file (text outside the root group)"

    while i < n:
        c = text[i]
        if c == "\\":
            if i + 1 >= n:
                return "RTF braces are not balanced (truncated control at end of file)"
            nxt = text[i + 1]
            if nxt in "{}\\":
                i += 2
                if root_closed:
                    return outside("text")
                continue
            if nxt == "'":
                i = i + 4 if i + 3 < n else n
                if root_closed and i <= n:
                    # hex byte is still inside the file; only flag if we already closed
                    # and this was not trailing whitespace (it isn't).
                    return outside("text")
                continue
            i += 1
            if i < n and text[i].isalpha():
                start = i
                while i < n and text[i].isalpha():
                    i += 1
                word = text[start:i]
                num_start = i
                if i < n and text[i] == "-":
                    i += 1
                while i < n and text[i].isdigit():
                    i += 1
                if word == "bin":
                    raw = text[num_start:i]
                    if raw in ("", "-"):
                        return "RTF malformed \\bin"
                    try:
                        nbytes = int(raw)
                    except ValueError:
                        return "RTF malformed \\bin"
                    if nbytes < 0:
                        return "RTF malformed \\bin"
                    if i < n and text[i] == " ":
                        i += 1
                    i += nbytes
                    if i > n:
                        return "RTF \\bin exceeds file"
                    if root_closed:
                        return outside("text")
                    continue
                if i < n and text[i] == " ":
                    i += 1
                if root_closed:
                    return outside("text")
                continue
            i += 1
            if root_closed:
                return outside("text")
            continue
        if c == "{":
            if root_closed:
                return outside("text")
            depth += 1
            i += 1
            continue
        if c == "}":
            if depth == 0 or root_closed:
                return outside("close")
            depth -= 1
            i += 1
            if depth == 0:
                root_closed = True
            continue
        if root_closed and c not in " \t\r\n":
            return outside("text")
        i += 1

    if not root_closed or depth != 0:
        return f"RTF braces are not balanced (depth {depth} at end of file)"
    return None


def flatten_text(value: Any) -> str:
    chunks: list[str] = []

    def walk(node: Any) -> None:
        if isinstance(node, str):
            chunks.append(node)
        elif isinstance(node, dict):
            for item in node.values():
                walk(item)
        elif isinstance(node, list):
            for item in node:
                walk(item)

    walk(value)
    return "\n".join(chunks)


def native_reimport_text(cli: Path, rtf_path: Path) -> str:
    proc = run_cmd(
        [str(cli), "call", "document_open", json.dumps({"path": str(rtf_path.resolve())})],
        timeout=60,
        env=headless_env(),
    )
    raw = proc.stdout.strip()
    if not raw:
        raise SystemExit(
            f"document_open transport failure ({proc.returncode}): "
            f"{proc.stderr.strip() or 'empty stdout'}"
        )
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"document_open bad JSON: {exc}\n{raw[:400]}") from exc
    if not isinstance(data, dict) or "ok" not in data:
        raise SystemExit(f"document_open transport failure: missing ok field: {raw[:200]}")
    if not data.get("ok"):
        raise SystemExit(f"native reimport failed: {data.get('error', data)}")
    document = data.get("document")
    if not isinstance(document, dict):
        raise SystemExit("native reimport returned no document")
    return flatten_text(document)


def require_snippets(body: str, snippets: list[Any], *, where: str) -> None:
    for snippet in snippets:
        if not isinstance(snippet, str) or snippet not in body:
            raise SystemExit(f"{where} missing text: {snippet!r}")


def libreoffice_odt_text(soffice: str, rtf_path: Path) -> str:
    with tempfile.TemporaryDirectory(prefix="wave20-rtf-") as tmp:
        out_dir = Path(tmp)
        try:
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
        except FileNotFoundError as exc:
            raise FileNotFoundError(soffice) from exc
        if proc.returncode != 0:
            raise SystemExit(
                f"soffice convert failed ({proc.returncode}): "
                f"{proc.stderr.strip() or proc.stdout.strip()}"
            )
        odt_files = list(out_dir.glob("*.odt"))
        if not odt_files:
            raise SystemExit("soffice produced no ODT")
        try:
            return read_odt_text(odt_files[0])
        except (OSError, zipfile.BadZipFile, KeyError) as exc:
            raise SystemExit(f"soffice ODT is not readable: {exc}") from exc


def check_soffice(rtf_path: Path, snippets: list[Any], *, interop: bool) -> None:
    soffice = shutil.which("soffice")
    if not soffice:
        if interop:
            raise SystemExit(INTEROP_SOFFICE_MISSING)
        return
    try:
        body = libreoffice_odt_text(soffice, rtf_path)
    except FileNotFoundError:
        if interop:
            raise SystemExit(INTEROP_SOFFICE_MISSING) from None
        return
    require_snippets(body, snippets, where="ODT")


def check_rtf_file(
    rtf_path: Path,
    *,
    cli: Path | None,
    snippets: list[Any],
    markers: list[Any],
    interop: bool,
) -> None:
    if not rtf_path.is_file():
        raise SystemExit(f"RTF not written: {rtf_path}")
    rtf_text = rtf_path.read_text(encoding="utf-8", errors="replace")
    for needle in markers:
        if not isinstance(needle, str) or needle not in rtf_text:
            raise SystemExit(f"RTF missing marker: {needle!r}")
    structure = rtf_structure_error(rtf_text)
    if structure:
        raise SystemExit(structure)
    if cli is not None:
        plain = native_reimport_text(cli, rtf_path)
        require_snippets(plain, snippets, where="native reimport")
    elif snippets and not interop:
        # Structure-only invocation (--rtf without --cli) does not reimport.
        pass
    check_soffice(rtf_path, snippets, interop=interop)


def self_test() -> int:
    broken = (
        "{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0\n"
        "{\\*\\listtable{\\list\\listtemplateid1{\\listlevel\\levelnfc23\\leveljc0\\levelstartat1"
        "\\leveltext\\'01\\u-3913 ?;}{\\levelnumbers;}}\\listid1}}\n"
        "{\\*\\listoverridetable{\\listoverride\\listid1\\listoverridecount0\\ls1}}\n"
        "\\viewkind4\n"
        "\\pard Titulo RTF\\par\n"
        "}\n"
    )
    err = rtf_structure_error(broken)
    if err is None or "closes before end of file" not in err:
        print(f"FAIL self-test did not catch unbalanced listtable: {err!r}", file=sys.stderr)
        return 1
    print(f"ok   unbalanced listtable: {err}")

    good = (
        "{\\rtf1\\ansi\\uc1\n"
        "{\\*\\listtable{\\list\\listtemplateid1{\\listlevel\\levelnfc23\\leveljc0\\levelstartat1"
        "{\\leveltext\\'01\\u-3913 ?;}{\\levelnumbers;}}\\listid1}}\n"
        "{\\*\\listoverridetable{\\listoverride\\listid1\\listoverridecount0\\ls1}}\n"
        "\\pard Titulo RTF \\{brace\\} English\\par\n"
        "}"
    )
    err = rtf_structure_error(good)
    if err:
        print(f"FAIL self-test rejected balanced RTF: {err}", file=sys.stderr)
        return 1
    print("ok   balanced listtable")

    extra = "{\\rtf1 hi}}"
    err = rtf_structure_error(extra)
    if not err or "not balanced" not in err:
        print(f"FAIL self-test extra close: {err!r}", file=sys.stderr)
        return 1
    print(f"ok   extra close: {err}")

    unclosed = "{\\rtf1 hi"
    err = rtf_structure_error(unclosed)
    if not err or "not balanced" not in err:
        print(f"FAIL self-test unclosed: {err!r}", file=sys.stderr)
        return 1
    print(f"ok   unclosed: {err}")

    ahead = "x{\\rtf1 hi}"
    err = rtf_structure_error(ahead)
    if not err or "outside the root group" not in err:
        print(f"FAIL self-test leading text: {err!r}", file=sys.stderr)
        return 1
    print(f"ok   leading text: {err}")

    if "skip" in INTEROP_SOFFICE_MISSING.lower():
        print(f"FAIL interop message looks like a skip: {INTEROP_SOFFICE_MISSING}", file=sys.stderr)
        return 1
    print(f"ok   interop without soffice: {INTEROP_SOFFICE_MISSING}")

    # word-foreign.rtf is a short hand sample whose root group is left open.
    for name in ("wave20-rtf-word.rtf", "wave20-rtf-lo.rtf"):
        path = REPO / "qa" / "fixtures" / name
        sample = path.read_text(encoding="latin-1")
        err = rtf_structure_error(sample)
        if err:
            print(f"FAIL fixture {name}: {err}", file=sys.stderr)
            return 1
        print(f"ok   fixture {name}")

    hostile = (REPO / "qa" / "fixtures" / "wave20-rtf-hostile-malformed.rtf").read_text(encoding="latin-1")
    err = rtf_structure_error(hostile)
    if not err or "not balanced" not in err:
        print(f"FAIL hostile malformed was accepted: {err!r}", file=sys.stderr)
        return 1
    print(f"ok   hostile malformed: {err}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path)
    parser.add_argument("--playbook", type=Path)
    parser.add_argument("--rtf", type=Path, help="validate an existing RTF instead of running a playbook")
    parser.add_argument(
        "--interop",
        action="store_true",
        help="require soffice; missing LibreOffice fails instead of skipping the ODT check",
    )
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    if args.rtf and args.playbook:
        raise SystemExit("pass either --playbook or --rtf")

    if args.rtf:
        rtf_path = args.rtf if args.rtf.is_absolute() else REPO / args.rtf
        cli = args.cli.resolve() if args.cli else None
        if cli is not None and not cli.is_file():
            raise SystemExit(f"missing cli: {cli}")
        check_rtf_file(rtf_path, cli=cli, snippets=[], markers=[], interop=args.interop)
        print("ok")
        return 0

    if not args.playbook or not args.cli:
        raise SystemExit("usage: check_wave20_rtf_out.py --cli … --playbook … | --rtf … | --self-test")

    cli = args.cli.resolve()
    if not cli.is_file():
        raise SystemExit(f"missing cli: {cli}")
    playbook = args.playbook if args.playbook.is_absolute() else REPO / args.playbook
    expect = load_expect(playbook)
    interop = bool(args.interop or expect.get("interop"))
    play(cli, playbook)

    rtf_path = Path(expect["rtf"])
    if not rtf_path.is_absolute():
        rtf_path = (REPO / rtf_path).resolve()
    check_rtf_file(
        rtf_path,
        cli=cli,
        snippets=list(expect.get("odt_text_contains") or []),
        markers=list(expect.get("rtf_contains") or []),
        interop=interop,
    )
    print("ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
