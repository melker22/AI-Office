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

# wave12: bookmarks/revisions must survive ODT/DOCX round-trip as real markup, not just internally
# (see Odf.cpp/Docx.cpp bookmarkStart/End, text:change-*/w:ins). Only checked if the source has any.
base_bm = len(docs["from_json"].get("bookmarks") or [])
base_rev = len(docs["from_json"].get("revisions") or [])
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

    for path in glob.glob(os.path.join(base, "*.odt")) + glob.glob("/tmp/wave12-roundtrip.odt"):
        if not os.path.isfile(path):
            continue
        xml = native_xml(path, "content.xml")
        if base_bm and "text:bookmark-start" not in xml:
            raise SystemExit(f"{path} missing native ODT bookmark markup")
        if base_rev and "text:change-start" not in xml and "text:changed-region" not in xml:
            raise SystemExit(f"{path} missing native ODT revision markup")
        print("native ODT markup OK", path)

    for path in glob.glob(os.path.join(base, "*.docx")) + glob.glob("/tmp/wave12-roundtrip.docx"):
        if not os.path.isfile(path):
            continue
        xml = native_xml(path, "word/document.xml")
        if base_bm and "w:bookmarkStart" not in xml:
            raise SystemExit(f"{path} missing native DOCX bookmark markup")
        if base_rev and "<w:ins" not in xml:
            raise SystemExit(f"{path} missing native DOCX revision markup")
        print("native DOCX markup OK", path)
