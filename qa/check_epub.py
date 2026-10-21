#!/usr/bin/env python3
"""Wave 20 EPUB QA: playbook export, epubcheck (0 errors + 0 warnings), structural checks."""

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
from xml.etree import ElementTree as ET

REPO = Path(__file__).resolve().parent.parent
DEFAULT_TIMEOUT = 300
EPUBCHECK_TIMEOUT = 120
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


def load_expectations(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"invalid expectations {path}: {exc}") from exc
    if not isinstance(data, dict):
        raise SystemExit(f"invalid expectations {path}: root must be an object")
    if "epub" not in data or not isinstance(data["epub"], str) or not data["epub"].strip():
        raise SystemExit(f"invalid expectations {path}: missing non-empty string field 'epub'")
    return data


def epubcheck_is_missing(resolved: str | None, exc: BaseException | None = None) -> bool:
    if isinstance(exc, FileNotFoundError):
        return True
    return resolved is None


def parse_epubcheck_output(text: str) -> tuple[int, int, list[str]]:
    """Return (errors, warnings, detail lines)."""
    errors = 0
    warnings = 0
    details: list[str] = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if re.search(r"\bERROR\b", stripped, re.I):
            errors += 1
            details.append(stripped)
        elif re.search(r"\bWARNING\b", stripped, re.I):
            warnings += 1
            details.append(stripped)
    lower = text.lower()
    if "no errors or warnings detected" in lower:
        return 0, 0, details
    m_err = re.search(r"(\d+)\s+error", lower)
    m_warn = re.search(r"(\d+)\s+warning", lower)
    if m_err:
        errors = max(errors, int(m_err.group(1)))
    if m_warn:
        warnings = max(warnings, int(m_warn.group(1)))
    return errors, warnings, details


def run_epubcheck(epub: Path) -> tuple[int, int, str]:
    resolved = shutil.which("epubcheck")
    if epubcheck_is_missing(resolved):
        raise SystemExit("epubcheck not found (add epubcheck to shell.nix)")
    try:
        proc = run_cmd([resolved, str(epub)], timeout=EPUBCHECK_TIMEOUT)
    except FileNotFoundError as exc:
        if epubcheck_is_missing(resolved, exc):
            raise SystemExit("epubcheck not found (add epubcheck to shell.nix)") from exc
        raise
    combined = (proc.stdout or "") + (proc.stderr or "")
    err_n, warn_n, _details = parse_epubcheck_output(combined)
    if proc.returncode != 0 and err_n == 0 and warn_n == 0:
        err_n = 1
    return err_n, warn_n, combined


def mimetype_stored_first(epub: Path) -> bool:
    with zipfile.ZipFile(epub, "r") as zf:
        names = zf.namelist()
        if not names or names[0] != "mimetype":
            return False
        info = zf.getinfo("mimetype")
        if info.compress_type != zipfile.ZIP_STORED:
            return False
        if zf.read("mimetype").decode("utf-8").strip() != "application/epub+zip":
            return False
    return True


def epub_text_bundle(epub: Path) -> str:
    parts: list[str] = []
    with zipfile.ZipFile(epub, "r") as zf:
        for name in zf.namelist():
            if name.endswith((".xhtml", ".html", ".xml", ".opf")):
                try:
                    parts.append(zf.read(name).decode("utf-8", errors="replace"))
                except OSError:
                    continue
    return "\n".join(parts)


def check_structure(epub: Path, exp: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if exp.get("mimetype_stored_first", True) and not mimetype_stored_first(epub):
        errors.append("mimetype is not the first stored entry")
    bundle = epub_text_bundle(epub)
    bundle_lower = bundle.lower()

    if exp.get("nav", True):
        with zipfile.ZipFile(epub, "r") as zf:
            has_nav = any("nav.xhtml" in n.lower() or "nav.html" in n.lower() for n in zf.namelist())
        if not has_nav and "epub:type=\"toc\"" not in bundle_lower and "<nav" not in bundle_lower:
            errors.append("missing nav document")

    pkg_lang = exp.get("package_lang")
    if isinstance(pkg_lang, str) and pkg_lang:
        if f'xml:lang="{pkg_lang}"' not in bundle and f"lang=\"{pkg_lang}\"" not in bundle:
            errors.append(f"missing package/block lang {pkg_lang!r}")

    if exp.get("has_lang_span"):
        if not re.search(r'xml:lang="en-', bundle) and not re.search(r'lang="en-', bundle):
            errors.append("missing foreign-language span (en)")

    if exp.get("has_rtl"):
        if 'dir="rtl"' not in bundle:
            errors.append("missing dir=rtl paragraph")

    footnote_min = int(exp.get("footnote_aside_min", 0))
    if footnote_min > 0:
        count = len(re.findall(r'epub:type="footnote"', bundle_lower))
        count += len(re.findall(r"<aside[^>]+footnote", bundle_lower))
        if count < footnote_min:
            errors.append(f"expected at least {footnote_min} footnote aside(s), got {count}")

    chapter_min = int(exp.get("chapter_files_min", 0))
    if chapter_min > 0:
        with zipfile.ZipFile(epub, "r") as zf:
            chapters = [
                n
                for n in zf.namelist()
                if n.endswith(".xhtml") and "nav" not in n.lower() and "toc" not in n.lower()
            ]
        if len(chapters) < chapter_min:
            errors.append(f"expected at least {chapter_min} chapter XHTML file(s), got {len(chapters)}")

    for forbidden in exp.get("hide_if_omitted") or []:
        if isinstance(forbidden, str) and forbidden and forbidden in bundle:
            errors.append(f"hide_if text must be omitted: {forbidden!r}")

    th_min = int(exp.get("table_th_min", 0))
    if th_min > 0:
        th_count = len(re.findall(r"<th\b", bundle_lower))
        if th_count < th_min:
            errors.append(f"expected at least {th_min} <th>, got {th_count}")

    nested_list = exp.get("nested_list")
    if nested_list:
        if "<ol" not in bundle_lower and "<ul" not in bundle_lower:
            errors.append("missing list markup")
        if nested_list == "required" and bundle_lower.count("<li") < 2:
            errors.append("expected nested list with multiple items")

    alt_text = exp.get("image_alt_contains")
    if isinstance(alt_text, str) and alt_text:
        if alt_text not in bundle:
            errors.append(f"missing image alt containing {alt_text!r}")

    return errors


def check_expectations(epub: Path, exp: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    want = exp.get("epubcheck") or {}
    want_err = int(want.get("errors", 0))
    want_warn = int(want.get("warnings", 0))
    got_err, got_warn, raw = run_epubcheck(epub)
    if got_err != want_err:
        errors.append(f"epubcheck errors {got_err} != {want_err}")
    if got_warn != want_warn:
        errors.append(f"epubcheck warnings {got_warn} != {want_warn}")
    if got_err or got_warn:
        snippet = raw.strip().splitlines()[-8:]
        if snippet:
            errors.append("epubcheck output tail: " + " | ".join(snippet))
    errors.extend(check_structure(epub, exp))
    return errors


def write_minimal_epub(path: Path) -> None:
    mimetype = b"application/epub+zip"
    container = b"""<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>"""
    opf = b"""<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" xml:lang="pt-PT" unique-identifier="uid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="uid">urn:uuid:f47ac10b-58cc-4372-a567-0e02b2c3d479</dc:identifier>
    <dc:title>Selftest</dc:title>
    <dc:language>pt-PT</dc:language>
    <meta property="dcterms:modified">2020-01-01T00:00:00Z</meta>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="c1" href="chapter.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine><itemref idref="c1"/></spine>
</package>"""
    nav = b"""<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="pt-PT">
<head><title>Nav</title></head>
<body><nav epub:type="toc"><ol><li><a href="chapter.xhtml">Ch</a></li></ol></nav></body>
</html>"""
    chapter = b"""<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="pt-PT">
<head><title>Ch</title></head>
<body><p>Hello <span xml:lang="en-US">English</span>.</p>
<aside epub:type="footnote"><p>Fn</p></aside></body>
</html>"""
    with zipfile.ZipFile(path, "w") as zf:
        zf.writestr("mimetype", mimetype, compress_type=zipfile.ZIP_STORED)
        zf.writestr("META-INF/container.xml", container)
        zf.writestr("OEBPS/content.opf", opf)
        zf.writestr("OEBPS/nav.xhtml", nav)
        zf.writestr("OEBPS/chapter.xhtml", chapter)


def self_test() -> int:
    ok = True

    sample = (
        "Validating using EPUB version 3.3 rules.\n"
        "ERROR(RSC-005): bad thing\n"
        "WARNING(HTM-014): minor\n"
        "Check finished with errors and warnings\n"
    )
    err, warn, _ = parse_epubcheck_output(sample)
    if err < 1 or warn < 1:
        print("FAIL self-test parse_epubcheck_output counts")
        ok = False
    else:
        print("ok   parse_epubcheck_output")

    clean = "No errors or warnings detected.\n"
    err2, warn2, _ = parse_epubcheck_output(clean)
    if err2 != 0 or warn2 != 0:
        print("FAIL self-test parse clean epubcheck")
        ok = False
    else:
        print("ok   parse clean epubcheck")

    with tempfile.TemporaryDirectory(prefix="aioffice-epub-self-") as tmp:
        epub = Path(tmp) / "minimal.epub"
        write_minimal_epub(epub)
        if not mimetype_stored_first(epub):
            print("FAIL self-test mimetype_stored_first")
            ok = False
        else:
            print("ok   mimetype_stored_first")
        struct_errs = check_structure(
            epub,
            {
                "package_lang": "pt-PT",
                "has_lang_span": True,
                "footnote_aside_min": 1,
                "chapter_files_min": 1,
                "nav": True,
            },
        )
        if struct_errs:
            print("FAIL self-test structure on minimal epub:", struct_errs)
            ok = False
        else:
            print("ok   structure on minimal epub")

        if shutil.which("epubcheck"):
            e, w, _ = run_epubcheck(epub)
            if e != 0 or w != 0:
                print(f"FAIL self-test epubcheck on minimal ({e} errors, {w} warnings)")
                ok = False
            else:
                print("ok   epubcheck on minimal epub")
        else:
            print("skip epubcheck binary (not on PATH)")

    if not epubcheck_is_missing(None, FileNotFoundError("epubcheck")):
        print("FAIL self-test FileNotFoundError should mean epubcheck missing")
        ok = False
    else:
        print("ok   epubcheck absence detection")

    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, default=REPO / "build/linux/aioffice-cli")
    parser.add_argument("--playbook", type=Path, help="qa/wave20-epub-*.json playbook")
    parser.add_argument("--expect", type=Path, help="expectations .epub.json (default: playbook stem + .epub.json)")
    parser.add_argument("--epub", type=Path, help="existing EPUB (with --skip-play)")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--skip-play", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    if not args.playbook and not args.epub:
        raise SystemExit("usage: check_epub.py --playbook … | --epub … --expect … | --self-test")

    if args.playbook:
        playbook = args.playbook.resolve()
        expect_path = (args.expect or playbook.with_name(playbook.stem + ".epub.json")).resolve()
        exp = load_expectations(expect_path)
        epub = Path(exp["epub"])
        if not epub.is_absolute():
            epub = (REPO / epub).resolve()
        if not args.skip_play:
            cli = args.cli.resolve()
            if not cli.is_file():
                raise SystemExit(f"missing cli: {cli}")
            play(cli, playbook)
    else:
        expect_path = args.expect.resolve() if args.expect else None
        if not expect_path or not expect_path.is_file():
            raise SystemExit("--epub requires --expect")
        exp = load_expectations(expect_path)
        epub = args.epub.resolve()

    if not epub.is_file():
        raise SystemExit(f"EPUB not found: {epub}")

    errors = check_expectations(epub, exp)
    if errors:
        for err in errors:
            print(f"FAIL {err}")
        return 1
    print(f"ok   {epub.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
