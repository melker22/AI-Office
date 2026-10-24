#!/usr/bin/env python3
"""Collect every CodeRabbit finding from AI Office PRs: inline comments + review-body sections
(outside-diff, duplicate, nitpick), robust to both old (<em>) and new (<strong>) body formats."""
import json, re, sys, urllib.request
API = "https://api.github.com/repos/melker22/AI-Office"
def get(u):
    out=[]; page=1
    while True:
        req=urllib.request.Request(f"{u}{'&' if '?' in u else '?'}per_page=100&page={page}", headers={"Accept":"application/vnd.github+json"})
        with urllib.request.urlopen(req, timeout=30) as r: d=json.load(r)
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
    m=re.search(r'\*\*([^*\n]{8,240})\*\*\s*\n', b)  # first bold sentence that is not the category table
    for m in re.finditer(r'\*\*([^*\n]{8,240})\*\*', b):
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
    for r in get(f"{API}/pulls/{n}/reviews"):
        if not r["user"]["login"].startswith("coderabbit"): continue
        body=r.get("body") or ""
        for _ in range(3): body=re.sub(r'\n[ \t]*>[ \t]?', '\n', body)
        # positions of section headers
        heads=[(m.start(), {'Outside diff range comments':'outside-diff','Duplicate comments':'duplicate','Nitpick comments':'nitpick','Additional comments':'additional'}[m.group(1)])
               for m in re.finditer(r'(Outside diff range comments|Duplicate comments|Nitpick comments|Additional comments)\s*(?:\(\d+\)|:\s*\d+)', body)]
        if not heads: continue
        stop=body.find('Prompt for all review comments'); stop = stop if stop>0 else len(body)
        prev=0
        for m in re.finditer(r'<!--\s*cr-comment:v1:[0-9a-f]+\s*-->', body):
            seg=body[prev:m.start()]; pos=prev; prev=m.end()
            if pos>stop: break
            kinds=[k for p,k in heads if p<=m.start()]
            if not kinds or kinds[-1]=='additional': continue
            kind=kinds[-1]
            # location: new/old summary line or nitpick "`lines`:" under "<summary>path (N)</summary>"
            loc=None
            mm=list(re.finditer(r'<code>([^<]+:\d[\d-]*)</code>\s*</summary>|<code>([^<]+:\d[\d-]*)</code>', seg))
            full=re.findall(r'`([\w./-]+/[\w.-]+:\d[\d-]*)`', seg)
            if full: loc=full[-1]
            elif mm: loc=(mm[-1].group(1) or mm[-1].group(2))
            else:
                fp=list(re.finditer(r'<summary>([^<()]+?)\s*\(\d+\)</summary>', body[:m.start()]))
                ln=list(re.finditer(r'`(\d+(?:-\d+)?)`:', seg))
                if fp: loc=fp[-1].group(1).strip()+(":"+ln[-1].group(1) if ln else "")
            # keep only the last finding inside seg
            starts=[x.start() for x in re.finditer(r'<details>\s*<summary>(?:<em>|<strong>)?\s*(?:🔴|🟠|🟡|🔵|🧹)?\s*'+SEV, seg)]
            nps=[x.start() for x in re.finditer(r'`\d+(?:-\d+)?`:', seg)]
            cut=max(starts+nps) if (starts or nps) else 0
            fb=clean(seg[cut:])
            path=(loc or "?").rsplit(':',1)[0]; lines=(loc or "").rsplit(':',1)[-1] if loc and ':' in loc else None
            findings.append(dict(pr=n, kind=kind, severity=sev(seg[cut:]), path=path, line=lines, title=title(fb),
                url=r["html_url"], created=r["submitted_at"], addressed=False, body=fb[:3500]))
json.dump(findings, sys.stdout, ensure_ascii=False, indent=1)
