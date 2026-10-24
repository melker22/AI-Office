#!/usr/bin/env python3
"""Merge gate for AI Office PRs: everything CodeRabbit and CI said about the PR head must be clean.

Born from PR #9: the old gate counted only *inline* review comments and merged while two findings sat
in the review body under "Outside diff range comments" (GitHub cannot post those inline). This gate
reads both places.

  scripts/pr-merge-gate.py <pr-number> [--since ISO8601]
  scripts/pr-merge-gate.py --self-test

Exit 0 = clean (safe to merge). Exit 1 = something to triage (printed). Exit 2 = still pending/unknown.

Authentication is optional. If GITHUB_TOKEN or GH_TOKEN is set, requests send
Authorization: Bearer <token>. With neither variable the calls stay anonymous. An HTTP 403 or 429
that is a GitHub rate limit prints a message and exits 2 (pending). It does not print a traceback.

Checks, all against the PR's current head commit:
  1. Commit status: ci/circleci and CodeRabbit both "success".
  2. Inline CodeRabbit comments created since --since (default: head commit date): must be 0.
     --since and each created_at are instants in UTC (a trailing Z or an offset such as +02:00).
     Comment and review lists are paged with per_page=100&page=N until a page returns fewer than 100.
  3. Every CodeRabbit *review* on the head commit: "Actionable comments posted: N", "Outside diff range
     comments (N)" and "Duplicate comments (N)" must all be 0. Nitpicks are reported, not blocking.
  4. mergeable_state == "clean". mergeable null, or mergeable_state "unknown", is pending (exit 2),
     not a failure. While a commit status is still pending, "unstable" or "blocked" is also pending.
     Once every status is success, those two states are problems again.
  5. Before exit 0, the PR is read again. A changed head is pending (exit 2). Mergeability comes
     from that second read.
Unresolved review threads need GraphQL (auth); the orchestrator checks them with the GitHub MCP tool
(pull_request_read get_review_comments) before merging — this script prints a reminder.

--self-test runs the decision logic on injected responses and does not use the network.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone

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


def parse_utc(value: str) -> datetime:
    """Parse a GitHub timestamp. Accepts a trailing Z and numeric offsets."""
    text = value.strip()
    if text.endswith("Z"):
        text = text[:-1] + "+00:00"
    dt = datetime.fromisoformat(text)
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return dt.astimezone(timezone.utc)


def created_on_or_after(created_at: str, since: str) -> bool:
    return parse_utc(created_at) >= parse_utc(since)


def classify_mergeability(pr: dict, status_pending: bool) -> tuple[list[str], bool, list[str]]:
    """Problems, whether this read is pending, and lines to print.

    unstable/blocked is pending only while a commit status is still pending.
    With every status at success, both states are problems.
    """
    problems: list[str] = []
    pending = False
    lines: list[str] = []
    state = pr.get("mergeable_state")
    if pr.get("merged"):
        lines.append("note: PR already merged (mergeability not applicable)")
    elif pr.get("mergeable") is None or state == "unknown":
        pending = True
        lines.append(
            "PENDING mergeability not computed yet "
            f"(mergeable={pr.get('mergeable')}, mergeable_state={state})"
        )
    elif status_pending and state in ("unstable", "blocked"):
        pending = True
        lines.append(
            f"PENDING mergeable_state = {state} while a status is still pending "
            f"(mergeable={pr.get('mergeable')})"
        )
    elif state != "clean":
        problems.append(f"mergeable_state = {state} (mergeable={pr.get('mergeable')})")
    return problems, pending, lines


def evaluate(pr_number: int, pr: dict, statuses: list, comments: list, reviews: list,
             commit_date: str, since: str | None, refetch) -> tuple[int, list[str]]:
    """Decide the gate from already loaded API payloads. refetch() reads the PR again."""
    head = pr["head"]["sha"]
    problems: list[str] = []
    pending = False
    status_pending = False
    lines: list[str] = []

    st = {s["context"]: s["state"] for s in statuses}
    for ctx in ("ci/circleci: build-and-test", "CodeRabbit"):
        state = st.get(ctx)
        if state in (None, "pending"):
            pending = True
            status_pending = True
            lines.append(f"PENDING {ctx}: {state}")
        elif state != "success":
            problems.append(f"{ctx} = {state}")

    bound = since or commit_date
    try:
        inline = [
            c for c in comments
            if c["user"]["login"].startswith("coderabbit") and c.get("in_reply_to_id") is None
            and created_on_or_after(c["created_at"], bound)
        ]
    except ValueError as exc:
        lines.append(f"PENDING could not parse --since or a comment time ({exc})")
        return 2, lines
    for c in inline:
        problems.append(
            f"inline comment {c['path']}:{c.get('line') or c.get('original_line')} {c['html_url']}"
        )

    on_head = [
        r for r in reviews
        if r["user"]["login"].startswith("coderabbit") and r.get("commit_id") == head
    ]
    if not on_head and not pending:
        pending = True
        lines.append("PENDING no CodeRabbit review on the head commit yet")
    for r in on_head:
        body = r.get("body") or ""
        actionable = count(r"Actionable comments posted:\s*(\d+)", body)
        outside = count(r"Outside diff range comments \((\d+)\)", body)
        duplicate = count(r"Duplicate comments \((\d+)\)", body)
        nitpick = count(r"Nitpick comments \((\d+)\)", body)
        lines.append(
            f"review {r['submitted_at']}: actionable={actionable} outside-diff={outside} "
            f"duplicate={duplicate} nitpick={nitpick}"
        )
        if actionable:
            problems.append(f"review {r['html_url']}: {actionable} actionable comment(s)")
        if outside:
            titles = re.findall(r"<strong>[^<]*</strong>\s*·\s*([^·]+?)\s*·\s*<code>([^<]+)</code>", body)
            detail = "; ".join(f"{t.strip()} ({loc})" for t, loc in titles) or "see review body"
            problems.append(f"review {r['html_url']}: {outside} OUTSIDE-DIFF comment(s): {detail}")
        if duplicate:
            problems.append(f"review {r['html_url']}: {duplicate} duplicate comment(s) (re-raised, still open)")

    merge_problems, merge_pending, merge_lines = classify_mergeability(pr, status_pending)
    problems.extend(merge_problems)
    pending = pending or merge_pending
    lines.extend(merge_lines)

    lines.append("REMINDER unresolved threads: check with GitHub MCP pull_request_read get_review_comments")
    if problems:
        lines.append("NOT CLEAN:")
        for p in problems:
            lines.append(f"  - {p}")
        return 1, lines
    if pending:
        return 2, lines

    fresh = refetch()
    fresh_head = fresh["head"]["sha"]
    if fresh_head != head:
        lines.append(f"PENDING head changed from {head} to {fresh_head}")
        return 2, lines
    again_problems, again_pending, again_lines = classify_mergeability(fresh, status_pending)
    lines.extend(again_lines)
    if again_problems:
        lines.append("NOT CLEAN:")
        for p in again_problems:
            lines.append(f"  - {p}")
        return 1, lines
    if again_pending:
        return 2, lines
    lines.append(f"CLEAN: PR #{pr_number} head {head[:7]}")
    return 0, lines


def _print_result(code: int, lines: list[str]) -> int:
    for line in lines:
        print(line)
    return code


def _main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("pr", nargs="?", type=int)
    ap.add_argument("--since", help="ISO8601; inline comments before this instant are ignored (default: head commit date)")
    ap.add_argument("--self-test", action="store_true", help="run offline checks; no network")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if args.pr is None:
        ap.error("pr is required unless --self-test is set")

    pr = get(f"{API}/pulls/{args.pr}")
    head = pr["head"]["sha"]
    statuses = get(f"{API}/commits/{head}/status").get("statuses", [])
    comments = get_all(f"/pulls/{args.pr}/comments")
    reviews = get_all(f"/pulls/{args.pr}/reviews")
    commit_date = get(f"{API}/commits/{head}")["commit"]["committer"]["date"]

    def refetch():
        return get(f"{API}/pulls/{args.pr}")

    return _print_result(*evaluate(args.pr, pr, statuses, comments, reviews, commit_date, args.since, refetch))


def self_test() -> int:
    failed = 0

    def check(name: str, cond: bool, detail: str = "") -> None:
        nonlocal failed
        if cond:
            print(f"ok   {name}")
        else:
            failed += 1
            print(f"FAIL {name} {detail}")

    # 22:00Z is after 23:00+02:00 (21:00Z). A string compare drops this comment.
    check(
        "offset is later than a Z stamp an hour ahead",
        created_on_or_after("2026-10-21T22:00:00Z", "2026-10-21T23:00:00+02:00"),
    )
    # 23:00+02:00 is 21:00Z, before 22:00Z. A string compare would keep it.
    check(
        "Z stamp is later than an offset that sorts after it",
        not created_on_or_after("2026-10-21T23:00:00+02:00", "2026-10-21T22:00:00Z"),
    )
    check(
        "Z and +00:00 are the same instant",
        parse_utc("2026-10-21T22:00:00Z") == parse_utc("2026-10-21T22:00:00+00:00"),
    )

    sha = "a" * 40
    quiet_review = {
        "user": {"login": "coderabbitai"},
        "commit_id": sha,
        "submitted_at": "2026-10-21T23:00:00Z",
        "html_url": "https://example/review",
        "body": "Actionable comments posted: 0\nOutside diff range comments (0)\nDuplicate comments (0)\n",
    }
    success = [
        {"context": "ci/circleci: build-and-test", "state": "success"},
        {"context": "CodeRabbit", "state": "success"},
    ]

    def pr(head: str = sha, mergeable=True, state: str = "clean", merged: bool = False) -> dict:
        return {"head": {"sha": head}, "mergeable": mergeable, "mergeable_state": state, "merged": merged}

    def run(snapshot, statuses, comments=None, reviews=None, since=None, fresh=None):
        calls = {"n": 0}

        def refetch():
            calls["n"] += 1
            return fresh if fresh is not None else snapshot

        code, lines = evaluate(10, snapshot, statuses, comments or [], reviews if reviews is not None else [quiet_review],
                               "2026-10-21T20:00:00Z", since, refetch)
        return code, "\n".join(lines), calls["n"]

    code, text, n = run(pr(), success)
    check("clean head stays clean after the same reread", code == 0 and "CLEAN" in text and n == 1, text)

    code, text, n = run(pr(), success, fresh=pr(head="b" * 40))
    check("changed head is pending", code == 2 and "PENDING head changed" in text and "CLEAN" not in text, text)

    code, text, n = run(pr(), success, fresh=pr(mergeable=None, state="unknown"))
    check("reread mergeability unknown is pending", code == 2 and "not computed" in text and n == 1, text)

    code, text, n = run(pr(), success, fresh=pr(state="unstable"))
    check("reread unstable with success statuses is a problem", code == 1 and "mergeable_state = unstable" in text, text)

    waiting = [
        {"context": "ci/circleci: build-and-test", "state": "success"},
        {"context": "CodeRabbit", "state": "pending"},
    ]
    code, text, n = run(pr(state="unstable"), waiting)
    check("unstable while a status is pending is not a problem", code == 2 and "NOT CLEAN" not in text and n == 0, text)
    code, text, n = run(pr(state="blocked"), waiting)
    check("blocked while a status is pending is not a problem", code == 2 and "NOT CLEAN" not in text and n == 0, text)
    code, text, n = run(pr(state="unstable"), success)
    check("unstable with every status success is a problem", code == 1 and "NOT CLEAN" in text and n == 0, text)
    code, text, n = run(pr(state="blocked"), success)
    check("blocked with every status success is a problem", code == 1 and "NOT CLEAN" in text and n == 0, text)
    code, text, n = run(pr(state="dirty"), waiting)
    check("dirty stays a problem while a status is pending", code == 1 and "mergeable_state = dirty" in text, text)

    late = {
        "user": {"login": "coderabbitai"},
        "in_reply_to_id": None,
        "created_at": "2026-10-21T22:00:00Z",
        "path": "src/core/Session.cpp",
        "line": 10,
        "html_url": "https://example/comment",
    }
    code, text, _n = run(pr(), success, comments=[late], since="2026-10-21T23:00:00+02:00")
    check("comment after --since by instant is reported", code == 1 and "inline comment" in text, text)
    code, text, _n = run(pr(), success, comments=[late], since="2026-10-21T22:30:00Z")
    check("comment before --since by instant is ignored", code == 0 and "inline comment" not in text, text)

    # End to end through _main with a fake GitHub (the PR #10 TypeError was only in this path).
    global get, get_all
    real_get, real_get_all, real_argv = get, get_all, sys.argv

    def fake_get(url: str):
        if url.endswith("/status"):
            return {"statuses": success}
        if "/commits/" in url:
            return {"commit": {"committer": {"date": "2026-10-21T20:00:00Z"}}}
        return pr()

    def fake_get_all(path: str):
        return [quiet_review] if path.endswith("/reviews") else []

    get, get_all, sys.argv = fake_get, fake_get_all, ["pr-merge-gate.py", "10"]
    try:
        import contextlib
        import io
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            code = _main()
        check("_main end to end with a clean fake PR", code == 0 and "CLEAN" in buf.getvalue(), buf.getvalue())
    except Exception as exc:  # noqa: BLE001 - a crash here is exactly what this case catches
        check("_main end to end with a clean fake PR", False, repr(exc))
    finally:
        get, get_all, sys.argv = real_get, real_get_all, real_argv

    if failed:
        print(f"{failed} self-test(s) failed")
        return 1
    print("ok   pr-merge-gate self-test")
    return 0


def main() -> int:
    try:
        return _main()
    except RateLimited as exc:
        print(f"PENDING GitHub API rate limit ({exc.detail}).")
        print("Set GITHUB_TOKEN or GH_TOKEN and re-run. Without a token the call stays anonymous.")
        return 2


if __name__ == "__main__":
    sys.exit(main())
