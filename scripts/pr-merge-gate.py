#!/usr/bin/env python3
"""Merge gate for AI Office PRs: everything CodeRabbit and CI said about the PR head must be clean.

Born from PR #9: the old gate counted only *inline* review comments and merged while two findings sat
in the review body under "Outside diff range comments" (GitHub cannot post those inline). This gate
reads both places.

  scripts/pr-merge-gate.py <pr-number> [--since ISO8601]

Exit 0 = clean (safe to merge). Exit 1 = something to triage (printed). Exit 2 = still pending/unknown.

Authentication is optional. If GITHUB_TOKEN or GH_TOKEN is set, requests send
Authorization: Bearer <token>. With neither variable the calls stay anonymous. An HTTP 403 or 429
that is a GitHub rate limit prints a message and exits 2 (pending). It does not print a traceback.

Checks, all against the PR's current head commit:
  1. Commit status: ci/circleci and CodeRabbit both "success".
  2. Inline CodeRabbit comments created since --since (default: head commit date): must be 0.
     Comment and review lists are paged with per_page=100&page=N until a page returns fewer than 100.
  3. Every CodeRabbit *review* on the head commit: "Actionable comments posted: N", "Outside diff range
     comments (N)" and "Duplicate comments (N)" must all be 0. Nitpicks are reported, not blocking.
  4. mergeable_state == "clean". mergeable null, or mergeable_state "unknown", is pending (exit 2),
     not a failure.
Unresolved review threads need GraphQL (auth); the orchestrator checks them with the GitHub MCP tool
(pull_request_read get_review_comments) before merging — this script prints a reminder.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request

API = "https://api.github.com/repos/melker22/AI-Office"


class RateLimited(Exception):
    def __init__(self, detail: str) -> None:
        super().__init__(detail)
        self.detail = detail


def _token() -> str:
    return os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN") or ""


def _is_rate_limit(code: int, headers, message: str) -> bool:
    if code not in (403, 429):
        return False
    if code == 429:
        return True
    if headers.get("X-RateLimit-Remaining") == "0":
        return True
    low = message.lower()
    return "rate limit" in low


def get(url: str):
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "aioffice-pr-merge-gate",
    }
    token = _token()
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return json.load(resp)
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        message = body
        try:
            parsed = json.loads(body)
            if isinstance(parsed, dict) and parsed.get("message"):
                message = str(parsed["message"])
        except json.JSONDecodeError:
            pass
        if _is_rate_limit(exc.code, exc.headers, message):
            raise RateLimited(f"HTTP {exc.code}: {message}") from exc
        raise


def get_all(path: str) -> list:
    """Page a list endpoint until a page comes back with fewer than 100 items."""
    page = 1
    items: list = []
    while True:
        batch = get(f"{API}{path}?per_page=100&page={page}")
        if not isinstance(batch, list):
            print(f"PENDING GitHub list {path} page {page} was not an array")
            raise SystemExit(2)
        items.extend(batch)
        if len(batch) < 100:
            return items
        page += 1


def count(pattern: str, text: str) -> int:
    m = re.search(pattern, text)
    return int(m.group(1)) if m else 0


def main() -> int:
    try:
        return _main()
    except RateLimited as exc:
        print(f"PENDING GitHub API rate limit ({exc.detail}).")
        print("Set GITHUB_TOKEN or GH_TOKEN and re-run. Without a token the call stays anonymous.")
        return 2


def _main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("pr", type=int)
    ap.add_argument("--since", help="ISO8601; inline comments before this are ignored (default: head commit date)")
    args = ap.parse_args()

    pr = get(f"{API}/pulls/{args.pr}")
    head = pr["head"]["sha"]
    problems: list[str] = []
    pending = False

    # 1. statuses
    st = {s["context"]: s["state"] for s in get(f"{API}/commits/{head}/status").get("statuses", [])}
    for ctx in ("ci/circleci: build-and-test", "CodeRabbit"):
        state = st.get(ctx)
        if state in (None, "pending"):
            pending = True
            print(f"PENDING {ctx}: {state}")
        elif state != "success":
            problems.append(f"{ctx} = {state}")

    # 2. inline comments since
    since = args.since or get(f"{API}/commits/{head}")["commit"]["committer"]["date"]
    inline = [
        c for c in get_all(f"/pulls/{args.pr}/comments")
        if c["user"]["login"].startswith("coderabbit") and c.get("in_reply_to_id") is None and c["created_at"] >= since
    ]
    for c in inline:
        problems.append(f"inline comment {c['path']}:{c.get('line') or c.get('original_line')} {c['html_url']}")

    # 3. review bodies on the head commit
    reviews = [r for r in get_all(f"/pulls/{args.pr}/reviews")
               if r["user"]["login"].startswith("coderabbit") and r.get("commit_id") == head]
    if not reviews and not pending:
        pending = True
        print("PENDING no CodeRabbit review on the head commit yet")
    for r in reviews:
        body = r.get("body") or ""
        actionable = count(r"Actionable comments posted:\s*(\d+)", body)
        outside = count(r"Outside diff range comments \((\d+)\)", body)
        duplicate = count(r"Duplicate comments \((\d+)\)", body)
        nitpick = count(r"Nitpick comments \((\d+)\)", body)
        print(f"review {r['submitted_at']}: actionable={actionable} outside-diff={outside} duplicate={duplicate} nitpick={nitpick}")
        if actionable:
            problems.append(f"review {r['html_url']}: {actionable} actionable comment(s)")
        if outside:
            titles = re.findall(r"<strong>[^<]*</strong>\s*·\s*([^·]+?)\s*·\s*<code>([^<]+)</code>", body)
            detail = "; ".join(f"{t.strip()} ({loc})" for t, loc in titles) or "see review body"
            problems.append(f"review {r['html_url']}: {outside} OUTSIDE-DIFF comment(s): {detail}")
        if duplicate:
            problems.append(f"review {r['html_url']}: {duplicate} duplicate comment(s) (re-raised, still open)")

    # 4. mergeability. null / unknown means GitHub has not computed it yet.
    if pr.get("merged"):
        print("note: PR already merged (mergeability not applicable)")
    elif pr.get("mergeable") is None or pr.get("mergeable_state") == "unknown":
        pending = True
        print(
            "PENDING mergeability not computed yet "
            f"(mergeable={pr.get('mergeable')}, mergeable_state={pr.get('mergeable_state')})"
        )
    elif pr.get("mergeable_state") != "clean":
        problems.append(f"mergeable_state = {pr.get('mergeable_state')} (mergeable={pr.get('mergeable')})")

    print("REMINDER unresolved threads: check with GitHub MCP pull_request_read get_review_comments")
    if problems:
        print("NOT CLEAN:")
        for p in problems:
            print("  -", p)
        return 1
    if pending:
        return 2
    print(f"CLEAN: PR #{args.pr} head {head[:7]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
