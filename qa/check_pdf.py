#!/usr/bin/env python3
"""Wave 19 PDF QA: run a playbook export, then veraPDF, qpdf --json, and pdffonts."""

from __future__ import annotations

import argparse
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


def self_test() -> int:
    ok = True

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

    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, default=REPO / "build/linux/aioffice-cli")
    parser.add_argument("--playbook", type=Path, help="qa/wave19-pdf-*.json playbook")
    parser.add_argument("--expect", type=Path, help="expectations .pdf.json (default: playbook + .pdf.json)")
    parser.add_argument("--self-test", action="store_true", help="unit checks for parsers and schema")
    parser.add_argument("--skip-play", action="store_true", help="only validate an existing PDF")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

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
    if errors:
        for err in errors:
            print(f"FAIL {err}")
        return 1
    print(f"ok   {playbook.name} -> {pdf.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
