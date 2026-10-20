#!/usr/bin/env python3
"""Prove wave 19 D4–D6 on one PDF: structure via qpdf, no fallback /Part, trailer /ID.

The playbook exports the document in body order. This script then sets
reading_order so the image precedes the heading and exports again.
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLAY = ROOT / "qa" / "wave19-pdf-tags.json"
AIO = Path("/tmp/wave19-tags.aio.json")
PDF = Path("/tmp/wave19-tags-order.pdf")
CLI = ROOT / "build" / "linux" / "aioffice-cli"


def run(cmd):
    r = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout)
        sys.stderr.write(r.stderr)
        raise SystemExit(f"failed: {' '.join(cmd)}")
    return r.stdout


def main():
    run([str(CLI), "play", str(PLAY)])
    doc = json.loads(AIO.read_text())
    body = doc["body"]
    by_type = {}
    for b in body:
        by_type.setdefault(b["type"], []).append(b["id"])
    image = by_type["image"][0]
    heading = by_type["heading"][0]
    order = [b["id"] for b in body]
    order.remove(image)
    order.insert(order.index(heading), image)
    doc["meta"]["reading_order"] = order
    ordered_path = Path("/tmp/wave19-tags-order.aio.json")
    ordered_path.write_text(json.dumps(doc))
    play = [
        {"tool": "document_open", "args": {"path": str(ordered_path)}},
        {"tool": "document_export_pdf", "args": {"path": str(PDF)}},
    ]
    play_path = Path("/tmp/wave19-tags-order-play.json")
    play_path.write_text(json.dumps(play))
    run([str(CLI), "play", str(play_path)])

    raw = PDF.read_bytes()
    trailers = raw.split(b"trailer")
    if len(trailers) < 3:
        raise SystemExit(f"expected original trailer plus incremental update, found {len(trailers) - 1}")
    if b"/ID" not in trailers[-1]:
        raise SystemExit("incremental trailer has no /ID")

    run(["qpdf", "--check", str(PDF)])
    out = run(["qpdf", "--json=1", str(PDF)])
    data = json.loads(out)
    objects = data["objects"]

    def deref(val):
        if isinstance(val, str) and val.endswith(" 0 R"):
            return objects.get(val)
        return val

    types = set()
    catalog_lang = None
    figure_alt = None
    span_lang = None
    doc_kids = []
    for obj in objects.values():
        if not isinstance(obj, dict):
            continue
        if obj.get("/Type") == "/Catalog" and "/Lang" in obj:
            catalog_lang = obj["/Lang"]
        s = obj.get("/S")
        if not isinstance(s, str):
            continue
        types.add(s)
        if s == "/Figure" and obj.get("/Alt"):
            figure_alt = obj["/Alt"]
        if s == "/Span" and obj.get("/Lang"):
            span_lang = obj["/Lang"]
        if s == "/Document" and isinstance(obj.get("/K"), list):
            for kid in obj["/K"]:
                node = deref(kid)
                if isinstance(node, dict) and isinstance(node.get("/S"), str):
                    doc_kids.append(node["/S"])

    print("catalog /Lang:", catalog_lang)
    print("document children:", doc_kids)
    print("structure types:", sorted(set(types)))
    print("figure /Alt:", figure_alt)
    print("span /Lang:", span_lang)

    missing = [t for t in ("/L", "/LI", "/Lbl", "/LBody", "/Table", "/TR", "/TH", "/TD", "/Figure", "/Note", "/TOC", "/TOCI", "/Span") if t not in types]
    if missing:
        raise SystemExit("missing structure types: " + ", ".join(missing))
    if "/Part" in types:
        raise SystemExit("fallback /Part is present")
    if not catalog_lang or "pt-PT" not in str(catalog_lang):
        raise SystemExit(f"catalog /Lang is not pt-PT: {catalog_lang}")
    if not span_lang or "en-US" not in str(span_lang):
        raise SystemExit(f"span /Lang is not en-US: {span_lang}")
    if not figure_alt or "Ponto" not in str(figure_alt):
        raise SystemExit(f"figure /Alt missing: {figure_alt}")
    if not doc_kids or doc_kids[0] != "/Figure":
        raise SystemExit(f"reading order did not put /Figure first: {doc_kids}")
    if "/H1" not in doc_kids[1:3]:
        raise SystemExit(f"heading not near the front after the figure: {doc_kids}")

    try:
        verapdf = subprocess.run(["verapdf", "--flavour", "ua1", str(PDF)], text=True, capture_output=True)
    except FileNotFoundError:
        print("verapdf not on PATH")
    else:
        sys.stdout.write(verapdf.stdout[-4000:])
        sys.stderr.write(verapdf.stderr[-2000:])
        print("verapdf exit", verapdf.returncode)
    print("ok")


if __name__ == "__main__":
    main()
