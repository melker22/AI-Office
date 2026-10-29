#!/usr/bin/env python3
"""Wave 19 PDF QA: run a playbook export, then veraPDF, qpdf --json, and pdffonts."""

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
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parent.parent
DEFAULT_TIMEOUT = 300
VERAPDF_TIMEOUT = 120
TOOL_TIMEOUT = 60


def headless_env() -> dict[str, str]:
    env = os.environ.copy()
    # A test interposer must not reach the CLI (CI's glibc is not this process's).
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


def play(cli: Path, playbook: Path) -> list[str]:
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
    results = out.get("results") or []
    with playbook.open(encoding="utf-8") as handle:
        calls = json.load(handle)
    if len(results) != len(calls):
        raise SystemExit(f"play result count {len(results)} != {len(calls)}")
    warnings: list[str] = []
    for call, res in zip(calls, results):
        if call.get("tool") == "document_export_pdf" and isinstance(res, dict):
            for w in res.get("warnings") or []:
                if isinstance(w, str):
                    warnings.append(w)
    return warnings


def load_expectations(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"invalid expectations {path}: {exc}") from exc
    if not isinstance(data, dict):
        raise SystemExit(f"invalid expectations {path}: root must be an object")
    if "pdf" not in data or not isinstance(data["pdf"], str) or not data["pdf"].strip():
        raise SystemExit(f"invalid expectations {path}: missing non-empty string field 'pdf'")
    return data


def parse_verapdf_xml(xml_text: str) -> tuple[bool | None, list[str]]:
    """Return (isCompliant, failed_rule_ids)."""
    failed: list[str] = []
    compliant: bool | None = None
    try:
        root = ET.fromstring(xml_text)
    except ET.ParseError:
        return None, failed
    for elem in root.iter():
        tag = elem.tag.split("}")[-1] if "}" in elem.tag else elem.tag
        if tag == "validationReport":
            val = elem.attrib.get("isCompliant") or elem.attrib.get("status")
            if val is not None:
                compliant = val.lower() in ("true", "passed", "pass")
        if tag == "rule" and elem.attrib.get("status") == "failed":
            rid = elem.attrib.get("id") or elem.attrib.get("specification") or tag
            failed.append(rid)
        if tag == "check" and elem.attrib.get("status") == "failed":
            rid = elem.attrib.get("ruleId") or elem.attrib.get("id") or "check"
            failed.append(rid)
    return compliant, failed


def verapdf_is_missing(resolved: str | None, exc: BaseException | None = None) -> bool:
    """verapdf is absent when PATH has no binary or exec raises FileNotFoundError.

    Do not sniff stderr: a normal JVM/veraPDF message can contain "not found".
    """
    if isinstance(exc, FileNotFoundError):
        return True
    return resolved is None


def run_verapdf(pdf: Path, flavour: str) -> tuple[bool | None, list[str], str]:
    resolved = shutil.which("verapdf")
    if verapdf_is_missing(resolved):
        raise SystemExit("verapdf not found (add verapdf to shell.nix)")
    proc: subprocess.CompletedProcess[str] | None = None
    for fmt in ("xml", "mrr"):
        try:
            proc = run_cmd(
                ["verapdf", f"--flavour={flavour}", f"--format={fmt}", str(pdf)],
                timeout=VERAPDF_TIMEOUT,
            )
        except FileNotFoundError as exc:
            if verapdf_is_missing(resolved, exc):
                raise SystemExit("verapdf not found (add verapdf to shell.nix)") from exc
            raise
        out = proc.stdout or proc.stderr or ""
        compliant, failed = parse_verapdf_xml(out)
        if compliant is not None or failed:
            return compliant, failed, out
        if "passed" in out.lower() and "failed" not in out.lower():
            return True, [], out
        if "failed" in out.lower() or "non-compliant" in out.lower():
            return False, failed or ["non-compliant"], out
    if proc is None:
        return None, [], ""
    return None, [], (proc.stdout or "") + (proc.stderr or "")


def qpdf_json(pdf: Path) -> dict[str, Any]:
    proc = run_cmd(["qpdf", "--json=1", str(pdf)], timeout=TOOL_TIMEOUT)
    if proc.returncode != 0:
        raise SystemExit(f"qpdf --json failed ({proc.returncode}): {(proc.stderr or proc.stdout)[:500]}")
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"qpdf JSON parse error: {exc}") from exc


def pdffonts_report(pdf: Path) -> list[dict[str, str]]:
    proc = run_cmd(["pdffonts", str(pdf)], timeout=TOOL_TIMEOUT)
    if proc.returncode != 0:
        raise SystemExit(f"pdffonts failed ({proc.returncode}): {proc.stderr.strip()}")
    lines = proc.stdout.splitlines()
    rows: list[dict[str, str]] = []
    for line in lines[2:]:
        if not line.strip():
            continue
        parts = line.split()
        if len(parts) < 7:
            continue
        # pdffonts uses a fixed 36-column name field; tail is type, encoding, emb, sub, uni, object ID.
        emb, sub, uni = parts[-5], parts[-4], parts[-3]
        rows.append(
            {
                "name": line[:36].strip(),
                "emb": emb,
                "sub": sub,
                "uni": uni,
            }
        )
    return rows


def pdftotext_report(pdf: Path) -> str:
    proc = run_cmd(["pdftotext", "-layout", str(pdf), "-"], timeout=TOOL_TIMEOUT)
    if proc.returncode != 0:
        raise SystemExit(f"pdftotext failed ({proc.returncode}): {proc.stderr.strip()}")
    return proc.stdout


def uncompressed_pdf(pdf: Path) -> str:
    try:
        proc = subprocess.run(
            ["qpdf", "--qdf", "--stream-data=uncompress", str(pdf), "-"],
            capture_output=True,
            timeout=TOOL_TIMEOUT,
            cwd=REPO,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise SystemExit(f"timeout after {TOOL_TIMEOUT}s: qpdf uncompress") from exc
    if proc.returncode != 0:
        err = proc.stderr.decode("latin-1", errors="replace")[:500]
        raise SystemExit(f"qpdf uncompress failed ({proc.returncode}): {err}")
    return proc.stdout.decode("latin-1", errors="replace")


def stream_has_rgb(data: str, rgb: tuple[float, float, float], tol: float = 0.02) -> bool:
    for match in re.finditer(
        r"([0-9]*\.?[0-9]+)\s+([0-9]*\.?[0-9]+)\s+([0-9]*\.?[0-9]+)\s+(?:rg|RG|scn|SCN)\b",
        data,
    ):
        vals = tuple(float(part) for part in match.groups())
        if all(abs(got - want) <= tol for got, want in zip(vals, rgb)):
            return True
    return False


def font_names(pdf: Path) -> list[str]:
    return sorted({row["name"] for row in pdffonts_report(pdf)})


def strip_lang(value: Any) -> None:
    if isinstance(value, dict):
        value.pop("lang", None)
        for child in value.values():
            strip_lang(child)
    elif isinstance(value, list):
        for child in value:
            strip_lang(child)


def export_cleared_lang(cli: Path, aio_path: Path, pdfa: bool, ua: bool) -> Path:
    try:
        doc = json.loads(aio_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"cannot read {aio_path}: {exc}") from exc
    if not isinstance(doc, dict) or not isinstance(doc.get("body"), list):
        raise SystemExit(f"{aio_path} has no body array")
    strip_lang(doc["body"])
    cleared = aio_path.with_name(aio_path.stem + "-nolang.aio.json")
    cleared.write_text(json.dumps(doc), encoding="utf-8")
    out_pdf = aio_path.with_name(aio_path.stem + "-nolang.pdf")
    play_path = aio_path.with_name(aio_path.stem + "-nolang-play.json")
    play_path.write_text(
        json.dumps(
            [
                {"tool": "document_open", "args": {"path": str(cleared)}},
                {
                    "tool": "document_export_pdf",
                    "args": {"path": str(out_pdf), "pdfa": pdfa, "ua": ua},
                },
            ]
        ),
        encoding="utf-8",
    )
    play(cli, play_path)
    if not out_pdf.is_file():
        raise SystemExit(f"cleared-lang export missing: {out_pdf}")
    return out_pdf


def iter_json_nodes(obj: Any):
    if isinstance(obj, dict):
        yield obj
        for val in obj.values():
            yield from iter_json_nodes(val)
    elif isinstance(obj, list):
        for item in obj:
            yield from iter_json_nodes(item)


def struct_elem_types(qpdf_data: dict[str, Any]) -> list[str]:
    types: list[str] = []
    for node in iter_json_nodes(qpdf_data):
        s_val = node.get("/S") or node.get("S")
        if isinstance(s_val, str):
            name = s_val.lstrip("/")
            types.append(name)
        elif isinstance(s_val, dict) and "name" in s_val:
            types.append(str(s_val["name"]).lstrip("/"))
    return types


def catalog_lang(qpdf_data: dict[str, Any]) -> str | None:
    for node in iter_json_nodes(qpdf_data):
        if "/Lang" in node:
            lang = node["/Lang"]
            if isinstance(lang, str):
                return lang
        if "Lang" in node and isinstance(node["Lang"], str):
            return node["Lang"]
    return None


def struct_elems_with_lang(qpdf_data: dict[str, Any]) -> list[str]:
    langs: list[str] = []
    for node in iter_json_nodes(qpdf_data):
        if "/S" in node or "S" in node:
            lang = node.get("/Lang") or node.get("Lang")
            if isinstance(lang, str):
                langs.append(lang)
    return langs


def document_struct_child_types(qpdf_data: dict[str, Any]) -> list[str]:
    """Best-effort: /S values under the first /Document struct elem."""
    for node in iter_json_nodes(qpdf_data):
        s_val = node.get("/S") or node.get("S")
        if s_val in ("/Document", "Document", {"name": "/Document"}):
            kids = node.get("/K") or node.get("K") or []
            if not isinstance(kids, list):
                continue
            order: list[str] = []
            for kid in kids:
                if isinstance(kid, dict):
                    kv = kid.get("/S") or kid.get("S")
                    if isinstance(kv, str):
                        order.append(kv.lstrip("/"))
            if order:
                return order
    return []


def check_expectations(
    pdf: Path,
    exp: dict[str, Any],
    play_warnings: list[str] | None = None,
    cli: Path | None = None,
) -> list[str]:
    errors: list[str] = []
    verapdf_exp = exp.get("verapdf") or {}
    if isinstance(verapdf_exp, dict):
        for flavour, want in verapdf_exp.items():
            if not isinstance(want, dict):
                continue
            compliant, failed, _snippet = run_verapdf(pdf, flavour)
            expected = want.get("compliant")
            if expected is not None and compliant != expected:
                rules = ", ".join(failed[:8])
                suffix = f"; failed rules: {rules}" if rules else ""
                errors.append(
                    f"veraPDF flavour {flavour}: expected compliant={expected}, got {compliant}{suffix}"
                )

    qpdf_exp = exp.get("qpdf") or {}
    if isinstance(qpdf_exp, dict) and qpdf_exp:
        data = qpdf_json(pdf)
        types = struct_elem_types(data)
        if qpdf_exp.get("no_part_fallback") is True and "Part" in types:
            count = types.count("Part")
            errors.append(f"qpdf struct: expected no /Part fallback, found {count} Part tag(s)")
        need_tags = qpdf_exp.get("struct_types")
        if isinstance(need_tags, list):
            for tag in need_tags:
                t = str(tag).lstrip("/")
                if t not in types:
                    errors.append(f"qpdf struct: missing /{t} (have {sorted(set(types))[:20]})")
        mins = qpdf_exp.get("min_struct_counts")
        if isinstance(mins, dict):
            for tag, raw_n in mins.items():
                t = str(tag).lstrip("/")
                try:
                    need = int(raw_n)
                except (TypeError, ValueError):
                    errors.append(f"qpdf min_struct_counts: bad count for /{t}")
                    continue
                got = types.count(t)
                if got < need:
                    errors.append(f"qpdf struct: /{t} count {got} < {need}")
        lang = qpdf_exp.get("catalog_lang")
        if isinstance(lang, str):
            got = catalog_lang(data)
            if got != lang:
                errors.append(f"qpdf catalog /Lang: expected {lang!r}, got {got!r}")
        span_lang = qpdf_exp.get("span_lang")
        if isinstance(span_lang, str):
            langs = struct_elems_with_lang(data)
            if span_lang not in langs:
                errors.append(f"qpdf struct /Lang: expected span lang {span_lang!r} in {langs!r}")
        figure_alt = qpdf_exp.get("figure_alt")
        if isinstance(figure_alt, str):
            alts: list[str] = []
            for node in iter_json_nodes(data):
                s_val = node.get("/S") or node.get("S")
                if s_val in ("/Figure", "Figure", {"name": "/Figure"}):
                    alt = node.get("/Alt") or node.get("Alt")
                    if isinstance(alt, str):
                        alts.append(alt)
            if figure_alt not in alts:
                errors.append(f"qpdf /Figure /Alt: expected {figure_alt!r}, found {alts!r}")
        order = qpdf_exp.get("document_child_types")
        if isinstance(order, list):
            got_order = document_struct_child_types(data)
            want = [str(x).lstrip("/") for x in order]
            if got_order and got_order != want:
                errors.append(f"qpdf /Document child /S order: expected {want}, got {got_order}")
            elif not got_order:
                errors.append("qpdf /Document child order: could not read struct tree (T1 pending?)")

    fonts_exp = exp.get("pdffonts") or {}
    if isinstance(fonts_exp, dict) and fonts_exp:
        if fonts_exp.get("all_embedded") is True or fonts_exp.get("name_contains") or fonts_exp.get("same_as_cleared_lang"):
            rows = pdffonts_report(pdf)
            if not rows:
                errors.append("pdffonts: no fonts listed")
            else:
                if fonts_exp.get("all_embedded") is True:
                    bad = [r["name"] for r in rows if r.get("emb", "").lower() != "yes"]
                    if bad:
                        errors.append(f"pdffonts: not embedded: {', '.join(bad)}")
                needles = fonts_exp.get("name_contains")
                if isinstance(needles, list):
                    names = [r["name"] for r in rows]
                    for needle in needles:
                        if not isinstance(needle, str) or not any(needle in name for name in names):
                            errors.append(f"pdffonts: no font name contains {needle!r} in {names!r}")
                cleared = fonts_exp.get("same_as_cleared_lang")
                if isinstance(cleared, dict):
                    if cli is None:
                        errors.append("pdffonts same_as_cleared_lang: cli is required")
                    else:
                        aio = Path(str(cleared.get("aio") or ""))
                        if not aio.is_file():
                            errors.append(f"pdffonts same_as_cleared_lang: missing {aio}")
                        else:
                            other = export_cleared_lang(
                                cli,
                                aio,
                                bool(cleared.get("pdfa")),
                                bool(cleared.get("ua")),
                            )
                            got = font_names(pdf)
                            want = font_names(other)
                            if got != want:
                                errors.append(
                                    f"pdffonts: bilingual fonts {got} != same document without foreign lang {want}"
                                )

    text_needles = exp.get("text_contains")
    if isinstance(text_needles, list):
        text = pdftotext_report(pdf)
        for needle in text_needles:
            if isinstance(needle, str) and needle not in text:
                errors.append(f"pdftotext: missing {needle!r}")

    rgb_needles = exp.get("content_rgb")
    if isinstance(rgb_needles, list):
        stream = uncompressed_pdf(pdf)
        for raw in rgb_needles:
            if not isinstance(raw, list) or len(raw) != 3:
                errors.append(f"content_rgb: expected [r, g, b], got {raw!r}")
                continue
            try:
                rgb = (float(raw[0]), float(raw[1]), float(raw[2]))
            except (TypeError, ValueError):
                errors.append(f"content_rgb: bad triple {raw!r}")
                continue
            if not stream_has_rgb(stream, rgb):
                errors.append(f"content stream: missing rg near {rgb}")

    warn_exp = exp.get("export_warnings")
    if warn_exp is not None:
        warnings = play_warnings or []
        if isinstance(warn_exp, dict):
            substr = warn_exp.get("contains")
            if isinstance(substr, str):
                joined = " ".join(warnings).lower()
                if substr.lower() not in joined:
                    errors.append(
                        f"export warnings: expected substring {substr!r} in {warnings!r} (T2 pending?)"
                    )
        elif isinstance(warn_exp, list):
            for item in warn_exp:
                if isinstance(item, str) and item.lower() not in " ".join(warnings).lower():
                    errors.append(f"export warnings: missing {item!r}")

    return errors


WAVE25_PLAYBOOKS = (
    "wave25-d20-utf16.json",
    "wave25-d21-copies.json",
    "wave25-d21-note-copies.json",
    "wave25-d21-note-collide.json",
    "wave25-d22-nested-list.json",
    "wave25-d22-list-order.json",
    "wave25-d23-cell-span.json",
    "wave25-d23-span-limit.json",
)

_PDF_NAMED_ESCAPES = {
    ord("n"): 0x0A,
    ord("r"): 0x0D,
    ord("t"): 0x09,
    ord("b"): 0x08,
    ord("f"): 0x0C,
    ord("("): ord("("),
    ord(")"): ord(")"),
    ord("\\"): ord("\\"),
}
_PDF_RAW_CONTROLS = (0x0A, 0x0D, 0x09, 0x08, 0x0C)


class PdfLiteral:
    def __init__(self, decoded: bytes, raw_controls: list[int]) -> None:
        self.decoded = decoded
        self.raw_controls = raw_controls


def parse_pdf_literal(data: bytes, open_at: int) -> tuple[PdfLiteral, int]:
    """Parse a PDF literal string starting at '('. PDF end-of-line inside the
    literal is normalized to LF, which is what corrupts an unescaped UTF-16 CR."""
    i = open_at + 1
    out = bytearray()
    raw_controls: list[int] = []
    depth = 1
    while i < len(data) and depth:
        c = data[i]
        if c == ord("\\"):
            i += 1
            if i >= len(data):
                break
            e = data[i]
            mapped = _PDF_NAMED_ESCAPES.get(e)
            if mapped is not None:
                out.append(mapped)
                i += 1
            elif e in b"01234567":
                octdigits = bytearray((e,))
                i += 1
                for _ in range(2):
                    if i < len(data) and data[i] in b"01234567":
                        octdigits.append(data[i])
                        i += 1
                    else:
                        break
                out.append(int(bytes(octdigits), 8) & 0xFF)
            elif e in (0x0A, 0x0D):
                if e == 0x0D and i + 1 < len(data) and data[i + 1] == 0x0A:
                    i += 2
                else:
                    i += 1
            else:
                out.append(e)
                i += 1
            continue
        if c == ord("("):
            depth += 1
            out.append(c)
            i += 1
            continue
        if c == ord(")"):
            depth -= 1
            if depth == 0:
                return PdfLiteral(bytes(out), raw_controls), i + 1
            out.append(c)
            i += 1
            continue
        if c in _PDF_RAW_CONTROLS:
            raw_controls.append(c)
        if c == 0x0D:
            if i + 1 < len(data) and data[i + 1] == 0x0A:
                i += 2
            else:
                i += 1
            out.append(0x0A)
            continue
        if c == 0x0A:
            out.append(0x0A)
            i += 1
            continue
        out.append(c)
        i += 1
    return PdfLiteral(bytes(out), raw_controls), i


def iter_pdf_literals(data: bytes, key: bytes) -> list[PdfLiteral]:
    found: list[PdfLiteral] = []
    start = 0
    while True:
        at = data.find(key, start)
        if at < 0:
            return found
        j = at + len(key)
        while j < len(data) and data[j] in b" \t\r\n":
            j += 1
        if j < len(data) and data[j] == ord("("):
            lit, end = parse_pdf_literal(data, j)
            found.append(lit)
            start = end
        else:
            start = at + len(key)


def utf16_pdf_text(decoded: bytes) -> str | None:
    if len(decoded) < 2 or decoded[:2] != b"\xfe\xff":
        return None
    body = decoded[2:]
    if len(body) % 2:
        return None
    try:
        return body.decode("utf-16-be")
    except UnicodeDecodeError:
        return None


def qpdf_objects(data: dict[str, Any]) -> dict[str, Any]:
    qpdf = data.get("qpdf")
    if isinstance(qpdf, list):
        for item in qpdf:
            if isinstance(item, dict) and isinstance(item.get("objects"), dict):
                return item["objects"]
    objects = data.get("objects")
    if isinstance(objects, dict):
        return objects
    return {}


def _obj_dict(obj: Any) -> dict[str, Any]:
    if not isinstance(obj, dict):
        return {}
    value = obj.get("value")
    if isinstance(value, dict):
        return value
    return obj


def _norm_ref(ref: str) -> str:
    ref = ref.strip()
    if ref.startswith("obj:"):
        ref = ref[4:]
    return ref


def _ref_of(value: Any) -> str | None:
    if isinstance(value, str) and value.endswith(" R"):
        return _norm_ref(value)
    if isinstance(value, dict):
        inner = value.get("object") or value.get("ref")
        if isinstance(inner, str) and inner.endswith(" R"):
            return _norm_ref(inner)
    return None


def _name_of(value: Any) -> str:
    if isinstance(value, str):
        return value if value.startswith("/") else f"/{value}"
    if isinstance(value, dict):
        inner = value.get("name")
        if isinstance(inner, str):
            return inner if inner.startswith("/") else f"/{inner}"
    return ""


def _lookup_object(objects: dict[str, Any], ref: str) -> dict[str, Any]:
    want = _norm_ref(ref)
    for key, obj in objects.items():
        if _norm_ref(str(key)) == want:
            return _obj_dict(obj)
    return {}


def _struct_elems(objects: dict[str, Any]) -> list[dict[str, Any]]:
    elems: list[dict[str, Any]] = []
    for obj in objects.values():
        body = _obj_dict(obj)
        if _name_of(body.get("/S") or body.get("S")):
            elems.append(body)
    return elems


def _as_list(value: Any) -> list[Any]:
    if value is None:
        return []
    if isinstance(value, list):
        return value
    return [value]


def _mcr_pages(elem: dict[str, Any], objects: dict[str, Any], seen: set[int]) -> set[str]:
    pages: set[str] = set()
    marker = id(elem)
    if marker in seen:
        return pages
    seen.add(marker)
    for kid in _as_list(elem.get("/K") if "/K" in elem else elem.get("K")):
        if isinstance(kid, dict):
            pg = _ref_of(kid.get("/Pg") or kid.get("Pg"))
            if pg:
                pages.add(pg)
            if _name_of(kid.get("/S") or kid.get("S")):
                pages |= _mcr_pages(kid, objects, seen)
            continue
        ref = _ref_of(kid)
        if ref:
            child = _lookup_object(objects, ref)
            if child:
                pages |= _mcr_pages(child, objects, seen)
    return pages


def _min_mcid(elem: dict[str, Any], objects: dict[str, Any], seen: set[int]) -> int | None:
    """Smallest MCID under a structure element (its first painted content)."""
    marker = id(elem)
    if marker in seen:
        return None
    seen.add(marker)
    best: int | None = None
    for kid in _as_list(elem.get("/K") if "/K" in elem else elem.get("K")):
        got: int | None = None
        if isinstance(kid, int) and not isinstance(kid, bool):
            got = kid
        elif isinstance(kid, dict) and not _ref_of(kid):
            mcid = kid.get("/MCID") if "/MCID" in kid else kid.get("MCID")
            if isinstance(mcid, int) and not isinstance(mcid, bool):
                got = mcid
            elif _name_of(kid.get("/S") or kid.get("S")):
                got = _min_mcid(kid, objects, seen)
        else:
            ref = _ref_of(kid)
            if ref:
                child = _lookup_object(objects, ref)
                if child:
                    got = _min_mcid(child, objects, seen)
        if got is not None and (best is None or got < best):
            best = got
    return best


def _attr_dicts(elem: dict[str, Any]) -> list[dict[str, Any]]:
    raw = elem.get("/A") if "/A" in elem else elem.get("A")
    out: list[dict[str, Any]] = []
    for item in _as_list(raw):
        if isinstance(item, dict):
            ref = _ref_of(item)
            if ref:
                continue
            out.append(item)
    return out


def _int_attr(attrs: dict[str, Any], name: str) -> int | None:
    value = attrs.get(name)
    if value is None and name.startswith("/"):
        value = attrs.get(name[1:])
    if isinstance(value, bool) or value is None:
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, float) and value.is_integer():
        return int(value)
    if isinstance(value, str) and value.isdigit():
        return int(value)
    return None


def _pdf_string(value: Any) -> str | None:
    if isinstance(value, str):
        return value
    if isinstance(value, dict):
        for key in ("string", "text", "value"):
            inner = value.get(key)
            if isinstance(inner, str):
                return inner
    return None


def note_id_errors(elems: list[dict[str, Any]], want: list[Any]) -> list[str]:
    """/Note /ID values, one per copy. Missing or duplicate ids fail."""
    if not isinstance(want, list) or not want or not all(isinstance(item, str) and item for item in want):
        return ["note_ids: expected a non-empty list of strings"]
    got: list[str] = []
    notes = 0
    errors: list[str] = []
    for elem in elems:
        if _name_of(elem.get("/S") or elem.get("S")) != "/Note":
            continue
        notes += 1
        raw = elem.get("/ID") if "/ID" in elem else elem.get("ID")
        text = _pdf_string(raw)
        if not text:
            errors.append(f"note_ids: /Note missing a readable /ID ({raw!r})")
            continue
        got.append(text)
    dupes = sorted({item for item in got if got.count(item) > 1})
    if dupes:
        errors.append(f"note_ids: duplicate /ID {dupes!r}")
    if notes == 0:
        errors.append("note_ids: no /Note elements")
    elif sorted(got) != sorted(want):
        errors.append(f"note_ids: got {got!r}, expected {want!r}")
    return errors


def check_wave25_extra(pdf: Path, exp: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    literals = exp.get("utf16_literals")
    if isinstance(literals, list):
        data = pdf.read_bytes()
        for spec in literals:
            if not isinstance(spec, dict):
                errors.append("utf16_literals: entry must be an object")
                continue
            key = str(spec.get("key") or "")
            want = spec.get("text")
            if not key.startswith("/") or not isinstance(want, str):
                errors.append(f"utf16_literals: bad entry {spec!r}")
                continue
            matches = iter_pdf_literals(data, key.encode("ascii"))
            ok = False
            seen: list[str | None] = []
            for lit in matches:
                got = utf16_pdf_text(lit.decoded)
                seen.append(got)
                if got == want and not lit.raw_controls:
                    ok = True
                    break
                if got == want and lit.raw_controls:
                    codes = ", ".join(f"0x{b:02X}" for b in lit.raw_controls)
                    errors.append(f"{key} round-trips but still contains raw controls ({codes})")
            if not ok and not any(err.startswith(key) for err in errors):
                errors.append(f"{key}: expected {want!r}, found {seen!r}")

    needs_objects = any(k in exp for k in ("copies_structure", "nested_list", "cell_spans", "note_ids"))
    objects: dict[str, Any] = {}
    elems: list[dict[str, Any]] = []
    if needs_objects:
        objects = qpdf_objects(qpdf_json(pdf))
        elems = _struct_elems(objects)
        if not elems:
            errors.append("qpdf: no structure elements in the object map")

    copies = exp.get("copies_structure")
    if isinstance(copies, dict) and elems:
        role = _name_of(str(copies.get("role") or "P"))
        try:
            min_count = int(copies.get("min_count"))
            distinct = int(copies.get("distinct_pages"))
        except (TypeError, ValueError):
            errors.append("copies_structure: min_count and distinct_pages must be integers")
        else:
            chosen = [e for e in elems if _name_of(e.get("/S") or e.get("S")) == role]
            if len(chosen) < min_count:
                errors.append(f"copies: /{role[1:]} count {len(chosen)} < {min_count}")
            pages: set[str] = set()
            for elem in chosen:
                owned = _mcr_pages(elem, objects, set())
                if len(owned) != 1:
                    errors.append(
                        f"copies: one /{role[1:]} covers pages {sorted(owned) or ['none']} "
                        "(page copies were merged)"
                    )
                pages |= owned
            if len(pages) < distinct:
                errors.append(f"copies: structure covers {len(pages)} page(s), expected {distinct}")

    nested = exp.get("nested_list")
    if isinstance(nested, dict) and elems:
        try:
            min_l = int(nested.get("min_l"))
        except (TypeError, ValueError):
            errors.append("nested_list: min_l must be an integer")
            min_l = 2
        lists = [e for e in elems if _name_of(e.get("/S") or e.get("S")) == "/L"]
        if len(lists) < min_l:
            errors.append(f"nested list: /L count {len(lists)} < {min_l}")
        inner = False
        for li in elems:
            if _name_of(li.get("/S") or li.get("S")) != "/LI":
                continue
            for kid in _as_list(li.get("/K") if "/K" in li else li.get("K")):
                child: dict[str, Any] | None = None
                if isinstance(kid, dict) and _name_of(kid.get("/S") or kid.get("S")):
                    child = kid
                else:
                    ref = _ref_of(kid)
                    if ref:
                        child = _lookup_object(objects, ref)
                if child and _name_of(child.get("/S") or child.get("S")) == "/L":
                    inner = True
        if not inner:
            errors.append("nested list: no /LI contains an inner /L")
        if nested.get("roots_in_order"):
            # Root lists under /Document must follow the page order of their
            # first content (single-page fixture: MCIDs grow in paint order).
            docs = [e for e in elems if _name_of(e.get("/S") or e.get("S")) == "/Document"]
            firsts: list[int] = []
            for doc_elem in docs[:1]:
                for kid in _as_list(doc_elem.get("/K") if "/K" in doc_elem else doc_elem.get("K")):
                    ref = _ref_of(kid)
                    child = _lookup_object(objects, ref) if ref else (kid if isinstance(kid, dict) else None)
                    if not child or _name_of(child.get("/S") or child.get("S")) != "/L":
                        continue
                    first = _min_mcid(child, objects, set())
                    if first is not None:
                        firsts.append(first)
            if len(firsts) < 2:
                errors.append(f"nested list: expected 2+ root /L under /Document, saw {len(firsts)}")
            elif firsts != sorted(firsts):
                errors.append(f"nested list: root /L out of document order (first MCIDs {firsts})")

    spans = exp.get("cell_spans")
    if isinstance(spans, dict) and elems:
        try:
            want_col = int(spans.get("col_span"))
            want_row = int(spans.get("row_span"))
        except (TypeError, ValueError):
            errors.append("cell_spans: col_span and row_span must be integers")
        else:
            got_col = False
            got_row = False
            for elem in elems:
                role = _name_of(elem.get("/S") or elem.get("S"))
                if role not in ("/TD", "/TH"):
                    continue
                for attrs in _attr_dicts(elem):
                    owner = _name_of(attrs.get("/O") or attrs.get("O"))
                    col = _int_attr(attrs, "/ColSpan")
                    row = _int_attr(attrs, "/RowSpan")
                    if col == want_col:
                        got_col = True
                        if owner != "/Table":
                            errors.append(f"cell span: /ColSpan {col} owner is {owner or 'missing'}, expected /Table")
                    if row == want_row:
                        got_row = True
                        if owner != "/Table":
                            errors.append(f"cell span: /RowSpan {row} owner is {owner or 'missing'}, expected /Table")
            if not got_col:
                errors.append(f"cell span: no TD/TH /A has /ColSpan {want_col}")
            if not got_row:
                errors.append(f"cell span: no TD/TH /A has /RowSpan {want_row}")

    if "note_ids" in exp:
        if not elems:
            errors.append("note_ids: no structure elements")
        else:
            errors.extend(note_id_errors(elems, exp.get("note_ids")))
    return errors


def run_wave25_suite(cli: Path) -> int:
    """Export each qa/wave25-*.json playbook and judge the PDF. Expectations live in
    qa/pdf/ so the ctest playbook glob does not try to play them."""
    missing = [name for name in WAVE25_PLAYBOOKS if not (REPO / "qa" / name).is_file()]
    if missing:
        print("FAIL wave25 playbooks missing: " + ", ".join(missing))
        return 1
    if not cli.is_file():
        print(f"FAIL wave25: missing cli {cli}")
        return 1
    failed = False
    for name in WAVE25_PLAYBOOKS:
        playbook = REPO / "qa" / name
        expect_path = REPO / "qa" / "pdf" / name
        if not expect_path.is_file():
            print(f"FAIL {name}: missing expectations {expect_path}")
            failed = True
            continue
        try:
            exp = load_expectations(expect_path)
        except SystemExit as exc:
            print(f"FAIL {name}: {exc}")
            failed = True
            continue
        with tempfile.TemporaryDirectory(prefix="wave25-") as tmp:
            dest = Path(tmp) / "out.pdf"
            calls = json.loads(playbook.read_text(encoding="utf-8"))
            wrote = False
            if isinstance(calls, list):
                for call in calls:
                    if isinstance(call, dict) and call.get("tool") == "document_export_pdf":
                        args = call.setdefault("args", {})
                        if isinstance(args, dict):
                            args["path"] = str(dest)
                            wrote = True
            if not wrote:
                print(f"FAIL {name}: playbook has no document_export_pdf")
                failed = True
                continue
            staged = Path(tmp) / "play.json"
            staged.write_text(json.dumps(calls), encoding="utf-8")
            try:
                warnings = play(cli, staged)
                errors = check_expectations(dest, exp, warnings, cli)
                errors.extend(check_wave25_extra(dest, exp))
            except SystemExit as exc:
                print(f"FAIL {name}: {exc}")
                failed = True
                continue
            if errors:
                failed = True
                for err in errors:
                    print(f"FAIL {name}: {err}")
            else:
                print(f"ok   {name}")
    return 1 if failed else 0


def pdf_cli_wiring_errors() -> list[str]:
    """self_test must take the CLI and pass it on; ctest must pass that path."""
    errors: list[str] = []
    source = inspect.getsource(self_test)
    if "build/linux/aioffice-cli" in source:
        errors.append("pdf self_test still uses build/linux/aioffice-cli")
    if "run_wave25_suite(cli)" not in source:
        errors.append("pdf self_test does not pass cli into run_wave25_suite")
    if not source.startswith("def self_test(cli:"):
        errors.append("pdf self_test does not take the cli path")
    main_source = inspect.getsource(main)
    if "self_test(args.cli)" not in main_source:
        errors.append("pdf main does not pass args.cli into self_test")
    cmake = (REPO / "CMakeLists.txt").read_text(encoding="utf-8")
    start = cmake.find("add_test(NAME pdf_check_selftest")
    end = cmake.find("set_tests_properties(pdf_check_selftest", start)
    if start < 0 or end < 0:
        errors.append("pdf_check_selftest CMake block not found")
    elif '--cli "${AIOFFICE_CLI}"' not in cmake[start:end]:
        errors.append('pdf_check_selftest does not pass --cli "${AIOFFICE_CLI}"')
    return errors


def self_test(cli: Path) -> int:
    ok = True

    wiring = pdf_cli_wiring_errors()
    if wiring:
        print("FAIL self-test cli wiring: " + "; ".join(wiring))
        ok = False
    else:
        print("ok   pdf self-test uses the passed CLI")

    dup = note_id_errors(
        [{"/S": "/Note", "/ID": "fn1"}, {"/S": "/Note", "/ID": "fn1"}],
        ["fn1", "fn1-c1"],
    )
    if not dup:
        print("FAIL self-test note_ids duplicate must fail")
        ok = False
    else:
        print("ok   note_ids duplicate fails")
    missing = note_id_errors([{"/S": "/P", "/ID": "fn1"}], ["fn1"])
    if not missing:
        print("FAIL self-test note_ids with no /Note must fail")
        ok = False
    else:
        print("ok   note_ids with no /Note fails")
    blank = note_id_errors([{"/S": "/Note"}], ["fn1"])
    if not blank:
        print("FAIL self-test note_ids missing /ID must fail")
        ok = False
    else:
        print("ok   note_ids missing /ID fails")
    good = note_id_errors(
        [{"/S": "/Note", "/ID": "fn1-c1"}, {"/S": "/Note", "/ID": "fn1"}],
        ["fn1", "fn1-c1"],
    )
    if good:
        print(f"FAIL self-test note_ids exact match: {good}")
        ok = False
    else:
        print("ok   note_ids exact match")

    sample_pass = """<validationReports><validationReport isCompliant="true"></validationReport></validationReports>"""
    c, f = parse_verapdf_xml(sample_pass)
    if c is not True or f:
        print("FAIL self-test parse compliant XML")
        ok = False
    else:
        print("ok   parse_verapdf_xml compliant")

    sample_fail = """<validationReports><validationReport isCompliant="false"><details><rule id="6.1.3-1" status="failed"/></details></validationReport></validationReports>"""
    c2, f2 = parse_verapdf_xml(sample_fail)
    if c2 is not False or "6.1.3-1" not in f2:
        print("FAIL self-test parse non-compliant XML")
        ok = False
    else:
        print("ok   parse_verapdf_xml non-compliant")

    try:
        load_expectations(Path("/nonexistent.pdf.json"))
        print("FAIL self-test load_expectations should exit")
        ok = False
    except SystemExit as exc:
        if "invalid expectations" not in str(exc):
            print(f"FAIL self-test load_expectations message: {exc}")
            ok = False
        else:
            print("ok   load_expectations rejects missing file")

    with tempfile.NamedTemporaryFile("w", suffix=".pdf.json", delete=False) as handle:
        json.dump({"pdf": "/tmp/x.pdf"}, handle)
        good = Path(handle.name)
    try:
        load_expectations(good)
        print("ok   load_expectations minimal schema")
    except SystemExit:
        print("FAIL self-test minimal expectations")
        ok = False
    finally:
        good.unlink(missing_ok=True)

    with tempfile.NamedTemporaryFile("w", suffix=".pdf.json", delete=False) as handle:
        json.dump({}, handle)
        bad = Path(handle.name)
    try:
        load_expectations(bad)
        print("FAIL self-test empty expectations should exit")
        ok = False
    except SystemExit as exc:
        if "missing non-empty string field 'pdf'" not in str(exc):
            print(f"FAIL self-test empty expectations message: {exc}")
            ok = False
        else:
            print("ok   load_expectations rejects empty object")

    bad.unlink(missing_ok=True)

    if not verapdf_is_missing(None):
        print("FAIL self-test verapdf_is_missing(None)")
        ok = False
    elif verapdf_is_missing("/usr/bin/verapdf"):
        print("FAIL self-test verapdf present path treated as missing")
        ok = False
    elif not verapdf_is_missing("/usr/bin/verapdf", FileNotFoundError("verapdf")):
        print("FAIL self-test FileNotFoundError should mean verapdf is missing")
        ok = False
    else:
        print("ok   verapdf absence is FileNotFoundError or no PATH entry")

    if not stream_has_rgb("0.769 0.118 0.227 scn", (0.768627, 0.117647, 0.227451)):
        print("FAIL self-test stream_has_rgb")
        ok = False
    elif stream_has_rgb("0.1 0.2 0.3 rg", (0.768627, 0.117647, 0.227451)):
        print("FAIL self-test stream_has_rgb false positive")
        ok = False
    else:
        print("ok   stream_has_rgb")

    figure_first = {
        "qpdf": [
            {
                "/Type": "/StructElem",
                "/S": "/Document",
                "/K": [
                    {"/Type": "/StructElem", "/S": "/Figure"},
                    {"/Type": "/StructElem", "/S": "/P"},
                ],
            }
        ]
    }
    got_order = document_struct_child_types(figure_first)
    if got_order != ["Figure", "P"]:
        print(f"FAIL self-test document_struct_child_types pass case: {got_order!r}")
        ok = False
    else:
        print("ok   document_struct_child_types pass case")
    if ["P", "P"] == ["Figure", "P"]:
        print("FAIL self-test document_child_types fail case (indistinguishable order)")
        ok = False
    else:
        print("ok   document_child_types fail case (P,P != Figure,P)")

    raw_cr = b"/Alt (\xfe\xff\x00\r)"
    lits = iter_pdf_literals(raw_cr, b"/Alt")
    if len(lits) != 1 or 0x0D not in lits[0].raw_controls or utf16_pdf_text(lits[0].decoded) == "\r":
        print(f"FAIL self-test raw CR in a UTF-16 literal must not count as escaped: {lits!r}")
        ok = False
    else:
        print("ok   raw CR inside a PDF literal is a control, not an escape")
    # Each control is its own UTF-16BE code unit: a 0x00 high byte plus the
    # escaped low byte. Consecutive escapes without the high byte are not "\r\n\t\b\f".
    escaped = b"/Alt (\xfe\xff\x00\\r\x00\\n\x00\\t\x00\\b\x00\\f)"
    elits = iter_pdf_literals(escaped, b"/Alt")
    if (
        len(elits) != 1
        or elits[0].raw_controls
        or utf16_pdf_text(elits[0].decoded) != "\r\n\t\b\f"
    ):
        got = utf16_pdf_text(elits[0].decoded) if elits else None
        print(f"FAIL self-test named escapes: got {got!r} controls={elits[0].raw_controls if elits else None}")
        ok = False
    else:
        print("ok   named \r \n \t \b \f escapes decode")

    if run_wave25_suite(cli) != 0:
        ok = False

    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, default=REPO / "build/linux/aioffice-cli")
    parser.add_argument("--playbook", type=Path, help="qa/wave19-pdf-*.json playbook")
    parser.add_argument("--expect", type=Path, help="expectations .pdf.json (default: playbook + .pdf.json)")
    parser.add_argument("--self-test", action="store_true", help="unit checks for parsers and schema")
    parser.add_argument("--wave25", action="store_true", help="run qa/wave25 PDF structure checks")
    parser.add_argument("--skip-play", action="store_true", help="only validate an existing PDF")
    args = parser.parse_args()

    if args.self_test:
        return self_test(args.cli)
    if args.wave25:
        return run_wave25_suite(args.cli.resolve())

    if not args.playbook:
        raise SystemExit("usage: check_pdf.py --playbook qa/wave19-pdf-….json [--expect …] | --self-test")
    playbook = args.playbook.resolve()
    expect_path = (args.expect or playbook.with_suffix(".pdf.json")).resolve()
    exp = load_expectations(expect_path)
    pdf = Path(exp["pdf"])
    if not pdf.is_absolute():
        pdf = (REPO / pdf).resolve()

    cli = args.cli.resolve()
    if not args.skip_play:
        if not cli.is_file():
            raise SystemExit(f"missing cli: {cli}")
        play_warnings = play(cli, playbook)
    else:
        play_warnings = []

    if not pdf.is_file():
        raise SystemExit(f"PDF not found after play: {pdf}")

    errors = check_expectations(pdf, exp, play_warnings, cli)
    errors.extend(check_wave25_extra(pdf, exp))
    if errors:
        for err in errors:
            print(f"FAIL {err}")
        return 1
    print(f"ok   {playbook.name} -> {pdf.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
