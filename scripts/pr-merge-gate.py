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
  3. Every CodeRabbit *review* on the head commit: "Actionable comments posted: N" must be 0. Every
     "Outside diff range" and "Duplicate" finding in the body of ANY CodeRabbit review of the PR (any
     commit: an incremental review of a later push does not repeat them) needs a human PR comment line
     `cr-ack <id> <fix commit or reason>`; the gate prints each unacknowledged id. Nitpicks are not blocking.
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
from pathlib import Path
import contextlib
import io
import http.client
import json
import os
import re
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone

API = "https://api.github.com/repos/melker22/AI-Office"


class NetworkUnavailable(Exception):
    """GitHub could not be reached (timeout, DNS, TLS, reset) or answered 5xx: the gate is pending."""


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
        try:
            body = exc.read().decode("utf-8", errors="replace")
        except (OSError, http.client.HTTPException) as read_exc:  # incl. ssl.SSLError, timeouts, resets
            raise NetworkUnavailable(f"HTTP {exc.code}, body unreadable: {read_exc!r}") from read_exc
        message = body
        try:
            parsed = json.loads(body)
            if isinstance(parsed, dict) and parsed.get("message"):
                message = str(parsed["message"])
        except json.JSONDecodeError:
            pass
        if _is_rate_limit(exc.code, exc.headers, message):
            raise RateLimited(f"HTTP {exc.code}: {message}") from exc
        if exc.code >= 500:
            raise NetworkUnavailable(f"HTTP {exc.code}: {message}") from exc
        raise
    except (OSError, http.client.HTTPException) as exc:
        # HTTPError (a URLError subclass) is handled above; this is the transport failing: URLError, TLS
        # (ssl.SSLError, an OSError raised while reading the body), timeouts and resets.
        raise NetworkUnavailable(repr(exc)) from exc


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


_SECTION_RE = re.compile(
    r"(Outside diff range comments|Duplicate comments|Nitpick comments|Additional comments"
    r"|Critical comments|Major comments|Minor comments|Trivial comments)\s*(?:\(\d+\)|:\s*\d+)")
# "<Severity> comments: N" appears when a review has too many findings to post inline (PR #11).
_SECTION_KIND = {"Outside diff range comments": "outside-diff", "Duplicate comments": "duplicate",
                 "Nitpick comments": "nitpick", "Additional comments": "additional",
                 "Critical comments": "body-critical", "Major comments": "body-major",
                 "Minor comments": "body-minor", "Trivial comments": "body-trivial"}
_BLOCKING_BODY_KINDS = ("outside-diff", "duplicate", "body-critical", "body-major", "body-minor")
# A bare `cr-ack <id>` does not count: the line must say which commit fixed it or why it is not a defect.
# The id must be the FULL cr-comment marker id (a prefix could match two findings).
_ACK_RE = re.compile(r"^\s*cr-ack\s+([0-9a-f]{16,})\b[ \t]+(\S.*)$", re.M)


# The prompt label starts the heading content (only an icon or 🤖 before it), so a finding whose own
# <summary> title mentions the phrase is not taken for it.
_PROMPT_HEADING_RE = re.compile(
    r"^[ \t]*(?:<details>[ \t]*)?<summary>[ \t]*(?:<picture>(?:(?!</picture>).)*</picture>[ \t]*)?(?:🤖[ \t]*)?"
    r"Prompt for all review comments[^<\n]*</summary>", re.M)


def _is_heading(body: str, m: re.Match) -> bool:
    """A section name only counts as a heading in heading position: `**…name (N)**` or
    `<summary>…name: N</summary>` starting its line. The same words quoted inside a finding's text
    must not switch the active section (an outside-diff finding would become a nitpick)."""
    line_start = body.rfind("\n", 0, m.start()) + 1
    prefix = body[line_start:m.start()]
    after = body[m.end():m.end() + 10]
    bold = prefix.lstrip().startswith("**") and "**" not in prefix.lstrip()[2:] and after.startswith("**")
    summary = prefix.lstrip().startswith("<summary>") and after.startswith("</summary>")
    return bold or summary


def body_findings(review: dict, kinds: tuple = _BLOCKING_BODY_KINDS) -> list[dict]:
    """Outside-diff and duplicate findings in one CodeRabbit review body (old <em> and new <strong>
    formats). Each ends with a <!-- cr-comment:v1:HEX --> marker, which is its id."""
    body = review.get("body") or ""
    for _ in range(3):  # nested "> " quote prefixes, the first line included
        body = re.sub(r"^[ \t]*>[ \t]?", "", body, flags=re.M)
    # The prompt section starts at its <summary> heading line; the same words quoted inside a finding
    # must not end the scan early (every later finding would be dropped).
    # ... and it comes after every finding, so the LAST such heading is the boundary.
    heads_prompt = list(_PROMPT_HEADING_RE.finditer(body))
    stop = heads_prompt[-1].start() if heads_prompt else len(body)
    heads = [(m.start(), _SECTION_KIND[m.group(1)]) for m in _SECTION_RE.finditer(body) if _is_heading(body, m)]
    out: list[dict] = []
    prev = 0
    for m in re.finditer(r"<!--\s*cr-comment:v1:([0-9a-f]+)\s*-->", body):
        seg, prev = body[prev:m.start()], m.end()
        if m.start() > stop:
            break
        section_kinds = [k for pos, k in heads if pos <= m.start()]
        if not section_kinds or section_kinds[-1] not in kinds:
            continue
        # the severity tag may wrap an icon: <strong><picture>…</picture> Major</strong> · title · <code>loc</code>
        t = re.findall(r"<(strong|em)>(?:(?!</\1>).)*</\1>\s*·\s*(.+?)\s*·\s*<code>([^<]+)</code>", seg, re.S)
        t = [(title_, loc_) for _tag, title_, loc_ in t]
        full = re.findall(r"`([\w./-]+/[\w.-]+:\d[\d-]*)`", seg)  # repo-relative path, when given
        if t:
            title, loc = re.sub(r"<[^>]+>", "", t[-1][0]).strip(), (full[-1] if full else t[-1][1])
        else:  # grouped by file: <summary>path: N</summary> ... `lines`: **category** | ... **Title**
            files = re.findall(r"<summary>([^<:()]+?)(?::\s*\d+|\s*\(\d+\))</summary>", body[:m.start()])
            ranges = re.findall(r"`(\d+(?:-\d+)?)`:", seg)
            bolds = [b for b in re.findall(r"^\*\*(.{8,240}?)\*\*[ \t]*$", seg, re.M)
                     if not re.search(r"Critical|Major|Minor|Trivial|Quick win|Heavy lift|<picture>", b)]
            title = bolds[-1] if bolds else "?"
            loc = (files[-1].strip() if files else "?") + (":" + ranges[-1] if ranges else "")
        out.append({"id": m.group(1), "kind": section_kinds[-1], "title": title, "loc": loc, "text": seg,
                    "commit": (review.get("commit_id") or "")[:7], "url": review.get("html_url", "")})
    return out


def acknowledgements(issue_comments: list) -> dict[str, str]:
    """`cr-ack <id> <commit or reason>` lines in PR conversation comments written by a human."""
    acks: dict[str, str] = {}
    for c in issue_comments or []:
        user = c.get("user") or {}
        if user.get("type") == "Bot" or user.get("login", "").startswith("coderabbit"):
            continue
        for m in _ACK_RE.finditer(c.get("body") or ""):
            acks[m.group(1)] = m.group(2).strip()
    return acks


def evaluate(pr_number: int, pr: dict, statuses: list, comments: list, reviews: list,
             commit_date: str, since: str | None, refetch,
             issue_comments: list | None = None, head_pr_count: int = 1) -> tuple[int, list[str]]:
    """Decide the gate from already loaded API payloads. refetch() reads the PR again."""
    head = pr["head"]["sha"]
    problems: list[str] = []
    pending = False
    status_pending = False
    lines: list[str] = []

    st = {s["context"]: s["state"] for s in statuses}
    # CodeRabbit sometimes reviews a commit without setting a commit status. Its review object on the
    # head, or its own "Review finished" / "Already reviewed the last commit" reply to a review request
    # made after the head was pushed (issue comments belong to THIS PR), proves the review happened.
    head_reviewed = any(r["user"]["login"].startswith("coderabbit") and r.get("commit_id") == head for r in reviews)
    cr_finished = False
    try:
        cr_finished = any(
            (c.get("user") or {}).get("login", "").startswith("coderabbit")
            and ("Review finished" in (c.get("body") or "") or "Already reviewed the last commit" in (c.get("body") or ""))
            and created_on_or_after(c.get("updated_at") or c.get("created_at") or "", commit_date)
            for c in issue_comments or [])
    except ValueError:
        cr_finished = False
    for ctx in ("ci/circleci: build-and-test", "CodeRabbit"):
        state = st.get(ctx)
        if ctx == "CodeRabbit" and state is None and (head_reviewed or cr_finished):
            lines.append("note: CodeRabbit set no commit status, but it reviewed the head commit")
            continue
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
    cr_desc = next((x.get("description") or "" for x in statuses if x.get("context") == "CodeRabbit"), "")
    if not on_head and not pending:
        # With nothing to say, CodeRabbit finishes an incremental review without posting a review object:
        # its status is success / "Review completed". "Review paused" (or anything else) is still pending.
        # The commit status is per commit, not per PR: if another open PR has the same head, the
        # "Review completed" may belong to that PR, so it proves nothing about this one.
        if st.get("CodeRabbit") == "success" and cr_desc.startswith("Review completed") and head_pr_count > 1:
            pending = True
            lines.append(f"PENDING head commit is shared by {head_pr_count} open PRs; 'Review completed' "
                         "cannot be attributed to this one (push a commit or wait for a review object)")
        elif st.get("CodeRabbit") == "success" and cr_desc.startswith("Review completed"):
            lines.append("note: CodeRabbit completed the head review with no new review (nothing to report)")
        elif cr_finished:
            lines.append("note: CodeRabbit finished reviewing the head with nothing to post (its reply on this PR)")
        else:
            pending = True
            lines.append(f"PENDING no CodeRabbit review on the head commit yet (status: {cr_desc or 'none'})")
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
        # outside-diff / duplicate: each must be acknowledged (below), on this or any earlier commit.

    # Outside-diff and duplicate findings live only in review bodies, and CodeRabbit does not repeat
    # them in the incremental review of a later push. Every one, from a review on ANY commit of the
    # PR, needs a human `cr-ack <id> <commit or reason>` comment (PR #10 merged with two unread).
    acks = acknowledgements(issue_comments)
    for r in reviews:
        if not r["user"]["login"].startswith("coderabbit"):
            continue
        for f in body_findings(r):
            if f["id"] in acks:
                continue
            problems.append(
                f"UNACKED {f['kind']} {f['id']} \"{f['title']}\" ({f['loc']}) from the review of "
                f"{f['commit']}: fix it, then comment `cr-ack {f['id']} <commit or reason>` {f['url']}"
            )

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
    issue_comments = get_all(f"/issues/{args.pr}/comments")
    # Only PRs whose own head IS this commit share the status; a stacked PR that merely contains the
    # commit has a different head.
    head_pr_count = sum(1 for p in get_all(f"/commits/{head}/pulls")
                        if p.get("state") == "open" and (p.get("head") or {}).get("sha") == head) or 1
    commit_date = get(f"{API}/commits/{head}")["commit"]["committer"]["date"]

    def refetch():
        return get(f"{API}/pulls/{args.pr}")

    return _print_result(*evaluate(args.pr, pr, statuses, comments, reviews, commit_date, args.since, refetch, issue_comments, head_pr_count))


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

    completed = [{"context": "ci/circleci: build-and-test", "state": "success"},
                 {"context": "CodeRabbit", "state": "success", "description": "Review completed"}]
    paused = [{"context": "ci/circleci: build-and-test", "state": "success"},
              {"context": "CodeRabbit", "state": "success", "description": "Review paused"}]
    code, text, n = run(pr(), completed, reviews=[])
    check("'Review completed' with no review on the head is clean", code == 0 and "CLEAN" in text, text)
    res = evaluate(10, pr(), completed, [], [], "2026-10-21T20:00:00Z", None, lambda: pr(), [], 2)
    check("'Review completed' on a head shared by two open PRs is pending", res[0] == 2, "\n".join(res[1]))
    ci_only = [{"context": "ci/circleci: build-and-test", "state": "success"}]
    code, text, n = run(pr(), ci_only)                     # quiet_review is on the head, no status
    check("head review without a CodeRabbit status is clean", code == 0 and "CLEAN" in text, text)
    finished = [{"user": {"login": "coderabbitai[bot]", "type": "Bot"}, "body": "✅ Action performed\n\nReview finished.",
                 "created_at": "2026-10-21T21:00:00Z", "updated_at": "2026-10-21T21:05:00Z"}]
    res = evaluate(10, pr(), ci_only, [], [], "2026-10-21T20:00:00Z", None, lambda: pr(), finished)
    check("'Review finished' reply after the push, no status, no review object: clean", res[0] == 0, "\n".join(res[1]))
    stale = [dict(finished[0], updated_at="2026-10-21T19:00:00Z", created_at="2026-10-21T19:00:00Z")]
    res = evaluate(10, pr(), ci_only, [], [], "2026-10-21T20:00:00Z", None, lambda: pr(), stale)
    check("a 'Review finished' reply from before the push does not count", res[0] == 2, "\n".join(res[1]))
    code, text, n = run(pr(), paused, reviews=[])
    check("'Review paused' with no review on the head is pending", code == 2 and "Review paused" in text, text)
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
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            code = _main()
        check("_main end to end with a clean fake PR", code == 0 and "CLEAN" in buf.getvalue(), buf.getvalue())
    except Exception as exc:  # noqa: BLE001 - a crash here is exactly what this case catches
        check("_main end to end with a clean fake PR", False, repr(exc))
    finally:
        get, get_all, sys.argv = real_get, real_get_all, real_argv

    # Body findings from a review on an EARLIER commit (the PR #10 hole), both body formats.
    old_sha = "c" * 40
    new_fmt = {
        "user": {"login": "coderabbitai"}, "commit_id": old_sha, "submitted_at": "2026-10-21T22:00:00Z",
        "html_url": "https://example/old-review",
        "body": ("**Actionable comments posted: 0**\n> [!CAUTION]\n> **⚠️ Outside diff range comments (1)**\n> \n"
                 "> <details>\n> <summary><strong>🟠 Major</strong> · Keep exports read-only. · "
                 "<code>Session.cpp:10-12</code></summary><blockquote>\n> body\n> "
                 "<!-- cr-comment:v1:0123456789abcdef01234567 -->\n> </blockquote></details>\n"
                 "<details><summary>🤖 Prompt for all review comments with AI agents</summary>\n"
                 "<!-- cr-comment:v1:ffffffffffffffffffffffff --></details>"),
    }
    old_fmt = dict(new_fmt, body=("> **⚠️ Outside diff range comments (1)**\n> \n> <details>\n> <summary><em>🟠 Major</em>"
                                  " · Preserve fieldArg. · <code>src/core/Odf.cpp:70-72</code></summary>"
                                  "<blockquote>\n> x\n> <!-- cr-comment:v1:abcdefabcdefabcdefabcdef -->\n"
                                  "> </blockquote></details>\n<details>\n<summary>🔇 Additional comments (2)</summary><blockquote>\n"
                                  "<!-- cr-comment:v1:999999999999999999999999 -->"))
    grouped = dict(new_fmt, body=("**Actionable comments posted: 1**\n<details>\n<summary><picture></picture> Minor comments: 1"
                                  "</summary><blockquote>\n<details>\n<summary>scripts/qa-all.sh: 1</summary><blockquote>\n"
                                  "`24-25`: **🎯 Functional Correctness** | **<picture></picture> Minor** | **⚡ Quick win**\n\n"
                                  "**Read the `FLEET_NEEDS_*_OUTPUT` variables by name.**\n\nbody\n<!-- cr-comment:v1:111111111111111111111111 -->\n"))
    check("grouped 'Minor comments: N' body finding",
          [(f["id"][:6], f["kind"], f["title"], f["loc"]) for f in body_findings(grouped)]
          == [("111111", "body-minor", "Read the `FLEET_NEEDS_*_OUTPUT` variables by name.", "scripts/qa-all.sh:24-25")],
          str(body_findings(grouped)))
    check("new body format: one outside-diff finding", [f["id"] for f in body_findings(new_fmt)]
          == ["0123456789abcdef01234567"], str(body_findings(new_fmt)))
    check("old body format: one outside-diff finding, additional ignored",
          [(f["id"], f["title"]) for f in body_findings(old_fmt)] == [("abcdefabcdefabcdefabcdef", "Preserve fieldArg.")],
          str(body_findings(old_fmt)))
    code, text, _n = run(pr(), success, reviews=[new_fmt, quiet_review])
    check("unacked outside-diff from an earlier commit blocks", code == 1 and "UNACKED outside-diff 0123456789abcdef01234567" in text, text)
    prefix_ack = [{"user": {"login": "melker22", "type": "User"}, "body": "cr-ack 0123456789abcdef fixed in deadbeef"}]
    res = evaluate(10, pr(), success, [], [new_fmt, quiet_review], "2026-10-21T20:00:00Z", None, lambda: pr(), prefix_ack)
    check("a prefix of the id does not acknowledge", res[0] == 1, "\n".join(res[1]))
    icon_fmt = dict(new_fmt, body=new_fmt["body"].replace("<strong>🟠 Major</strong>",
                    '<strong><picture><img src="x.svg" alt=""></picture> Major</strong>'))
    check("severity tag with an icon still yields title and location",
          [(f["title"], f["loc"]) for f in body_findings(icon_fmt)] == [("Keep exports read-only.", "Session.cpp:10-12")],
          str(body_findings(icon_fmt)))
    human_ack = [{"user": {"login": "melker22", "type": "User"}, "body": "cr-ack 0123456789abcdef01234567 fixed in deadbeef"}]
    res = evaluate(10, pr(), success, [], [new_fmt, quiet_review], "2026-10-21T20:00:00Z", None, lambda: pr(), human_ack)
    check("acked outside-diff passes", res[0] == 0, "\n".join(res[1]))
    bare_ack = [{"user": {"login": "melker22", "type": "User"}, "body": "cr-ack 0123456789abcdef01234567"}]
    res = evaluate(10, pr(), success, [], [new_fmt, quiet_review], "2026-10-21T20:00:00Z", None, lambda: pr(), bare_ack)
    check("a bare cr-ack without commit or reason does not count", res[0] == 1, "\n".join(res[1]))
    bot_ack = [{"user": {"login": "coderabbitai[bot]", "type": "Bot"}, "body": "cr-ack 0123456789abcdef01234567 not a defect"}]
    res = evaluate(10, pr(), success, [], [new_fmt, quiet_review], "2026-10-21T20:00:00Z", None, lambda: pr(), bot_ack)
    check("a bot cannot acknowledge", res[0] == 1, "\n".join(res[1]))

    quoted = dict(new_fmt, body=new_fmt["body"].replace("> body\n", "> It cites Nitpick comments (1) and Minor comments: 3 in its text.\n"))
    phrase = dict(new_fmt, body=new_fmt["body"].replace("> body\n", "> See the Prompt for all review comments section.\n"))
    check("the prompt-section phrase quoted inside a finding does not end the scan",
          [f["id"][:6] for f in body_findings(phrase)] == ["012345"], str(body_findings(phrase)))
    in_title = dict(new_fmt, body=new_fmt["body"].replace(
        "· Keep exports read-only. ·", "· Prompt for all review comments must stay last. ·"))
    check("the prompt-section phrase in a finding title does not end the scan",
          [(f["id"][:6], f["title"]) for f in body_findings(in_title)]
          == [("012345", "Prompt for all review comments must stay last.")], str(body_findings(in_title)))
    check("section names quoted inside a finding do not change its section",
          [(f["id"][:6], f["kind"]) for f in body_findings(quoted)] == [("012345", "outside-diff")], str(body_findings(quoted)))

    # Real CodeRabbit review bodies (old <em>, new <strong> with icons, grouped "Minor comments: N"):
    # the number of blocking body findings per PR must not change when the parser does.
    fixtures = Path(__file__).resolve().parent.parent / "qa" / "fixtures" / "coderabbit"
    expected_counts = {1: 1, 2: 3, 4: 1, 6: 1, 9: 3, 10: 3, 11: 28}
    for n, want in expected_counts.items():
        path = fixtures / f"pr{n}-reviews.json"
        if not path.is_file():
            check(f"real review bodies of PR #{n}", False, f"missing {path}")
            continue
        got = [f for r in json.loads(path.read_text(encoding="utf-8")) for f in body_findings(r)]
        check(f"real review bodies of PR #{n}: {want} blocking body finding(s)",
              len(got) == want and all(f["loc"] != "?" and f["title"] != "?" for f in got),
              str([(f["kind"], f["title"][:40], f["loc"]) for f in got]))

    import ssl
    real_urlopen2, real_argv2 = urllib.request.urlopen, sys.argv

    def tls_urlopen(*_a, **_k):
        raise ssl.SSLError("The handshake operation timed out")

    urllib.request.urlopen, sys.argv = tls_urlopen, ["pr-merge-gate.py", "10"]
    try:
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            code = main()
        check("a TLS failure is pending", code == 2 and "unreachable" in buf.getvalue(), buf.getvalue())
    except Exception as exc:  # noqa: BLE001
        check("a TLS failure is pending", False, repr(exc))
    finally:
        urllib.request.urlopen, sys.argv = real_urlopen2, real_argv2

    # A network failure is pending (exit 2), never a traceback that reads as "something to triage".
    real_urlopen, real_argv = urllib.request.urlopen, sys.argv

    def timeout_urlopen(*_a, **_k):
        raise TimeoutError("The read operation timed out")

    urllib.request.urlopen, sys.argv = timeout_urlopen, ["pr-merge-gate.py", "10"]
    try:
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            code = main()
        check("network timeout is pending", code == 2 and "unreachable" in buf.getvalue(), buf.getvalue())
    except Exception as exc:  # noqa: BLE001
        check("network timeout is pending", False, repr(exc))
    finally:
        urllib.request.urlopen, sys.argv = real_urlopen, real_argv

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
    except NetworkUnavailable as exc:
        print(f"PENDING GitHub API unreachable ({exc}). Re-run when the network is back.")
        return 2


if __name__ == "__main__":
    sys.exit(main())
