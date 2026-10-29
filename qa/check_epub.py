#!/usr/bin/env python3
"""Wave 20 EPUB QA: playbook export, epubcheck (0 errors + 0 warnings), structural checks."""

from __future__ import annotations

import argparse
import inspect
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import zipfile
from pathlib import Path
from typing import Any
from xml.etree import ElementTree as ET

REPO = Path(__file__).resolve().parent.parent
DEFAULT_TIMEOUT = 300
EPUBCHECK_TIMEOUT = 120
TOOL_TIMEOUT = 60


def spawn_env(env: dict[str, str] | None = None) -> dict[str, str]:
    """Child environment. Drop inherited LD_PRELOAD so a test interposer cannot leak in."""
    out = dict(os.environ if env is None else env)
    out.pop("LD_PRELOAD", None)
    return out


def headless_env() -> dict[str, str]:
    env = spawn_env()
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
            env=spawn_env(env),
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


XHTML_NS = "http://www.w3.org/1999/xhtml"
EPUB_NS = "http://www.idpf.org/2007/ops"


def toc_anchor_texts(epub: Path) -> list[str]:
    with zipfile.ZipFile(epub) as zf:
        name = next((n for n in zf.namelist() if n.lower().endswith("nav.xhtml")), "")
        if not name:
            return []
        root = ET.fromstring(zf.read(name))
    texts: list[str] = []
    for nav in root.iter(f"{{{XHTML_NS}}}nav"):
        if nav.get(f"{{{EPUB_NS}}}type") != "toc":
            continue
        for anchor in nav.iter(f"{{{XHTML_NS}}}a"):
            texts.append("".join(anchor.itertext()).strip())
    return texts


def footnote_aside_count(epub: Path) -> tuple[int, list[str]]:
    """Count each XHTML aside once when epub:type's tokens include footnote.

    The type attribute is {http://www.idpf.org/2007/ops}type. Tokens are the
    attribute value split on whitespace. "footnotes" is not the token "footnote".
    """
    count = 0
    problems: list[str] = []
    type_attr = f"{{{EPUB_NS}}}type"
    aside_tag = f"{{{XHTML_NS}}}aside"
    with zipfile.ZipFile(epub, "r") as zf:
        names = [n for n in zf.namelist() if n.lower().endswith(".xhtml")]
        for name in names:
            try:
                root = ET.fromstring(zf.read(name))
            except ET.ParseError as exc:
                problems.append(f"{name}: not well-formed XML ({exc})")
                continue
            for aside in root.iter(aside_tag):
                raw = aside.get(type_attr)
                if raw is None:
                    continue
                if "footnote" in raw.split():
                    count += 1
    return count, problems


def aside_plain(epub: Path) -> str:
    parts: list[str] = []
    with zipfile.ZipFile(epub) as zf:
        names = sorted(
            n for n in zf.namelist() if n.lower().endswith(".xhtml") and "nav" not in n.lower()
        )
        for name in names:
            root = ET.fromstring(zf.read(name))
            for aside in root.iter(f"{{{XHTML_NS}}}aside"):
                parts.append("".join(aside.itertext()))
    return "\n".join(parts)


def png_size(data: bytes) -> tuple[int, int]:
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n":
        return 0, 0
    return int.from_bytes(data[16:20], "big"), int.from_bytes(data[20:24], "big")


def _local_tag(tag: object) -> str:
    if not isinstance(tag, str):
        return ""
    return tag.split("}")[-1] if "}" in tag else tag


def _element_has_nested_list(root: ET.Element) -> bool:
    """True when an ol or ul is a descendant of an li, including through a wrapper."""
    for li in root.iter():
        if _local_tag(li.tag) != "li":
            continue
        for desc in li.iter():
            if desc is not li and _local_tag(desc.tag) in ("ol", "ul"):
                return True
    return False


def html_has_nested_list(html: str) -> bool:
    """True when one well-formed document nests ol/ul inside an li.

    A parse error is not a nested list. Callers that have an EPUB must parse
    each XHTML file; a concatenated bundle is not one document.
    """
    try:
        root = ET.fromstring(html)
    except ET.ParseError:
        return False
    return _element_has_nested_list(root)


def _zip_name_key(name: str) -> str:
    return name.replace("\\", "/").lstrip("/").lower()


def _resolve_opf_href(opf_name: str, href: str) -> str:
    """Href relative to the OPF, without a fragment or query."""
    # Manifest hrefs are URLs: drop fragment and query, then percent-decode the path (EPUB 3.3).
    href = urllib.parse.unquote(href.split("#", 1)[0].split("?", 1)[0].strip()).replace("\\", "/")
    if not href:
        return ""
    if href.startswith("/"):
        return href.lstrip("/").lower()
    opf = opf_name.replace("\\", "/")
    base = opf.rsplit("/", 1)[0] if "/" in opf else ""
    parts = [p for p in base.split("/") if p] if base else []
    for part in href.split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            if parts:
                parts.pop()
            continue
        parts.append(part)
    return "/".join(parts).lower()


def _opf_names(zf: zipfile.ZipFile) -> list[str]:
    """OPF package documents: META-INF/container.xml rootfiles, else every *.opf entry."""
    names = {_zip_name_key(n): n for n in zf.namelist()}
    container = names.get("meta-inf/container.xml")
    if container is not None:
        try:
            root = ET.fromstring(zf.read(container))
        except ET.ParseError as exc:
            raise ValueError(f"META-INF/container.xml is not well-formed ({exc})") from exc
        found = [el.get("full-path") or "" for el in root.iter() if _local_tag(el.tag) == "rootfile"]
        found = [names.get(_zip_name_key(p)) for p in found if p]
        if not found or None in found:
            raise ValueError("META-INF/container.xml names no OPF that exists in the package")
        return found
    opfs = [n for k, n in names.items() if k.endswith(".opf")]
    if not opfs:
        raise ValueError("no OPF package document")
    return opfs


def _nav_document_names(zf: zipfile.ZipFile) -> set[str]:
    """Navigation documents = manifest items with properties="nav" (OPF metadata only, never a
    filename guess: a chapter called canvas.xhtml is content). Raises ValueError when no OPF can be
    read, so a nested-list requirement fails instead of silently counting the nav as content."""
    nav: set[str] = set()
    for name in _opf_names(zf):
        try:
            root = ET.fromstring(zf.read(name))
        except ET.ParseError as exc:
            raise ValueError(f"{name}: OPF is not well-formed ({exc})") from exc
        for item in root.iter():
            if _local_tag(item.tag) != "item":
                continue
            if "nav" not in (item.get("properties") or "").split():
                continue
            resolved = _resolve_opf_href(name, item.get("href") or "")
            if resolved:
                nav.add(resolved)
    return nav


def epub_has_nested_list(epub: Path) -> tuple[bool, str]:
    """(found, error). A list nested in a list item in some content document (not navigation)."""
    try:
        zf = zipfile.ZipFile(epub)
    except zipfile.BadZipFile as exc:
        return False, f"not a ZIP package ({exc})"
    with zf:
        try:
            nav_names = _nav_document_names(zf)
        except ValueError as exc:
            return False, str(exc)
        for name in zf.namelist():
            key = _zip_name_key(name)
            if not key.endswith(".xhtml") and not key.endswith(".html"):
                continue
            if key in nav_names:
                continue
            try:
                root = ET.fromstring(zf.read(name))
            except ET.ParseError:
                continue
            if _element_has_nested_list(root):
                return True, ""
    return False, ""


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
        # Primary subtag "en" or a regional tag such as "en-US". xml:lang= contains lang=.
        if not re.search(r'lang="en(?:-[A-Za-z0-9]+)*"', bundle):
            errors.append("missing foreign-language span (en)")

    if exp.get("has_rtl"):
        if 'dir="rtl"' not in bundle:
            errors.append("missing dir=rtl paragraph")

    footnote_min = int(exp.get("footnote_aside_min", 0))
    if footnote_min > 0:
        count, parse_problems = footnote_aside_count(epub)
        errors.extend(parse_problems)
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

    # Complement of hide_if_omitted: text whose hide_if is false (or that has none) must be present.
    for required in exp.get("text_contains") or []:
        if isinstance(required, str) and required and required not in bundle:
            errors.append(f"text must be present: {required!r}")

    for absent in exp.get("text_absent") or []:
        if isinstance(absent, str) and absent and absent in bundle:
            errors.append(f"text must be absent: {absent!r}")

    th_min = int(exp.get("table_th_min", 0))
    if th_min > 0:
        th_count = len(re.findall(r"<th\b", bundle_lower))
        if th_count < th_min:
            errors.append(f"expected at least {th_min} <th>, got {th_count}")

    nested_list = exp.get("nested_list")
    if nested_list:
        if "<ol" not in bundle_lower and "<ul" not in bundle_lower:
            errors.append("missing list markup")
        if nested_list == "required":
            found, why = epub_has_nested_list(epub)
            if why:
                errors.append(f"cannot check nested_list: {why}")
            elif not found:
                errors.append("expected a list nested inside a list item")

    alt_text = exp.get("image_alt_contains")
    if isinstance(alt_text, str) and alt_text:
        if alt_text not in bundle:
            errors.append(f"missing image alt containing {alt_text!r}")

    for tag in exp.get("lang_spans") or []:
        if not isinstance(tag, str) or not tag:
            continue
        if f'lang="{tag}"' not in bundle:
            errors.append(f"missing lang span {tag!r}")

    for tag in exp.get("lang_absent") or []:
        if isinstance(tag, str) and tag and tag in bundle:
            errors.append(f"language tag must be absent: {tag!r}")

    heading_order = exp.get("nav_heading_order") or []
    if heading_order:
        got = toc_anchor_texts(epub)
        pos = 0
        for title in heading_order:
            while pos < len(got) and got[pos] != title:
                pos += 1
            if pos >= len(got):
                errors.append(f"nav missing heading {title!r} in order; got {got}")
                break
            pos += 1

    aside_order = exp.get("aside_text_order") or []
    if aside_order:
        blob = aside_plain(epub)
        cursor = 0
        for snippet in aside_order:
            found = blob.find(snippet, cursor)
            if found < 0:
                errors.append(f"aside order missing {snippet!r} in {blob!r}")
                break
            cursor = found + len(snippet)

    if exp.get("raster_png_equal"):
        with zipfile.ZipFile(epub) as zf:
            pngs = [n for n in zf.namelist() if n.lower().endswith(".png")]
            blobs = [zf.read(n) for n in pngs]
        if len(blobs) != 2:
            errors.append(f"expected 2 raster PNGs, got {len(blobs)}")
        elif blobs[0] != blobs[1]:
            errors.append("raster PNG bytes differ (caption painted into one image)")
        else:
            width, height = png_size(blobs[0])
            if width < 200 or height < 200:
                errors.append(f"raster PNG is {width}x{height}, expected a full-size image")
            elif height == 0 or not (0.85 <= (width / height) <= 1.15):
                errors.append(f"raster aspect {width}x{height} is not square")

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


def write_footnote_token_epub(path: Path) -> None:
    """Asides whose epub:type token is footnote, plus a prefix that must not count."""
    chapter = """<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<body>
<aside epub:type="footnote"><p>A</p></aside>
<aside epub:type="rearnote footnote"><p>B</p></aside>
<aside epub:type="endnote footnote"><p>C</p></aside>
<aside epub:type="footnotes"><p>D</p></aside>
<p epub:type="footnote">Not an aside</p>
</body>
</html>"""
    nav = """<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<body><nav epub:type="toc"><ol><li><a href="chapter.xhtml">Ch</a></li></ol></nav></body>
</html>"""
    with zipfile.ZipFile(path, "w") as zf:
        zf.writestr("OEBPS/nav.xhtml", nav)
        zf.writestr("OEBPS/chapter.xhtml", chapter)


def write_list_markup_epub(path: Path, chapter: str) -> None:
    """Nav plus one chapter. Used to prove nested-list detection is per file."""
    nav = """<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml">
<body><nav><ol><li><a href="chapter.xhtml">Ch</a></li></ol></nav></body>
</html>"""
    chapter_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml">
<body>{chapter}</body>
</html>"""
    opf = """<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0">
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="c1" href="chapter.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
</package>"""
    with zipfile.ZipFile(path, "w") as zf:
        zf.writestr("OEBPS/nav.xhtml", nav)
        zf.writestr("OEBPS/chapter.xhtml", chapter_xml)
        zf.writestr("OEBPS/content.opf", opf)


def write_nav_list_epub(path: Path, nav_name: str, nav_inner: str, chapter_inner: str,
                        nav_href: str | None = None) -> None:
    """One manifest nav document plus a chapter. nav_name may omit the word nav; nav_href is the
    manifest href (default nav_name), e.g. percent-encoded."""
    nav_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml">
<body>{nav_inner}</body>
</html>"""
    chapter_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml">
<body>{chapter_inner}</body>
</html>"""
    opf = f"""<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0">
  <manifest>
    <item id="nav" href="{nav_href or nav_name}" media-type="application/xhtml+xml" properties="nav"/>
    <item id="c1" href="chapter.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
</package>"""
    with zipfile.ZipFile(path, "w") as zf:
        zf.writestr(f"OEBPS/{nav_name}", nav_xml)
        zf.writestr("OEBPS/chapter.xhtml", chapter_xml)
        zf.writestr("OEBPS/content.opf", opf)


XML_LANG = "{http://www.w3.org/XML/1998/namespace}lang"


_CONTENT_MEDIA = {"application/xhtml+xml", "text/html"}


def _manifest_items(zf: zipfile.ZipFile) -> list[dict[str, Any]]:
    """Manifest items from the OPF named by container.xml.

    Classification uses media-type and properties, not the filename. A missing or
    unreadable OPF, or a href that is not in the package, is an error.
    """
    names = {_zip_name_key(n): n for n in zf.namelist()}
    items: list[dict[str, Any]] = []
    for opf in _opf_names(zf):
        try:
            root = ET.fromstring(zf.read(opf))
        except ET.ParseError as exc:
            raise ValueError(f"{opf}: OPF is not well-formed ({exc})") from exc
        for item in root.iter():
            if _local_tag(item.tag) != "item":
                continue
            href = _resolve_opf_href(opf, item.get("href") or "")
            if not href:
                raise ValueError(f"{opf}: manifest item has no href")
            real = names.get(href)
            if real is None:
                raise ValueError(f"{opf}: manifest href {href!r} is not in the package")
            media = (item.get("media-type") or "").split(";", 1)[0].strip().lower()
            if not media:
                raise ValueError(f"{opf}: manifest item {href!r} has no media-type")
            items.append(
                {
                    "name": real,
                    "href": href,
                    "media_type": media,
                    "properties": (item.get("properties") or "").split(),
                }
            )
    return items


def content_roots(epub: Path) -> list[ET.Element]:
    """Content documents: manifest items whose media-type is XHTML or HTML, except properties=nav."""
    roots: list[ET.Element] = []
    with zipfile.ZipFile(epub) as zf:
        content = [
            item
            for item in _manifest_items(zf)
            if item["media_type"] in _CONTENT_MEDIA and "nav" not in item["properties"]
        ]
        if not content:
            raise ValueError("OPF manifest has no content document")
        for item in content:
            try:
                roots.append(ET.fromstring(zf.read(item["name"])))
            except ET.ParseError as exc:
                raise ValueError(f"{item['name']}: not well-formed XML ({exc})") from exc
    return roots


def style_css(epub: Path) -> str:
    with zipfile.ZipFile(epub) as zf:
        sheets = [item for item in _manifest_items(zf) if item["media_type"] == "text/css"]
        if not sheets:
            raise ValueError("OPF manifest has no text/css stylesheet")
        return "\n".join(zf.read(item["name"]).decode("utf-8") for item in sheets)


def element_text(el: ET.Element) -> str:
    return "".join(el.itertext())


def node_matches(el: ET.Element, spec: dict[str, Any]) -> bool:
    if spec.get("tag") and _local_tag(el.tag) != spec["tag"]:
        return False
    if "id" in spec and (el.get("id") or "") != spec["id"]:
        return False
    if "class" in spec and spec["class"] not in (el.get("class") or "").split():
        return False
    if "dir" in spec and (el.get("dir") or "") != spec["dir"]:
        return False
    if "xml_lang" in spec and (el.get(XML_LANG) or "") != spec["xml_lang"]:
        return False
    if "lang" in spec and (el.get("lang") or "") != spec["lang"]:
        return False
    if "href_contains" in spec and spec["href_contains"] not in (el.get("href") or ""):
        return False
    if "text" in spec and spec["text"] not in element_text(el):
        return False
    return True


def missing_nodes(roots: list[ET.Element], specs: list[Any]) -> list[str]:
    errors: list[str] = []
    for spec in specs:
        if not isinstance(spec, dict):
            errors.append(f"node spec must be an object, got {spec!r}")
            continue
        found = False
        for root in roots:
            for el in root.iter():
                if node_matches(el, spec):
                    found = True
                    break
            if found:
                break
        if not found:
            errors.append(f"missing element {spec}")
    return errors


def css_rule_has(css: str, selector: str, snippet: str) -> bool:
    for match in re.finditer(r"([^{}]+)\{([^{}]*)\}", css):
        selectors = [part.strip() for part in match.group(1).split(",")]
        if selector in selectors and snippet in match.group(2):
            return True
    return False


def marker_errors(styles: list[str], markers: list[Any]) -> list[str]:
    """Each marker must be a CSS string list-style-type with one trailing space inside the quotes."""
    errors: list[str] = []
    for marker in markers:
        if not isinstance(marker, str):
            errors.append(f"list marker must be a string, got {marker!r}")
            continue
        want = f'list-style-type: "{marker} "'
        if not any(want in style for style in styles):
            errors.append(f"missing list marker {want!r}")
    return errors


def li_styles(roots: list[ET.Element]) -> list[str]:
    styles: list[str] = []
    for root in roots:
        for el in root.iter():
            if _local_tag(el.tag) == "li":
                styles.append(el.get("style") or "")
    return styles


def play_json(cli: Path, playbook: Path) -> dict[str, Any]:
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
    if not isinstance(out, dict):
        raise SystemExit(f"play result is not an object: {raw[:400]}")
    return out


def rewrite_save_path(playbook: dict[str, Any], epub: Path) -> dict[str, Any]:
    data = json.loads(json.dumps(playbook))
    calls = data.get("calls")
    if not isinstance(calls, list):
        raise SystemExit("wave25 playbook needs a calls array")
    found = False
    for step in calls:
        if isinstance(step, dict) and step.get("tool") == "document_save":
            args = step.setdefault("args", {})
            if not isinstance(args, dict):
                raise SystemExit("document_save args must be an object")
            args["path"] = str(epub)
            found = True
    if not found:
        raise SystemExit("wave25 playbook has no document_save step")
    return data


def unexpected_hrefs(roots: list[ET.Element], needles: list[Any]) -> list[str]:
    """href_absent: no content-document href may contain the needle (broken fragments)."""
    hrefs: list[str] = []
    for root in roots:
        for el in root.iter():
            href = el.get("href") or ""
            if href:
                hrefs.append(href)
    errors: list[str] = []
    for needle in needles:
        if not isinstance(needle, str) or not needle:
            errors.append(f"href_absent entry must be a non-empty string, got {needle!r}")
            continue
        hit = next((href for href in hrefs if needle in href), "")
        if hit:
            errors.append(f"unexpected href containing {needle!r}: {hit!r}")
    return errors


def chapter_text_errors(epub: Path, specs: list[Any]) -> list[str]:
    """Match chapter text by the manifest href of one content document."""
    errors: list[str] = []
    if not isinstance(specs, list) or not specs:
        return ["chapter_text must be a non-empty list"]
    with zipfile.ZipFile(epub) as zf:
        items = [
            item
            for item in _manifest_items(zf)
            if item["media_type"] in _CONTENT_MEDIA and "nav" not in item["properties"]
        ]
        files = [
            (item.get("href") or "", zf.read(item["name"]).decode("utf-8"))
            for item in items
        ]
    for spec in specs:
        if not isinstance(spec, dict):
            errors.append(f"chapter_text entry must be an object, got {spec!r}")
            continue
        needle = spec.get("href_contains") or ""
        if not isinstance(needle, str) or not needle:
            errors.append(f"chapter_text href_contains must be a non-empty string, got {needle!r}")
            continue
        matches = [text for href, text in files if needle in href]
        if len(matches) != 1:
            errors.append(f"chapter file {needle!r} matched {len(matches)}")
            continue
        text = matches[0]
        for snippet in spec.get("contains") or []:
            if not isinstance(snippet, str) or not snippet or snippet not in text:
                errors.append(f"{needle} missing {snippet!r}")
        for snippet in spec.get("absent") or []:
            if isinstance(snippet, str) and snippet and snippet in text:
                errors.append(f"{needle} should not contain {snippet!r}")
    return errors


def check_wave25_package(epub: Path, spec: dict[str, Any]) -> list[str]:
    errors = check_expectations(epub, spec)
    roots = content_roots(epub)
    errors.extend(missing_nodes(roots, spec.get("nodes") or []))
    errors.extend(unexpected_hrefs(roots, spec.get("href_absent") or []))
    errors.extend(marker_errors(li_styles(roots), spec.get("list_markers") or []))
    if "chapter_text" in spec:
        errors.extend(chapter_text_errors(epub, spec.get("chapter_text")))
    css = style_css(epub)
    for rule in spec.get("css_rules") or []:
        if not isinstance(rule, dict):
            errors.append(f"css rule spec must be an object, got {rule!r}")
            continue
        selector = rule.get("selector") or ""
        snippet = rule.get("contains") or ""
        if not selector or not snippet or not css_rule_has(css, selector, snippet):
            errors.append(f"missing CSS rule {selector!r} containing {snippet!r}")
    return errors


EPUB_WAVE25 = {f"D{n}" for n in range(13, 20)}


def epub_wave25_spec(playbook: object) -> dict[str, Any] | None:
    """EPUB wave 25 checks declare checker=epub. Other qa/wave25-*.json files belong to other decisions."""
    if not isinstance(playbook, dict):
        return None
    spec = playbook.get("wave25")
    if not isinstance(spec, dict) or spec.get("checker") != "epub":
        return None
    return spec


# SystemExit is BaseException. epubcheck raises it when the validator is missing,
# and that must fail the decision instead of aborting the rest of the suite.
_PACKAGE_ERRORS = (OSError, zipfile.BadZipFile, ET.ParseError, ValueError, SystemExit)


def finish_wave25_decision(decision: str, action) -> tuple[bool, str]:
    """Run one decision's package check. A read failure fails that decision only."""
    try:
        errors = action()
    except _PACKAGE_ERRORS as exc:
        return False, f"FAIL {decision}: cannot read EPUB ({exc})"
    if not isinstance(errors, list):
        return False, f"FAIL {decision}: checker returned {errors!r}"
    if errors:
        detail = "\n".join(f"  {err}" for err in errors)
        return False, f"FAIL {decision}:\n{detail}"
    return True, f"ok   {decision}"


def wave25_validator_exit_errors() -> list[str]:
    """A validator SystemExit must fail one decision and still run the next."""
    ran: list[str] = []

    def first() -> list[str]:
        ran.append("first")
        raise SystemExit("epubcheck not found")

    def second() -> list[str]:
        ran.append("second")
        return []

    try:
        first_ok, _first_msg = finish_wave25_decision("D13", first)
        second_ok, _second_msg = finish_wave25_decision("D16", second)
    except SystemExit as exc:
        return [f"validator SystemExit aborted the suite ({exc}); ran={ran}"]
    if first_ok or not second_ok or ran != ["first", "second"]:
        return [
            "a validator SystemExit must fail that decision and still run the next "
            f"(ran={ran}, first_ok={first_ok}, second_ok={second_ok})"
        ]
    return []


def epub_cli_wiring_errors() -> list[str]:
    """self_test must forward the CLI it was given, and ctest must pass that path."""
    errors: list[str] = []
    source = inspect.getsource(self_test)
    if '"build"' in source and "aioffice-cli" in source:
        errors.append("epub self_test still falls back to the build/linux CLI")
    if "run_wave25_suite(cli)" not in source:
        errors.append("epub self_test does not pass cli into run_wave25_suite")
    cmake = (REPO / "CMakeLists.txt").read_text(encoding="utf-8")
    start = cmake.find("add_test(NAME epub_check_selftest")
    end = cmake.find("set_tests_properties(epub_check_selftest", start)
    if start < 0 or end < 0:
        errors.append("epub_check_selftest CMake block not found")
    elif '--cli "${AIOFFICE_CLI}"' not in cmake[start:end]:
        errors.append('epub_check_selftest does not pass --cli "${AIOFFICE_CLI}"')
    return errors


def run_wave25_suite(cli: Path) -> bool:
    books = sorted((REPO / "qa").glob("wave25-*.json"))
    if not books:
        print("FAIL wave25: no qa/wave25-*.json playbooks")
        return False
    if not cli.is_file():
        print(f"FAIL wave25: missing cli {cli}")
        return False
    ok = True
    seen: set[str] = set()
    for playbook_path in books:
        try:
            playbook = json.loads(playbook_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            print(f"FAIL {playbook_path.name}: {exc}")
            ok = False
            continue
        spec = epub_wave25_spec(playbook)
        if spec is None:
            continue
        decision = spec.get("decision")
        if not isinstance(decision, str) or decision not in EPUB_WAVE25:
            print(f"FAIL {playbook_path.name}: checker epub needs decision D13–D19")
            ok = False
            continue
        if decision in seen:
            print(f"FAIL {playbook_path.name}: duplicate decision {decision}")
            ok = False
            continue
        seen.add(decision)
        with tempfile.TemporaryDirectory(prefix="wave25-epub-") as tmp:
            epub = Path(tmp) / "book.epub"
            rewritten = rewrite_save_path(playbook, epub)
            script = Path(tmp) / "play.json"
            script.write_text(json.dumps(rewritten, ensure_ascii=False), encoding="utf-8")
            try:
                result = play_json(cli, script)
            except SystemExit as exc:
                print(f"FAIL {decision}: {exc}")
                ok = False
                continue
            if not result.get("ok"):
                print(f"FAIL {decision}: {result.get('error', result)}")
                ok = False
                continue
            passed, message = finish_wave25_decision(
                decision, lambda: check_wave25_package(epub, spec)
            )
            print(message)
            if not passed:
                ok = False
                continue
    missing = EPUB_WAVE25 - seen
    if missing:
        print("FAIL wave25: missing EPUB decisions " + ", ".join(sorted(missing)))
        ok = False
    return ok


def self_test(cli: Path) -> int:
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

        token_epub = Path(tmp) / "tokens.epub"
        write_footnote_token_epub(token_epub)
        token_count, token_problems = footnote_aside_count(token_epub)
        if token_problems or token_count != 3:
            print(
                "FAIL self-test footnote token count:",
                token_count,
                token_problems,
            )
            ok = False
        else:
            print("ok   footnote aside token count")

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

    flat = '<ul xmlns="http://www.w3.org/1999/xhtml"><li>A</li><li>B</li></ul>'
    nested = (
        '<ul xmlns="http://www.w3.org/1999/xhtml"><li>A<ul><li>B</li></ul></li></ul>'
    )
    if html_has_nested_list(flat):
        print("FAIL self-test nested_list flat case should fail")
        ok = False
    else:
        print("ok   nested_list flat case rejected")
    if not html_has_nested_list(nested):
        print("FAIL self-test nested_list nested case should pass")
        ok = False
    else:
        print("ok   nested_list nested case accepted")

    with tempfile.TemporaryDirectory(prefix="aioffice-epub-lists-") as tmp:
        list_exp = {"nested_list": "required", "nav": False, "mimetype_stored_first": False}
        flat_epub = Path(tmp) / "flat.epub"
        write_list_markup_epub(flat_epub, "<ul><li>A</li><li>B</li></ul>")
        flat_errs = check_structure(flat_epub, list_exp)
        if "expected a list nested inside a list item" not in flat_errs:
            print("FAIL self-test flat EPUB with nested_list required should fail:", flat_errs)
            ok = False
        else:
            print("ok   flat EPUB nested_list required rejected")
        nested_epub = Path(tmp) / "nested.epub"
        write_list_markup_epub(nested_epub, "<ul><li>A<ul><li>B</li></ul></li></ul>")
        nested_errs = check_structure(nested_epub, list_exp)
        nested_bad = [e for e in nested_errs if "nested" in e or "list markup" in e]
        if nested_bad:
            print("FAIL self-test nested EPUB should pass:", nested_errs)
            ok = False
        else:
            print("ok   nested EPUB nested_list required accepted")

        nested_nav = "<ol><li>Ch<ul><li>Sub</li></ul></li></ol>"
        flat_chapter = "<ul><li>A</li><li>B</li></ul>"
        nav_epub = Path(tmp) / "nav-nested.epub"
        write_nav_list_epub(nav_epub, "nav.xhtml", nested_nav, flat_chapter)
        nav_errs = check_structure(nav_epub, list_exp)
        if "expected a list nested inside a list item" not in nav_errs:
            print("FAIL self-test nested TOC with flat chapters should fail:", nav_errs)
            ok = False
        else:
            print("ok   nested TOC with flat chapters rejected")
        toc_epub = Path(tmp) / "toc-nested.epub"
        write_nav_list_epub(toc_epub, "toc.xhtml", nested_nav, flat_chapter)
        toc_errs = check_structure(toc_epub, list_exp)
        if "expected a list nested inside a list item" not in toc_errs:
            print("FAIL self-test properties=nav TOC with flat chapters should fail:", toc_errs)
            ok = False
        else:
            print("ok   properties=nav TOC with flat chapters rejected")
        # A content document whose name contains "nav" (canvas.xhtml) is content, not navigation.
        canvas_epub = Path(tmp) / "canvas.epub"
        write_nav_list_epub(canvas_epub, "toc.xhtml", "<ol><li>Ch</li></ol>", flat_chapter)
        with zipfile.ZipFile(canvas_epub, "a") as zf:
            zf.writestr("OEBPS/canvas.xhtml", '<?xml version="1.0" encoding="UTF-8"?>\n'
                        '<html xmlns="http://www.w3.org/1999/xhtml"><body>'
                        "<ul><li>A<ul><li>B</li></ul></li></ul></body></html>")
        canvas_errs = check_structure(canvas_epub, list_exp)
        if any("nested" in e for e in canvas_errs):
            print("FAIL self-test a nested list in canvas.xhtml should count as content:", canvas_errs)
            ok = False
        else:
            print("ok   nested list in a content file named canvas.xhtml accepted")
        # An OPF that cannot be parsed fails the requirement with a clear error.
        broken_epub = Path(tmp) / "broken-opf.epub"
        write_list_markup_epub(broken_epub, "<ul><li>A<ul><li>B</li></ul></li></ul>")
        with zipfile.ZipFile(broken_epub, "a") as zf:
            zf.writestr("META-INF/container.xml", "<container><rootfiles><rootfile full-path=\"OEBPS/bad.opf\"/>"
                        "</rootfiles></container>")
            zf.writestr("OEBPS/bad.opf", "<package><manifest><item")
        broken_errs = check_structure(broken_epub, list_exp)
        if not any(e.startswith("cannot check nested_list:") for e in broken_errs):
            print("FAIL self-test an unparseable OPF should fail nested_list with a clear error:", broken_errs)
            ok = False
        else:
            print("ok   unparseable OPF fails nested_list with a clear error")
        enc_epub = Path(tmp) / "toc-encoded.epub"
        write_nav_list_epub(enc_epub, "toc file.xhtml", nested_nav, flat_chapter, nav_href="toc%20file.xhtml")
        enc_errs = check_structure(enc_epub, list_exp)
        if "expected a list nested inside a list item" not in enc_errs:
            print("FAIL self-test percent-encoded nav href with flat chapters should fail:", enc_errs)
            ok = False
        else:
            print("ok   percent-encoded nav href with flat chapters rejected")

    mcp = (REPO / "docs" / "mcp-tools.md").read_text(encoding="utf-8")
    open_section = mcp.split("### `document_open`", 1)[1].split("### `document_save`", 1)[0]
    save_section = mcp.split("### `document_save`", 1)[1].split("### `document_export_pdf`", 1)[0]
    if "via LibreOffice when present" in open_section or "native RTF import is wave 20" in open_section:
        print("FAIL mcp-tools.md still describes RTF import as LibreOffice or future wave 20")
        ok = False
    elif "native" not in open_section.lower():
        print("FAIL mcp-tools.md document_open does not call RTF import native")
        ok = False
    else:
        print("ok   mcp-tools.md native RTF import")
    if "working path" not in save_section or "are exports" not in save_section:
        print("FAIL mcp-tools.md document_save does not separate working files from exports")
        ok = False
    else:
        print("ok   mcp-tools.md export vs working path")

    spec = (REPO / "docs" / "waves" / "wave20-epub-rtf.md").read_text(encoding="utf-8")
    if "derivado do id do documento" in spec or "não tem id de documento" not in spec or "versão 5" not in spec:
        print("FAIL wave20 D1 does not describe the UUID v5 of title and author")
        ok = False
    else:
        print("ok   wave20 D1 identifier")

    dispatch = (REPO / "src" / "mcp" / "ToolDispatch.cpp").read_text(encoding="utf-8")
    save_desc = dispatch.split('tool(QStringLiteral("document_save")', 1)[1].split("a.append(tool", 1)[0]
    if "front matter" not in save_desc or "Heading 1" not in save_desc or "working file" not in save_desc:
        print("FAIL ToolDispatch document_save description")
        ok = False
    else:
        print("ok   ToolDispatch document_save description")

    if marker_errors(["list-style-type: disc"], ["★"]):
        print("ok   list marker check rejects a disc list")
    else:
        print("FAIL list marker check should reject a disc list")
        ok = False
    if marker_errors(['list-style-type: "★ "'], ["★"]):
        print("FAIL list marker check should accept a quoted star")
        ok = False
    else:
        print("ok   list marker check accepts a quoted star")
    bare = ET.fromstring('<p xmlns="http://www.w3.org/1999/xhtml">עברית</p>')
    if not missing_nodes([bare], [{"tag": "p", "text": "עברית", "dir": "rtl"}]):
        print("FAIL dir:auto check should fail when the paragraph has no dir")
        ok = False
    else:
        print("ok   dir:auto check fails without dir")

    if epub_wave25_spec({"wave25": {"decision": "D1"}}) is not None:
        print("FAIL a wave25 playbook without checker=epub should be skipped")
        ok = False
    else:
        print("ok   non-epub wave25 playbook skipped")
    if epub_wave25_spec({"wave25": {"checker": "epub", "decision": "D13"}}) is None:
        print("FAIL checker=epub should select the playbook")
        ok = False
    else:
        print("ok   checker=epub selects the playbook")

    with tempfile.TemporaryDirectory(prefix="aioffice-epub-manifest-") as tmp:
        classified = Path(tmp) / "classified.epub"
        container = (
            '<?xml version="1.0"?>\n'
            '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
            '<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>'
            "</rootfiles></container>"
        )
        opf = (
            '<?xml version="1.0"?>\n'
            '<package xmlns="http://www.idpf.org/2007/opf" version="3.0"><manifest>'
            '<item id="decoy" href="chapter.xhtml" media-type="image/png"/>'
            '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'
            '<item id="body" href="notes.bin" media-type="application/xhtml+xml"/>'
            '<item id="css" href="style.css" media-type="text/css"/>'
            "</manifest></package>"
        )
        body = (
            '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml">'
            '<body><p id="real">REAL</p></body></html>'
        )
        decoy = (
            '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml">'
            '<body><p id="decoy">DECOY</p></body></html>'
        )
        nav = (
            '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml">'
            "<body><nav><p>NAV</p></nav></body></html>"
        )
        with zipfile.ZipFile(classified, "w") as zf:
            zf.writestr("META-INF/container.xml", container)
            zf.writestr("OEBPS/content.opf", opf)
            zf.writestr("OEBPS/chapter.xhtml", decoy)
            zf.writestr("OEBPS/nav.xhtml", nav)
            zf.writestr("OEBPS/notes.bin", body)
            zf.writestr("OEBPS/style.css", ".sReal { color: #010203; }\n")
        try:
            roots = content_roots(classified)
            texts = [element_text(root) for root in roots]
            css = style_css(classified)
        except (OSError, zipfile.BadZipFile, ET.ParseError, ValueError) as exc:
            print(f"FAIL manifest classification: {exc}")
            ok = False
        else:
            if texts != ["REAL"] or "color: #010203" not in css:
                print(f"FAIL manifest classification used a name heuristic: {texts!r}")
                ok = False
            else:
                print("ok   manifest media-type selects content and css")
        broken = Path(tmp) / "broken.epub"
        with zipfile.ZipFile(broken, "w") as zf:
            zf.writestr("META-INF/container.xml", container)
            zf.writestr("OEBPS/content.opf", "<package><manifest>")
        try:
            content_roots(broken)
        except ValueError:
            print("ok   unparseable OPF fails content classification")
        else:
            print("FAIL unparseable OPF should fail content classification")
            ok = False

    exit_errors = wave25_validator_exit_errors()
    if exit_errors:
        print("FAIL self-test wave25 validator exit: " + "; ".join(exit_errors))
        ok = False
    else:
        print("ok   wave25 suite continues after a validator SystemExit")

    wiring = epub_cli_wiring_errors()
    if wiring:
        print("FAIL self-test cli wiring: " + "; ".join(wiring))
        ok = False
    else:
        print("ok   epub self-test uses the passed CLI")

    if not run_wave25_suite(cli):
        ok = False

    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, default=REPO / "build/linux/aioffice-cli")
    parser.add_argument("--playbook", type=Path, help="qa/wave20-epub-*.json playbook")
    parser.add_argument("--expect", type=Path, help="expectations .epub.json (default: playbook stem + .epub.json)")
    parser.add_argument("--epub", type=Path, help="existing EPUB (with --skip-play)")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--wave25-only", action="store_true", help="run qa/wave25-*.json EPUB checks")
    parser.add_argument("--skip-play", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test(args.cli)
    if args.wave25_only:
        return 0 if run_wave25_suite(args.cli.resolve()) else 1

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
