#!/usr/bin/env python3
"""LibreOffice round-trip of revisions (wave 17) and dynamic fields (wave 18 D7).

Exports a document through aioffice-cli, converts with soffice, strips embedded
JSON, and reimports. Revisions compare kind, block, offsets, text, and author.
Fields compare the ODT resave (D6 elements) plus odt→docx instructions and the
text that survives docx→odt.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "qa" / "wave17-odt-revisions.aio.json"


def play(cli, calls):
    fd, name = tempfile.mkstemp(suffix=".json")
    os.close(fd)
    path = Path(name)
    try:
        path.write_text(json.dumps(calls), encoding="utf-8")
        env = os.environ.copy()
        env["AIOFFICE_HEADLESS"] = "1"
        env["QT_QPA_PLATFORM"] = "offscreen"
        try:
            proc = subprocess.run(
                [cli, "play", str(path)],
                cwd=ROOT,
                env=env,
                capture_output=True,
                text=True,
                timeout=300,
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            raise SystemExit("aioffice-cli play timed out after 300s") from exc
        raw = proc.stdout
        start = raw.find("{")
        if proc.returncode != 0 or start < 0:
            sys.stderr.write(proc.stdout)
            sys.stderr.write(proc.stderr)
            raise SystemExit(f"aioffice-cli play failed ({proc.returncode})")
        data = json.loads(raw[start:])
        if not data.get("ok"):
            print(json.dumps(data, indent=2)[:4000])
            raise SystemExit("aioffice-cli play returned ok:false")
        return data
    finally:
        path.unlink(missing_ok=True)


def convert(src, outdir, target):
    profile = tempfile.mkdtemp(prefix="aioffice-lo-")
    try:
        proc = subprocess.run(
            [
                "soffice",
                "--headless",
                "--nolockcheck",
                "--nologo",
                "--norestore",
                f"-env:UserInstallation=file://{profile}",
                "--convert-to",
                target,
                "--outdir",
                str(outdir),
                str(src),
            ],
            capture_output=True,
            text=True,
            timeout=180,
            check=False,
        )
    finally:
        shutil.rmtree(profile, ignore_errors=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise SystemExit(f"soffice --convert-to {target} failed ({proc.returncode})")


def strip_native_json(path: Path):
    """Drop the embedded aioffice JSON so import reads LibreOffice markup only."""
    tmp = path.with_suffix(path.suffix + ".strip")
    with zipfile.ZipFile(path, "r") as zin, zipfile.ZipFile(tmp, "w") as zout:
        for info in zin.infolist():
            data = zin.read(info.filename)
            name = info.filename
            if name == "word/aioffice.json":
                continue
            if name == "[Content_Types].xml":
                text = data.decode("utf-8")
                text = text.replace(
                    '<Override PartName="/word/aioffice.json" ContentType="application/json"/>',
                    "",
                )
                data = text.encode("utf-8")
            if name == "meta.xml" and b"aioffice:extra" in data:
                text = data.decode("utf-8")
                start = text.find("<meta:user-defined")
                while start >= 0:
                    end = text.find("</meta:user-defined>", start)
                    chunk = text[start : end + len("</meta:user-defined>")] if end >= 0 else ""
                    if "aioffice:extra" in chunk:
                        text = text[:start] + text[end + len("</meta:user-defined>") :]
                        start = text.find("<meta:user-defined", start)
                    else:
                        start = text.find("<meta:user-defined", start + 1)
                data = text.encode("utf-8")
            zout.writestr(info, data)
    tmp.replace(path)


def block_index(body, block_id):
    for i, block in enumerate(body):
        if block.get("id") == block_id:
            return i
    return block_id


def norm(revs, body):
    rows = []
    for rev in revs:
        rows.append(
            {
                "kind": rev.get("kind") or "",
                "block": block_index(body, rev.get("block_id")),
                "start": rev.get("start"),
                "end": rev.get("end"),
                "text": rev.get("text") or "",
                "author": rev.get("author") or "",
            }
        )
    rows.sort(key=lambda r: (str(r["block"]), r["start"], r["end"], r["kind"], r["text"], r["author"]))
    return rows


def check_prior(revs, label):
    for rev in revs:
        if rev.get("kind") != "format":
            continue
        prior = rev.get("prior_runs") or []
        if prior:
            print(f"FAIL {label}: format revision kept prior_runs after ODT passage")
            print(json.dumps(rev, ensure_ascii=False))
            return False
    return True


def plain_of(body):
    parts = []
    for block in body:
        text = "".join((run.get("text") or "") for run in (block.get("runs") or []))
        hide = block.get("hide_if") or ""
        if text or hide:
            parts.append(text)
    return "\n".join(parts)


def load_arg(run):
    raw = run.get("field_arg") or ""
    if not raw:
        return {}
    try:
        value = json.loads(raw)
    except json.JSONDecodeError:
        return {"_raw": raw}
    return value if isinstance(value, dict) else {"_raw": raw}


def fields_of(body):
    found = []
    for index, block in enumerate(body):
        for run in block.get("runs") or []:
            if not run.get("field"):
                continue
            found.append(
                {
                    "index": index,
                    "field": run.get("field") or "",
                    "arg": run.get("field_arg") or "",
                    "obj": load_arg(run),
                    "text": run.get("text") or "",
                    "hide": block.get("hide_if") or "",
                }
            )
        if not any(run.get("field") for run in (block.get("runs") or [])) and block.get("hide_if"):
            found.append(
                {
                    "index": index,
                    "field": "",
                    "arg": "",
                    "obj": {},
                    "text": "".join((run.get("text") or "") for run in (block.get("runs") or [])),
                    "hide": block.get("hide_if") or "",
                }
            )
    return found


def require(ok, label, detail):
    if ok:
        return True
    print(f"FAIL interop {label}: {detail}")
    return False


def build_fields_doc(cli, work: Path):
    odt = work / "fields.odt"
    docx = work / "fields.docx"
    calls = [
        {"tool": "document_new", "args": {"title": "Wave18 interop fields", "force": True}},
        {"tool": "document_set_title", "args": {"subject": "Subj", "keywords": "k1,k2"}},
        {
            "tool": "set_vars",
            "args": {
                "vars": {
                    "status": "go",
                    "sender.name": "Ada Lovelace",
                    "doc.created": "2020-01-02T03:04:05",
                    "doc.modified": "2021-02-03T04:05:06",
                }
            },
        },
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {"id": "pIf", "type": "paragraph", "runs": [{"text": ""}]},
                    {"id": "pSeq", "type": "paragraph", "runs": [{"text": ""}]},
                    {"id": "pPages", "type": "paragraph", "runs": [{"text": ""}]},
                    {"id": "pChars", "type": "paragraph", "runs": [{"text": ""}]},
                    {"id": "pSubj", "type": "paragraph", "runs": [{"text": ""}]},
                    {"id": "pKw", "type": "paragraph", "runs": [{"text": ""}]},
                    {"id": "pCreated", "type": "paragraph", "runs": [{"text": ""}]},
                    {"id": "pMod", "type": "paragraph", "runs": [{"text": ""}]},
                    {"id": "pRev", "type": "paragraph", "runs": [{"text": ""}]},
                    {"id": "pName", "type": "paragraph", "runs": [{"text": ""}]},
                    {"id": "pList", "type": "paragraph", "runs": [{"text": ""}]},
                    {"id": "pHide", "type": "paragraph", "runs": [{"text": "Secret"}]},
                ],
            },
        },
        {
            "tool": "insert_field",
            "args": {
                "id": "pIf",
                "offset": 0,
                "kind": "if",
                "arg": {"cond": 'status == "go"', "then": "YES", "else": "NO"},
            },
        },
        {
            "tool": "insert_field",
            "args": {
                "id": "pSeq",
                "offset": 0,
                "kind": "seq",
                "arg": '{"name":"Cap","format":"arabic"}',
            },
        },
        {"tool": "insert_field", "args": {"id": "pPages", "offset": 0, "kind": "stat", "arg": "pages"}},
        {"tool": "insert_field", "args": {"id": "pChars", "offset": 0, "kind": "stat", "arg": "chars"}},
        {"tool": "insert_field", "args": {"id": "pSubj", "offset": 0, "kind": "doc_info", "arg": "subject"}},
        {"tool": "insert_field", "args": {"id": "pKw", "offset": 0, "kind": "doc_info", "arg": "keywords"}},
        {"tool": "insert_field", "args": {"id": "pCreated", "offset": 0, "kind": "doc_info", "arg": "created"}},
        {"tool": "insert_field", "args": {"id": "pMod", "offset": 0, "kind": "doc_info", "arg": "modified"}},
        {"tool": "insert_field", "args": {"id": "pRev", "offset": 0, "kind": "doc_info", "arg": "revision"}},
        {"tool": "insert_field", "args": {"id": "pName", "offset": 0, "kind": "sender", "arg": "name"}},
        {
            "tool": "insert_field",
            "args": {
                "id": "pList",
                "offset": 0,
                "kind": "input_list",
                "arg": '{"options":["Um","Dois"],"selected":"Dois"}',
            },
        },
        {"tool": "apply_paragraph_format", "args": {"id": "pHide", "hide_if": 'status == "go"'}},
        {"tool": "document_save", "args": {"path": str(odt)}},
        {"tool": "document_save", "args": {"path": str(docx)}},
    ]
    play(cli, calls)
    return odt, docx


def assert_odt_markup(odt: Path):
    with zipfile.ZipFile(odt) as zf:
        xml = zf.read("content.xml").decode("utf-8")
    needed = [
        "xmlns:ooow",
        "<text:conditional-text",
        "<text:sequence-decls>",
        '<text:sequence text:name="Cap"',
        "<text:page-count>",
        "<text:character-count>",
        "<text:subject>",
        "<text:keywords>",
        "<text:creation-date>",
        "<text:modification-date>",
        "<text:editing-cycles>",
        "<text:sender-firstname>",
        "<text:hidden-paragraph",
        "<text:drop-down",
    ]
    missing = [tag for tag in needed if tag not in xml]
    return require(not missing, "odt markup", "missing " + ", ".join(missing))


def assert_resaved_fields(body):
    """LibreOffice writer8 keeps D6 elements. Stat totals are recalculated; dates follow the locale."""
    ok = True
    rows = fields_of(body)
    ifs = [row for row in rows if row["field"] == "if"]
    ok = require(len(ifs) == 1, "odt resave if", f"count {len(ifs)}") and ok
    if ifs:
        obj = ifs[0]["obj"]
        ok = require(
            obj.get("cond") == 'status == "go"' and obj.get("then") == "YES" and obj.get("else") == "NO",
            "odt resave if",
            json.dumps(obj, ensure_ascii=False),
        ) and ok
        ok = require(ifs[0]["text"] == "YES", "odt resave if text", ifs[0]["text"]) and ok
    seqs = [row for row in rows if row["field"] == "seq"]
    ok = require(len(seqs) == 1, "odt resave seq", f"count {len(seqs)}") and ok
    if seqs:
        obj = seqs[0]["obj"]
        ok = require(
            obj.get("name") == "Cap" and obj.get("format") == "arabic" and seqs[0]["text"] == "1",
            "odt resave seq",
            json.dumps(seqs[0], ensure_ascii=False),
        ) and ok
    chars = [row for row in rows if row["field"] == "stat" and row["arg"] == "chars"]
    ok = require(len(chars) == 1, "odt resave stat chars", f"count {len(chars)}") and ok
    pages = [row for row in rows if row["field"] == "page_count"]
    ok = require(len(pages) == 1, "odt resave stat pages", f"count {len(pages)}") and ok
    subj = [row for row in rows if row["field"] == "doc_info" and row["arg"] == "subject"]
    ok = require(len(subj) == 1 and subj[0]["text"] == "Subj", "odt resave subject", str(subj)) and ok
    names = [row for row in rows if row["field"] == "sender" and row["arg"] == "name"]
    ok = require(
        len(names) == 1 and names[0]["text"] == "Ada Lovelace", "odt resave sender", str(names)
    ) and ok
    lists = [row for row in rows if row["field"] == "input_list"]
    ok = require(len(lists) == 1, "odt resave drop-down", f"count {len(lists)}") and ok
    if lists:
        obj = lists[0]["obj"]
        ok = require(
            obj.get("selected") == "Dois" and obj.get("options") == ["Um", "Dois"],
            "odt resave drop-down",
            json.dumps(obj, ensure_ascii=False),
        ) and ok
    hidden = [block for block in body if (block.get("hide_if") or "") == 'status == "go"']
    ok = require(len(hidden) == 1, "odt resave hide_if", f"count {len(hidden)}") and ok
    if hidden:
        text = "".join((run.get("text") or "") for run in (hidden[0].get("runs") or []))
        ok = require(text == "Secret", "odt resave hide text", text) and ok
    return ok


def assert_docx_instructions(docx: Path):
    with zipfile.ZipFile(docx) as zf:
        xml = zf.read("word/document.xml").decode("utf-8")
    needed = [
        " IF ",
        " SEQ Cap ",
        " NUMPAGES ",
        " NUMCHARS ",
        " SUBJECT ",
        " USERNAME ",
    ]
    missing = [token for token in needed if token not in xml]
    return require(not missing, "odt→docx instructions", "missing " + ", ".join(missing))


def assert_plain_contains(body, label, needles):
    plain = plain_of(body)
    missing = [needle for needle in needles if needle not in plain]
    return require(not missing, label, f"missing {missing} in {plain!r}")


def build_rtf_interop_doc(cli, work: Path):
    rtf = work / "wave20-interop.rtf"
    odt = work / "wave20-interop.odt"
    calls = [
        {"tool": "document_new", "args": {"title": "Wave20 RTF interop", "force": True}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "start",
                "blocks": [
                    {"type": "heading", "level": 1, "runs": [{"text": "Wave20 RTF interop"}]},
                    {"type": "paragraph", "runs": [{"text": "Alpha paragraph."}]},
                    {"type": "list-item", "list_kind": "bullet", "runs": [{"text": "List A"}]},
                    {"type": "table", "header": True, "rows": 2, "cols": 2},
                ],
            },
        },
        {"tool": "set_cell", "args": {"id": "tbl1", "row": 0, "col": 0, "text": "H"}},
        {"tool": "set_cell", "args": {"id": "tbl1", "row": 0, "col": 1, "text": "V"}},
        {"tool": "set_cell", "args": {"id": "tbl1", "row": 1, "col": 0, "text": "1"}},
        {"tool": "set_cell", "args": {"id": "tbl1", "row": 1, "col": 1, "text": "2"}},
        {"tool": "document_save", "args": {"path": str(rtf)}},
        {"tool": "document_save", "args": {"path": str(odt)}},
    ]
    play(cli, calls)
    needles = ["Wave20 RTF interop", "Alpha", "List A", "H", "V"]
    return rtf, odt, needles


def rtf_interop_roundtrip(cli):
    """Wave 20 D6: our RTF through LibreOffice and LO RTF into native import."""
    work = Path(tempfile.mkdtemp(prefix="aioffice-interop-rtf-"))
    try:
        rtf, odt, needles = build_rtf_interop_doc(cli, work)
        if not rtf.is_file() or not odt.is_file():
            print("FAIL interop rtf: native export did not write RTF/ODT")
            return 1
        ok = True
        lo_odt_dir = work / "rtf-to-odt"
        lo_rtf_dir = work / "odt-to-rtf"
        lo_odt_dir.mkdir()
        lo_rtf_dir.mkdir()
        convert(rtf, lo_odt_dir, "odt")
        convert(odt, lo_rtf_dir, "rtf")
        odt_out = lo_odt_dir / rtf.with_suffix(".odt").name
        rtf_out = lo_rtf_dir / odt.with_suffix(".rtf").name
        if not odt_out.is_file():
            print("FAIL interop our RTF→ODT: soffice did not write ODT")
            return 1
        strip_native_json(odt_out)
        _revs, body_lo = snapshot(cli, odt_out)
        ok = assert_plain_contains(body_lo, "our RTF→ODT", needles) and ok
        if rtf_out.is_file():
            _revs, body_native = snapshot(cli, rtf_out)
            ok = assert_plain_contains(body_native, "LO RTF→native", needles) and ok
        else:
            print("FAIL interop ODT→RTF: soffice did not write RTF")
            ok = False
        if ok:
            print("ok   interop rtf our RTF→ODT / LO RTF→native")
        return 0 if ok else 1
    finally:
        shutil.rmtree(work, ignore_errors=True)


def fields_roundtrip(cli):
    """D7: if/seq/stat/hide_if through LibreOffice.

    ODT resave keeps the D6 elements. odt→docx is checked on the Word instructions
    LibreOffice writes and on the evaluated text our importer can see (complex
    fields are not fldSimple). docx→odt checks the cached text that survives the
    current DOCX export; native IF/SEQ export is the parallel DOCX task.
    """
    work = Path(tempfile.mkdtemp(prefix="aioffice-interop-fields-"))
    try:
        odt, docx = build_fields_doc(cli, work)
        ok = assert_odt_markup(odt)
        resaved = work / "resaved"
        to_docx = work / "to-docx"
        to_odt = work / "to-odt"
        convert(odt, resaved, "odt")
        convert(odt, to_docx, "docx")
        convert(docx, to_odt, "odt")
        resaved_odt = resaved / "fields.odt"
        docx_out = to_docx / "fields.docx"
        odt_out = to_odt / "fields.odt"
        if not resaved_odt.is_file() or not docx_out.is_file() or not odt_out.is_file():
            print("FAIL interop fields: soffice did not write a converted file")
            return 1
        strip_native_json(resaved_odt)
        strip_native_json(docx_out)
        strip_native_json(odt_out)
        _revs, resaved_body = snapshot(cli, resaved_odt)
        _revs, docx_body = snapshot(cli, docx_out)
        _revs, odt_body = snapshot(cli, odt_out)
        ok = assert_resaved_fields(resaved_body) and ok
        ok = assert_docx_instructions(docx_out) and ok
        ok = (
            assert_plain_contains(
                docx_body, "odt→docx text", ["YES", "1", "Subj", "Ada Lovelace", "Dois", "Secret"]
            )
            and ok
        )
        ok = (
            assert_plain_contains(
                odt_body, "docx→odt text", ["Subj", "Ada Lovelace", "Dois", "Secret"]
            )
            and ok
        )
        if ok:
            print("ok   interop fields odt resave / odt→docx / docx→odt")
        return 0 if ok else 1
    finally:
        shutil.rmtree(work, ignore_errors=True)


def snapshot(cli, path):
    data = play(
        cli,
        [
            {"tool": "document_open", "args": {"path": str(path)}},
            {"tool": "revision_list", "args": {}},
            {"tool": "document_get", "args": {}},
        ],
    )
    revs = data["results"][1].get("revisions") or []
    body = data["results"][2]["document"]["body"]
    return revs, body


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: qa-interop.py /path/to/aioffice-cli")
    cli = sys.argv[1]
    if not Path(cli).is_file():
        raise SystemExit(f"missing {cli}")
    if shutil.which("soffice") is None:
        print("skip interop")
        return 0

    work = Path(tempfile.mkdtemp(prefix="aioffice-interop-"))
    try:
        src = work / "src"
        to_odt = work / "docx-to-odt"
        to_docx = work / "odt-to-docx"
        src.mkdir()
        to_odt.mkdir()
        to_docx.mkdir()
        docx = src / "rev.docx"
        odt = src / "rev.odt"
        opened = play(
            cli,
            [
                {"tool": "document_open", "args": {"path": str(FIXTURE)}},
                {"tool": "revision_list", "args": {}},
                {"tool": "document_get", "args": {}},
                {"tool": "document_save", "args": {"path": str(docx)}},
                {"tool": "document_save", "args": {"path": str(odt)}},
            ],
        )
        base_revs = opened["results"][1].get("revisions") or []
        base_body = opened["results"][2]["document"]["body"]
        expect = norm(base_revs, base_body)

        convert(docx, to_odt, "odt")
        convert(odt, to_docx, "docx")
        odt_out = to_odt / "rev.odt"
        docx_out = to_docx / "rev.docx"
        if not odt_out.is_file() or not docx_out.is_file():
            print("FAIL interop: soffice did not write the converted file")
            print("odt", odt_out.exists(), "docx", docx_out.exists())
            return 1
        strip_native_json(odt_out)
        strip_native_json(docx_out)

        status = 0
        for label, path in (("docx→odt", odt_out), ("odt→docx", docx_out)):
            revs, body = snapshot(cli, path)
            got = norm(revs, body)
            if got != expect or not check_prior(revs, label):
                print(f"FAIL interop {label}")
                print("expect", json.dumps(expect, ensure_ascii=False))
                print("got   ", json.dumps(got, ensure_ascii=False))
                status = 1
            else:
                print(f"ok   interop {label}")
        status = fields_roundtrip(cli) or status
        status = rtf_interop_roundtrip(cli) or status
        return status
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
