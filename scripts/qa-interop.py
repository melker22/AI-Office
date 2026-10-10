#!/usr/bin/env python3
"""LibreOffice round-trip of insert+delete+format revisions (wave 17 D8).

Exports a document through aioffice-cli, converts docx→odt and odt→docx with
soffice, reimports the native markup, and compares kind, block, offsets, text
and author. A format revision that passed through ODT must not carry prior_runs.
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
    path.write_text(json.dumps(calls), encoding="utf-8")
    env = os.environ.copy()
    env["AIOFFICE_HEADLESS"] = "1"
    env["QT_QPA_PLATFORM"] = "offscreen"
    proc = subprocess.run(
        [cli, "play", str(path)],
        cwd=ROOT,
        env=env,
        capture_output=True,
        text=True,
    )
    path.unlink(missing_ok=True)
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
        return status
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
