#!/usr/bin/env python3
import json
import os
import sys

base = sys.argv[1] if len(sys.argv) > 1 else "/tmp/aioffice-qa2"


def types(path):
    with open(path) as f:
        d = json.load(f)
    doc = d.get("document") or d
    body = doc.get("body") or []
    page = (doc.get("meta") or {}).get("page") or {}
    return [b.get("type") for b in body], page, body


for name in ("from_json", "from_odt", "from_docx"):
    t, p, body = types(os.path.join(base, f"{name}.json"))
    print(name, t, p)
    for need in ("table", "list-item", "image", "break"):
        if need not in t:
            raise SystemExit(f"{name} missing {need}: {t}")
print("round-trip types OK")
