#!/usr/bin/env python3
"""Wave 27 (T2) judge: the shared XHTML block renderer (D4), the self-contained
HTML5 export_html (D5), and multi-paragraph notes on the EPUB/HTML export side (D2).

It drives the real CLI with a generated playbook, then parses the exported HTML and
the exported EPUB content documents as XML. Run without the wave-27 fixes it fails:
export_html emits a bare `<p>` soup (no <style>, no notes, no evaluated fields, no
data: images) and a note whose runs carry U+2029 is flattened into a single <p>.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from xml.etree import ElementTree as ET

REPO = Path(__file__).resolve().parent.parent
XHTML_NS = "http://www.w3.org/1999/xhtml"
NS = "{" + XHTML_NS + "}"
XML_LANG = "{http://www.w3.org/XML/1998/namespace}lang"
DEFAULT_TIMEOUT = 300
EPUBCHECK_TIMEOUT = 120

# One 1x1 PNG (the same bytes qa/wave20-epub.json uses for its figure).
PNG_1X1_B64 = (
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="
)

# The note below must export as three paragraphs, the middle one bold.
NOTE_PARAGRAPHS = ["PARA-ONE", "PARA-TWO", "PARA-THREE"]


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
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            argv,
            capture_output=True,
            text=True,
            timeout=timeout,
            cwd=REPO,
            env=spawn_env(env),
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise SystemExit(f"timeout after {timeout}s: {' '.join(argv)}") from exc


def play(cli: Path, playbook: Path) -> dict:
    proc = run_cmd([str(cli), "play", str(playbook)], timeout=DEFAULT_TIMEOUT, env=headless_env())
    raw = proc.stdout.strip()
    if not raw:
        raise SystemExit(f"play failed ({proc.returncode}): {proc.stderr.strip() or 'empty stdout'}")
    try:
        out = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"play bad JSON: {exc}\n{raw[:400]}") from exc
    if not isinstance(out, dict):
        raise SystemExit(f"play result is not an object: {raw[:400]}")
    if not out.get("ok"):
        raise SystemExit(f"play: {out.get('error', out)}")
    return out


def _local(tag: object) -> str:
    if not isinstance(tag, str):
        return ""
    return tag.split("}")[-1] if "}" in tag else tag


def _classes(el: ET.Element) -> list[str]:
    return (el.get("class") or "").split()


def _text(el: ET.Element) -> str:
    return "".join(el.itertext())


def _iter_local(root: ET.Element, name: str) -> list[ET.Element]:
    return [el for el in root.iter() if _local(el.tag) == name]


def _direct(el: ET.Element, name: str) -> list[ET.Element]:
    return [c for c in el if _local(c.tag) == name]


def _attr_int(value: str | None) -> int:
    try:
        return int(value) if value is not None else 0
    except (TypeError, ValueError):
        return 0


def build_playbook(html_path: Path, epub_path: Path) -> list:
    return [
        {"tool": "document_new", "args": {"title": "Wave27 HTML", "force": True}},
        {"tool": "document_set_title", "args": {"title": "Wave27 HTML", "author": "QA"}},
        {
            "tool": "create_style",
            "args": {"name": "Corpo Especial", "kind": "paragraph", "bold": True, "size_pt": 14},
        },
        {
            "tool": "create_style",
            "args": {"name": "Realce", "kind": "character", "italic": True, "color": "#AA0000"},
        },
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "start",
                "blocks": [
                    {"id": "h1", "type": "heading", "level": 1, "runs": [{"text": "Capítulo Um"}]},
                    {"id": "pStyle", "type": "paragraph", "style": "Corpo Especial", "runs": [{"text": "PAR-STYLE"}]},
                    {"id": "pChar", "type": "paragraph", "runs": [{"text": "CHARSTYLE", "char_style": "Realce"}]},
                    {"id": "pField", "type": "paragraph"},
                    {
                        "id": "pHide",
                        "type": "paragraph",
                        "hide_if": "1",
                        "runs": [{"text": "MUST-NOT-APPEAR-HIDEIF"}],
                    },
                    {
                        "id": "pSec",
                        "type": "paragraph",
                        "section_hidden": True,
                        "runs": [{"text": "MUST-NOT-APPEAR-SECTION"}],
                    },
                    {
                        "id": "pLang",
                        "type": "paragraph",
                        "runs": [{"text": "normal "}, {"text": "english", "lang": "en"}],
                    },
                    {"id": "pRtl", "type": "paragraph", "dir": "rtl", "lang": "ar", "runs": [{"text": "مرحبا"}]},
                    {
                        "id": "t1",
                        "type": "table",
                        "header": True,
                        "rows": [
                            [{"runs": [{"text": "H1"}]}, {"runs": [{"text": "H2"}]}, {"runs": [{"text": "H3"}]}],
                            [{"runs": [{"text": "wide"}], "col_span": 2}, {"runs": [{"text": "x"}]}],
                            [{"runs": [{"text": "tall"}], "row_span": 2}, {"runs": [{"text": "y"}]}, {"runs": [{"text": "z"}]}],
                            [{"runs": [{"text": "y2"}]}, {"runs": [{"text": "z2"}]}],
                        ],
                    },
                    {"id": "l1", "type": "list-item", "list_kind": "bullet", "level": 1, "runs": [{"text": "Pai"}]},
                    {"id": "l2", "type": "list-item", "list_kind": "bullet", "level": 2, "runs": [{"text": "Filho"}]},
                    {"id": "l3", "type": "list-item", "list_kind": "bullet", "level": 1, "runs": [{"text": "Tio"}]},
                    {
                        "id": "img1",
                        "type": "image",
                        "alt": "Marca de prova",
                        "caption": "Legenda de prova",
                        "width_mm": 20,
                        "image_mime": "image/png",
                        "image_b64": PNG_1X1_B64,
                    },
                    {"id": "pRef", "type": "paragraph", "runs": [{"text": "ref", "note_id": "fnMulti"}]},
                    {
                        "id": "fnMulti",
                        "type": "footnote",
                        "runs": [
                            {"text": NOTE_PARAGRAPHS[0]},
                            {"text": "\u2029"},
                            {"text": NOTE_PARAGRAPHS[1], "bold": True},
                            {"text": "\u2029"},
                            {"text": NOTE_PARAGRAPHS[2]},
                        ],
                    },
                ],
            },
        },
        {
            "tool": "insert_field",
            "args": {
                "id": "pField",
                "offset": 0,
                "kind": "if",
                "arg": {"cond": "1 == 1", "then": "FIELD-YES", "else": "FIELD-NO"},
            },
        },
        {"tool": "export_html", "args": {"path": str(html_path)}, "expect": {"ok": True, "path": str(html_path)}},
        # document_save to .html goes through the same renderer as export_html (wave 27 D5).
        {"tool": "document_save", "args": {"path": str(html_path.with_name("saved.html"))}, "expect": {"ok": True}},
        {
            "tool": "document_save",
            "args": {"path": str(epub_path)},
            "expect": {"ok": True, "format": "epub", "chapters": 1},
        },
    ]


def check_html(html: str) -> list[str]:
    errors: list[str] = []
    try:
        root = ET.fromstring(html)
    except ET.ParseError as exc:
        return [f"export_html is not well-formed XML: {exc}"]
    if _local(root.tag) != "html":
        errors.append(f"root element is {_local(root.tag)!r}, expected 'html'")
    if not (root.get("lang") or root.get(XML_LANG)):
        errors.append("html element has no lang")
    if not html.lstrip().startswith("<?xml"):
        errors.append("export_html has no XML declaration")
    if "<!DOCTYPE html>" not in html:
        errors.append("export_html has no <!DOCTYPE html>")

    metas = _iter_local(root, "meta")
    if not any((m.get("charset") or "").lower() == "utf-8" for m in metas):
        errors.append("no <meta charset=\"utf-8\">")
    titles = _iter_local(root, "title")
    if not titles or _text(titles[0]) != "Wave27 HTML":
        errors.append(f"<title> is not the metadata title: {[_text(t) for t in titles]!r}")

    css = "\n".join(_text(s) for s in _iter_local(root, "style"))
    if ".sCorpo-Especial" not in css:
        errors.append("no CSS rule for the paragraph style class .sCorpo-Especial")
    if ".sRealce" not in css:
        errors.append("no CSS rule for the character style class .sRealce")
    if not any("sCorpo-Especial" in _classes(el) for el in root.iter()):
        errors.append("no element carries the paragraph style class sCorpo-Especial")
    if not any("sRealce" in _classes(el) for el in root.iter()):
        errors.append("no element carries the character style class sRealce")

    full = _text(root)
    if "FIELD-YES" not in full:
        errors.append("the evaluated if-field text FIELD-YES is missing")
    if "FIELD-NO" in full:
        errors.append("the unevaluated if-field branch FIELD-NO leaked into the output")
    for bad in ("MUST-NOT-APPEAR-HIDEIF", "MUST-NOT-APPEAR-SECTION"):
        if bad in full:
            errors.append(f"omitted block text must not appear: {bad!r}")
    if not any((el.get("lang") or "") == "en" for el in root.iter()):
        errors.append("no foreign-language (lang=en) span")
    if not any((el.get("dir") or "") == "rtl" for el in root.iter()):
        errors.append("no dir=rtl block")

    imgs = _iter_local(root, "img")
    if not imgs:
        errors.append("no <img> in the exported HTML")
    elif not any((img.get("src") or "").startswith("data:image/") for img in imgs):
        errors.append("the figure image is not embedded as a data: URI")
    if not any("Legenda de prova" == _text(fc) for fc in _iter_local(root, "figcaption")):
        errors.append("missing figure caption")

    ths = _iter_local(root, "th")
    if not ths:
        errors.append("table header cell <th> missing")
    tds = _iter_local(root, "td")
    if not any(_attr_int(td.get("colspan")) >= 2 for td in tds):
        errors.append("no td with colspan>=2")
    if not any(_attr_int(td.get("rowspan")) >= 2 for td in tds):
        errors.append("no td with rowspan>=2")

    nested = False
    for li in _iter_local(root, "li"):
        if any(_local(d.tag) in ("ul", "ol") for d in li.iter() if d is not li):
            nested = True
            break
    if not nested:
        errors.append("no nested list (ul/ol inside an li)")

    body = _iter_local(root, "body")
    sections = [s for s in _iter_local(root, "section") if "notes" in _classes(s)]
    if not sections:
        errors.append("notes are not in a <section class=\"notes\">")
    else:
        if body:
            kids = list(body[0])
            if not kids or _local(kids[-1].tag) != "section":
                errors.append("the notes section is not the last element of <body>")
        aside = next((a for a in sections[0].iter() if _local(a.tag) == "aside" and a.get("id") == "fnMulti"), None)
        if aside is None:
            errors.append("the fnMulti aside is missing from the notes section")
        else:
            ps = _direct(aside, "p")
            if len(ps) != 3:
                errors.append(f"note fnMulti should have 3 paragraphs, got {len(ps)}")
            else:
                if NOTE_PARAGRAPHS[0] not in _text(ps[0]):
                    errors.append("note paragraph 1 text missing")
                if not any(_local(x.tag) == "strong" and NOTE_PARAGRAPHS[1] in _text(x) for x in ps[1].iter()):
                    errors.append("note paragraph 2 is not bold")
                if NOTE_PARAGRAPHS[2] not in _text(ps[2]):
                    errors.append("note paragraph 3 text missing")
            if not any(_local(a.tag) == "a" and (a.get("href") or "").startswith("#") for a in aside.iter()):
                errors.append("the note has no back link to its reference")

    if not any(
        _local(a.tag) == "a" and (a.get("href") or "") == "#fnMulti" for a in root.iter()
    ):
        errors.append("the in-text note reference does not link to #fnMulti")
    return errors


def epub_content_roots(epub: Path) -> list[ET.Element]:
    roots: list[ET.Element] = []
    with zipfile.ZipFile(epub, "r") as zf:
        for name in zf.namelist():
            low = name.lower()
            if not (low.endswith(".xhtml") or low.endswith(".html")):
                continue
            if "nav" in low.split("/")[-1]:
                continue
            roots.append(ET.fromstring(zf.read(name)))
    return roots


def check_epub(epub: Path) -> list[str]:
    errors: list[str] = []
    try:
        roots = epub_content_roots(epub)
    except (OSError, zipfile.BadZipFile, ET.ParseError) as exc:
        return [f"cannot read the exported EPUB: {exc}"]
    if not roots:
        return ["the exported EPUB has no content document"]

    aside = None
    for root in roots:
        for el in root.iter():
            if _local(el.tag) == "aside" and el.get("id") == "fnMulti":
                aside = el
    if aside is None:
        errors.append("the fnMulti aside is missing from the EPUB chapter")
    else:
        ps = _direct(aside, "p")
        if len(ps) != 3:
            errors.append(f"EPUB note fnMulti should have 3 paragraphs, got {len(ps)}")
        else:
            if NOTE_PARAGRAPHS[0] not in _text(ps[0]):
                errors.append("EPUB note paragraph 1 text missing")
            if not any(_local(x.tag) == "strong" and NOTE_PARAGRAPHS[1] in _text(x) for x in ps[1].iter()):
                errors.append("EPUB note paragraph 2 is not bold")
            if NOTE_PARAGRAPHS[2] not in _text(ps[2]):
                errors.append("EPUB note paragraph 3 text missing")

    # D4: the EPUB chapter uses the same shared renderer as export_html, so the
    # paragraph style class and the table spans must render identically.
    blob = "".join(_text(r) for r in roots)
    any_class = any("sCorpo-Especial" in _classes(el) for r in roots for el in r.iter())
    if not any_class:
        errors.append("the EPUB chapter lost the paragraph style class sCorpo-Especial")
    tds = [el for r in roots for el in _iter_local(r, "td")]
    if not any(_attr_int(td.get("colspan")) >= 2 for td in tds):
        errors.append("the EPUB chapter table lost colspan")
    if not any(_attr_int(td.get("rowspan")) >= 2 for td in tds):
        errors.append("the EPUB chapter table lost rowspan")
    if "FIELD-YES" not in blob:
        errors.append("the EPUB chapter lost the evaluated if-field text")
    return errors


# A selection export must not drag in orphan notes that are outside the
# selection: only an orphan whose own block is selected may be written (and
# warned about). An orphan kept that way still needs the note its body
# references, even when that target block is not itself selected. Before the
# wave-27 fix every orphan was exported.
SELECTION_ORPHAN_TEXT = "ORPHAN-BODY"
SELECTION_NESTED_TEXT = "NESTED-BODY"


def build_selection_playbook(plain_path: Path, selected_path: Path, kept_path: Path) -> list:
    return [
        {"tool": "document_new", "args": {"title": "Wave27 sel", "force": True}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "start",
                "blocks": [
                    {"id": "selP1", "type": "paragraph", "runs": [{"text": "one"}, {"text": "", "note_id": "selFn1"}]},
                    {"id": "selFn1", "type": "footnote", "runs": [{"text": "NOTE-ONE"}]},
                    {"id": "selP2", "type": "paragraph", "runs": [{"text": "two"}]},
                    {
                        "id": "selFnOrphan",
                        "type": "footnote",
                        "runs": [
                            {"text": SELECTION_ORPHAN_TEXT},
                            {"text": "", "note_id": "selFnNested"},
                        ],
                    },
                    {"id": "selFnNested", "type": "footnote", "runs": [{"text": SELECTION_NESTED_TEXT}]},
                ],
            },
        },
        {"tool": "export_html", "args": {"path": str(plain_path)}},
        {"tool": "export_html", "args": {"path": str(selected_path), "ids": ["selP1"]}},
        {"tool": "export_html", "args": {"path": str(kept_path), "ids": ["selP1", "selFnOrphan"]}},
    ]


def _aside_ids(text: str) -> set[str]:
    root = ET.fromstring(text)
    return {a.get("id") for a in root.iter() if _local(a.tag) == "aside"}


def _warnings_text(result: dict) -> str:
    return " | ".join(str(w) for w in (result.get("warnings") or []))


def check_selection_orphans(
    results: list[dict], plain_path: Path, selected_path: Path, kept_path: Path
) -> list[str]:
    errors: list[str] = []
    if len(results) < 3:
        return ["the selection playbook returned fewer than three results"]
    plain, selected, kept = results[-3], results[-2], results[-1]
    for label, res in (("plain", plain), ("selected", selected), ("kept", kept)):
        if not res.get("ok"):
            errors.append(f"export_html ({label}) failed: {res.get('error')}")
    try:
        plain_html = plain_path.read_text(encoding="utf-8")
        selected_html = selected_path.read_text(encoding="utf-8")
        kept_html = kept_path.read_text(encoding="utf-8")
    except OSError as exc:
        return errors + [f"cannot read a selection export: {exc}"]

    if "selFnOrphan" not in _aside_ids(plain_html):
        errors.append("a full export must keep the orphan note (no selection)")
    if "selFn1" not in _aside_ids(selected_html):
        errors.append("the selection export dropped the note referenced from the selected block")
    if "selFnOrphan" in _aside_ids(selected_html):
        errors.append("the selection export kept an orphan note whose block is not selected")
    if SELECTION_ORPHAN_TEXT in selected_html:
        errors.append("the selection export wrote the unselected orphan note body")
    if "selFnNested" in _aside_ids(selected_html) or SELECTION_NESTED_TEXT in selected_html:
        errors.append("the selection export wrote a note reachable only from an unselected orphan")
    if "selFnOrphan" in _warnings_text(selected):
        errors.append("the selection export warned about an unselected orphan note")
    if "selFnOrphan" not in _aside_ids(kept_html):
        errors.append("an explicitly selected orphan note must be exported")
    if "selFnOrphan" not in _warnings_text(kept):
        errors.append("an explicitly selected orphan note must still be warned as unreferenced")
    # The kept orphan references selFnNested, whose block is not selected: the
    # target body must be written so the reference resolves.
    if "selFnNested" not in _aside_ids(kept_html):
        errors.append("a note referenced from a kept orphan note was dropped")
    if SELECTION_NESTED_TEXT not in kept_html:
        errors.append("the body of a note referenced from a kept orphan note was dropped")
    if "selFnNested" in _warnings_text(kept):
        errors.append("a note referenced from a kept orphan note was warned as unreferenced")
    return errors


def run_epubcheck(epub: Path) -> list[str]:
    resolved = shutil.which("epubcheck")
    if resolved is None:
        return ["epubcheck not found on PATH"]
    env = spawn_env()
    # Force the English report so the "0 errors and 0 warnings" claim is checked
    # on the text, not on a localized message.
    env["LC_ALL"] = "C"
    env["LANG"] = "C"
    proc = run_cmd([resolved, str(epub)], timeout=EPUBCHECK_TIMEOUT, env=env)
    combined = (proc.stdout or "") + (proc.stderr or "")
    low = combined.lower()
    if "no errors or warnings detected" in low and proc.returncode == 0:
        return []
    return [f"epubcheck reported problems (exit {proc.returncode}): " + " | ".join(combined.strip().splitlines()[-6:])]


def hidden_xref_errors(cli: Path, folder: Path) -> list[str]:
    """A cross-reference must never publish text its target does not show: an omitted block
    (section_hidden, true hide_if), hidden or false-show_if runs, or hidden table cells. Checked in
    export_html, a .html save and every file of the EPUB (chapter titles, nav, captions)."""
    out = folder / "hidden-xref.html"
    saved = folder / "hidden-xref-saved.html"
    epub = folder / "hidden-xref.epub"
    xr = lambda target: {"field": "xref", "field_arg": target, "xref_kind": "page"}
    steps = [
        {"tool": "document_new", "args": {"title": "Wave27 hidden xref", "force": True}},
        {"tool": "insert_blocks", "args": {"after_id": "p1", "blocks": [
            {"id": "pHid", "type": "paragraph", "section_hidden": True, "runs": [{"text": "SECRET-XREF-TARGET"}]},
            {"id": "pExpr", "type": "paragraph", "hide_if": "1", "runs": [{"text": "SECRET-EXPR-TARGET"}]},
            {"id": "pRunsOff", "type": "paragraph", "runs": [
                {"text": "SECRET-RUN-HIDDEN", "hidden": True},
                {"text": "SECRET-RUN-SHOWIF", "show_if": "nope"}]},
            {"id": "tCells", "type": "table", "header": False,
             "rows": [[{"runs": [{"text": "SECRET-CELL", "hidden": True}]}, {"runs": [{"text": "cell ok"}]}]]},
            {"id": "hHead", "type": "heading", "level": 1, "runs": [{"text": "Chapter "}, xr("pHid"), xr("pRunsOff")]},
            {"id": "tCap", "type": "table", "header": False, "rows": [[{"runs": [{"text": "x"}]}]],
             "caption_runs": [{"text": "Table "}, xr("pExpr")]},
            {"id": "pRef", "type": "paragraph", "runs": [
                {"text": "See "}, xr("pHid"), {"text": " and "}, xr("pExpr"), {"text": " and "}, xr("pRunsOff"),
                {"text": " and "}, xr("tCells"),
                {"text": " text "}, {"field": "xref", "field_arg": "pHid"}]},
        ]}},
        {"tool": "export_html", "args": {"path": str(out)}, "expect": {"ok": True}},
        {"tool": "document_save", "args": {"path": str(saved)}, "expect": {"ok": True}},
        {"tool": "document_save", "args": {"path": str(epub)}, "expect": {"ok": True, "format": "epub"}},
    ]
    script = folder / "hidden-xref.json"
    script.write_text(json.dumps(steps), encoding="utf-8")
    play(cli, script)
    texts: dict[str, str] = {}
    try:
        texts["export_html"] = out.read_text(encoding="utf-8")
        texts["document_save .html"] = saved.read_text(encoding="utf-8")
        with zipfile.ZipFile(epub) as zf:
            for name in zf.namelist():
                if name.endswith((".xhtml", ".html", ".opf", ".ncx")):
                    texts[f"epub {name}"] = zf.read(name).decode("utf-8", errors="replace")
    except (OSError, zipfile.BadZipFile) as exc:
        return [f"cannot read the hidden-xref outputs: {exc}"]
    secrets = ("SECRET-XREF-TARGET", "SECRET-EXPR-TARGET", "SECRET-RUN-HIDDEN", "SECRET-RUN-SHOWIF", "SECRET-CELL")
    return [f"{where}: hidden text {sec!r} leaked through a cross-reference"
            for where, txt in texts.items() for sec in secrets if sec in txt]


def run_suite(cli: Path) -> bool:
    if not cli.is_file():
        print(f"FAIL wave27: missing cli {cli}")
        return False
    with tempfile.TemporaryDirectory(prefix="wave27-html-") as tmp:
        html_path = Path(tmp) / "export.html"
        epub_path = Path(tmp) / "export.epub"
        script = Path(tmp) / "play.json"
        script.write_text(
            json.dumps(build_playbook(html_path, epub_path), ensure_ascii=False), encoding="utf-8"
        )
        play(cli, script)
        try:
            html = html_path.read_text(encoding="utf-8")
        except OSError as exc:
            print(f"FAIL wave27 D5: cannot read {html_path}: {exc}")
            return False

        html_errors = check_html(html)
        hidden_errors = hidden_xref_errors(cli, Path(tmp))
        try:
            saved_errors = check_html(html_path.with_name("saved.html").read_text(encoding="utf-8"))
        except OSError as exc:
            saved_errors = [f"cannot read the document_save .html output: {exc}"]
        epub_errors = check_epub(epub_path)
        epubcheck_errors = run_epubcheck(epub_path)

        plain_sel = Path(tmp) / "sel-plain.html"
        selected_sel = Path(tmp) / "sel-only.html"
        kept_sel = Path(tmp) / "sel-orphan.html"
        sel_script = Path(tmp) / "sel.json"
        sel_script.write_text(
            json.dumps(build_selection_playbook(plain_sel, selected_sel, kept_sel), ensure_ascii=False),
            encoding="utf-8",
        )
        sel_out = play(cli, sel_script)
        selection_errors = check_selection_orphans(
            sel_out.get("results") or [], plain_sel, selected_sel, kept_sel
        )

    ok = True
    for label, errs in (
        ("D5 export_html", html_errors),
        ("D5 export_html selection orphans", selection_errors),
        ("D5 document_save .html", saved_errors),
        ("D5 page xref to a hidden block", hidden_errors),
        ("D2 EPUB note paragraphs", epub_errors),
        ("EPUB epubcheck", epubcheck_errors),
    ):
        if errs:
            ok = False
            print(f"FAIL wave27 {label}:")
            for err in errs:
                print(f"  {err}")
        else:
            print(f"ok   wave27 {label}")
    return ok


def self_test(cli: Path | None) -> int:
    ok = True
    if _local("{http://www.w3.org/1999/xhtml}p") != "p":
        print("FAIL self-test _local")
        ok = False
    if _classes(ET.fromstring('<p xmlns="http://www.w3.org/1999/xhtml" class="a b"/>')) != ["a", "b"]:
        print("FAIL self-test _classes")
        ok = False
    # A document with no styles/notes/fields must fail the D5 checks.
    bare = '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Wave27 HTML</title></head><body><p>x</p></body></html>'
    if not check_html(bare):
        print("FAIL self-test a bare HTML document must fail the D5 checks")
        ok = False
    else:
        print("ok   bare HTML document rejected")
    if cli is None:
        print("FAIL self-test needs --cli for the end-to-end run")
        return 1
    if not run_suite(cli):
        ok = False
    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test(args.cli)
    if args.cli is None:
        parser.error("--cli is required")
    return 0 if run_suite(args.cli) else 1


if __name__ == "__main__":
    raise SystemExit(main())
