#!/usr/bin/env python3
import glob
import json
import os
import sys
import zipfile

base = sys.argv[1] if len(sys.argv) > 1 else "/tmp/aioffice-qa2"


def types(path):
    with open(path) as f:
        d = json.load(f)
    doc = d.get("document") or d
    body = doc.get("body") or []
    page = (doc.get("meta") or {}).get("page") or {}
    return [b.get("type") for b in body], page, body, doc


docs = {}
for name in ("from_json", "from_odt", "from_docx"):
    t, p, body, doc = types(os.path.join(base, f"{name}.json"))
    docs[name] = doc
    print(name, t, p)
    for need in ("table", "list-item", "image", "break"):
        if need not in t:
            raise SystemExit(f"{name} missing {need}: {t}")
print("round-trip types OK")


def ctl_attrs(body):
    """Block indices that carry dir or logical align in the source document."""
    out = []
    for i, b in enumerate(body):
        d = b.get("dir") or ""
        a = b.get("align") or ""
        if d or a in ("start", "end"):
            out.append((i, d, a))
    return out


base_body = docs["from_json"].get("body") or []
ctl = ctl_attrs(base_body)
if ctl:
    for name in ("from_odt", "from_docx"):
        body = docs[name].get("body") or []
        for i, want_dir, want_align in ctl:
            if i >= len(body):
                raise SystemExit(f"{name} body shorter than source at index {i}")
            got = body[i]
            got_dir = got.get("dir") or ""
            if got_dir != want_dir:
                raise SystemExit(
                    f"{name} body[{i}] dir mismatch: {got_dir!r} vs {want_dir!r}"
                )
            got_align = got.get("align") or ""
            if want_align and got_align != want_align:
                raise SystemExit(
                    f"{name} body[{i}] align mismatch: {got_align!r} vs {want_align!r}"
                )
    print("round-trip dir/align OK")

# wave12: bookmarks/revisions must survive ODT/DOCX round-trip as real markup, not just internally
# (see Odf.cpp/Docx.cpp bookmarkStart/End, text:change-*/w:ins). Only checked if the source has any.
base_bm = len(docs["from_json"].get("bookmarks") or [])
base_revs = docs["from_json"].get("revisions") or []
base_rev = len(base_revs)
need_ins = any(r.get("kind") == "insert" for r in base_revs)
need_del = any(r.get("kind") == "delete" for r in base_revs)
if base_bm or base_rev:
    for name in ("from_odt", "from_docx"):
        nbm = len(docs[name].get("bookmarks") or [])
        nrev = len(docs[name].get("revisions") or [])
        if base_bm and nbm != base_bm:
            raise SystemExit(f"{name} bookmark count mismatch: {nbm} vs {base_bm}")
        if base_rev and nrev != base_rev:
            raise SystemExit(f"{name} revision count mismatch: {nrev} vs {base_rev}")
    print("round-trip bookmarks/revisions OK")

    def native_xml(path, member):
        with zipfile.ZipFile(path) as z:
            return z.read(member).decode("utf-8", "replace")

    office_files = []
    office_files += glob.glob(os.path.join(base, "*.odt"))
    office_files += glob.glob(os.path.join(base, "*.docx"))
    for extra in sys.argv[2:]:
        office_files.append(extra)

    for path in office_files:
        if not os.path.isfile(path):
            continue
        if path.endswith(".odt"):
            xml = native_xml(path, "content.xml")
            if base_bm and "text:bookmark-start" not in xml:
                raise SystemExit(f"{path} missing native ODT bookmark markup")
            if need_ins and "text:change-start" not in xml and "text:changed-region" not in xml:
                raise SystemExit(f"{path} missing native ODT insert markup")
            print("native ODT markup OK", path)
        elif path.endswith(".docx"):
            xml = native_xml(path, "word/document.xml")
            if base_bm and "w:bookmarkStart" not in xml:
                raise SystemExit(f"{path} missing native DOCX bookmark markup")
            if need_ins and "<w:ins" not in xml:
                raise SystemExit(f"{path} missing native DOCX insert markup")
            if need_del and "<w:del" not in xml:
                raise SystemExit(f"{path} missing native DOCX delete markup")
            print("native DOCX markup OK", path)
