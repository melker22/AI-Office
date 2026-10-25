#!/usr/bin/env python3
"""Collect every CodeRabbit finding from AI Office PRs: inline comments + review-body sections
(outside-diff, duplicate, nitpick), robust to both old (<em>) and new (<strong>) body formats."""
import importlib.util, json, re, sys, urllib.request
from pathlib import Path
_spec = importlib.util.spec_from_file_location("pr_merge_gate", Path(__file__).with_name("pr-merge-gate.py"))
_gate = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(_gate)
_ALL_BODY_KINDS = ("outside-diff", "duplicate", "nitpick", "body-critical", "body-major", "body-minor", "body-trivial")
API = "https://api.github.com/repos/melker22/AI-Office"
def get(u):
    """Every page of a list endpoint (or one object), through the merge gate's authenticated get()."""
    out=[]; page=1
    while True:
        d=_gate.get(f"{u}{'&' if '?' in u else '?'}per_page=100&page={page}")
        if not isinstance(d,list): return d
        out+=d
        if len(d)<100: return out
        page+=1
def clean(b):
    b=re.sub(r'<!--.*?-->','',b,flags=re.S)
    b=re.sub(r'<details>\s*<summary>(🤖 Prompt for AI Agents|🧩 Analysis chain|🔎 Supported by static analysis|🧰 Tools|📝 Committable suggestion)</summary>.*?</details>','',b,flags=re.S)
    return re.sub(r'\n\s*>\s?','\n',b).strip()
SEV=r'(Critical|Major|Minor|Trivial|Nitpick)'
def sev(b):
    m=re.search(SEV,b); return m.group(1) if m else "?"
def title(b):
    for m in re.finditer(r'^\*\*(.{8,240}?)\*\*[ \t]*$', b, re.M):
        t=m.group(1)
        if not re.search(SEV+r'|Quick win|Heavy lift|Data Integrity|Correctness|Performance|Security|Maintainab', t): return t
    return b.strip().splitlines()[0][:160] if b.strip() else "?"
findings=[]
for n in [int(x) for x in sys.argv[1:]]:
    for c in get(f"{API}/pulls/{n}/comments"):
        if not c["user"]["login"].startswith("coderabbit") or c.get("in_reply_to_id"): continue
        b=clean(c["body"])
        findings.append(dict(pr=n, kind="inline", severity=sev(c["body"]), path=c["path"], line=c.get("line") or c.get("original_line"),
            title=title(b), url=c["html_url"], created=c["created_at"], addressed=("Addressed in" in c["body"] or "✅" in c["body"][:400]), body=b[:3500]))
    # Review-body findings: same parser as the merge gate (both body formats, icons in the severity tag,
    # grouped "<Severity> comments: N" sections). `cr_id` is the CodeRabbit marker id used by `cr-ack`.
    for r in get(f"{API}/pulls/{n}/reviews"):
        if not r["user"]["login"].startswith("coderabbit"): continue
        for f in _gate.body_findings(r, kinds=_ALL_BODY_KINDS):
            loc=f["loc"] if f["loc"]!="?" else ""
            path, _, lines = loc.rpartition(":") if ":" in loc else (loc or "?", "", "")
            severity = f["kind"][5:].capitalize() if f["kind"].startswith("body-") else sev(f["text"])
            findings.append(dict(pr=n, kind=f["kind"], severity=severity, path=path or "?", line=lines or None,
                title=f["title"], cr_id=f["id"], url=r["html_url"], created=r["submitted_at"], addressed=False,
                body=clean(f["text"])[:3500]))
json.dump(findings, sys.stdout, ensure_ascii=False, indent=1)
