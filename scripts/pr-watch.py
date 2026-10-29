#!/usr/bin/env python3
"""Wait for a PR's CI and CodeRabbit review, then exit with what happened.

The orchestrator runs this in the background after a push or an
`@coderabbitai review` request. The process exits as soon as there is
something to act on, which wakes the agent that started it:

  * CodeRabbit posted a review on the PR head commit       -> cr=reviewed
  * CodeRabbit replied "Review finished" / "Already reviewed"
    after the trigger, with no review on the head commit    -> cr=no_review
  * CodeRabbit replied "rate limited" after the trigger     -> cr=rate_limited
  * CI on the head commit failed                            -> ci=failure

With `--until all`, a CodeRabbit result also waits for CI to finish.
Every state change is printed as one JSON line; the last line is the
final state. `--notify-task <id>` also sends it to a Cockpit task.

Polling is cheap: every request carries the ETag of the previous answer
(If-None-Match), and a 304 does not count against GitHub's rate limit.
Without a token a 304 still counts, so the interval is 540 s (at least seven
requests per poll, 60 requests per hour shared by the whole IP); with a read-only token (GITHUB_TOKEN / GH_TOKEN or
~/.config/aio-pr-watch/token, mode 600) it is 30 s (5000 per hour).

Exit codes: 0 = something to act on (see the last line), 2 = timeout,
3 = bad arguments or token file.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import stat
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

REPO_DEFAULT = "melker22/AI-Office"
BOT = "coderabbitai[bot]"
TOKEN_FILE = Path.home() / ".config" / "aio-pr-watch" / "token"
PER_PAGE = 100


class TokenError(Exception):
    pass


def load_token(path: Path = TOKEN_FILE) -> str:
    env = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if env:
        return env.strip()
    if not path.exists():
        return ""
    try:
        mode = stat.S_IMODE(path.stat().st_mode)
    except OSError as exc:
        raise TokenError(f"cannot read {path}: {exc}") from exc
    if mode & 0o077:
        raise TokenError(f"{path} is readable by others (mode {mode:o}); run chmod 600 {path}")
    try:
        return path.read_text(encoding="utf-8").strip()
    except OSError as exc:
        raise TokenError(f"cannot read {path}: {exc}") from exc


@dataclass
class Response:
    status: int
    body: Any = None
    headers: dict[str, str] = field(default_factory=dict)


class Http:
    """GET with ETag reuse. A 304 returns the cached body of the last 200."""

    def __init__(self, token: str, opener: Callable[[urllib.request.Request], Any] | None = None):
        self.token = token
        self.opener = opener or (lambda req: urllib.request.urlopen(req, timeout=30))
        self.cache: dict[str, tuple[str, Any]] = {}
        self.requests = 0
        self.not_modified = 0
        self.rate_reset = 0.0  # epoch seconds when a rate-limited quota comes back

    def get(self, url: str) -> Response:
        headers = {"Accept": "application/vnd.github+json", "User-Agent": "aioffice-pr-watch"}
        if self.token:
            headers["Authorization"] = f"Bearer {self.token}"
        cached = self.cache.get(url)
        if cached:
            headers["If-None-Match"] = cached[0]
        self.requests += 1
        try:
            resp = self.opener(urllib.request.Request(url, headers=headers))
            body = json.load(resp)
            etag = resp.headers.get("ETag")
            if etag:
                self.cache[url] = (etag, body)
            return Response(200, body, dict(resp.headers))
        except urllib.error.HTTPError as exc:
            raw = exc.headers  # HTTPMessage: case-insensitive lookups
            hget = (lambda k: raw.get(k)) if raw is not None else (lambda k: None)
            hdrs = dict(raw or {})
            if exc.code == 304 and cached:
                self.not_modified += 1
                return Response(304, cached[1], hdrs)
            retry_after = hget("Retry-After")
            secondary = False
            if exc.code == 403 and not retry_after and hget("X-RateLimit-Remaining") != "0":
                try:  # a secondary limit can come with quota left and no timing header
                    secondary = "secondary rate limit" in (exc.read() or b"").decode("utf-8", "replace").lower()
                except (OSError, ValueError):
                    secondary = False
            if exc.code == 429 or (exc.code == 403 and (hget("X-RateLimit-Remaining") == "0" or retry_after
                                                        or secondary)):
                # Wait for the later of Retry-After (secondary limits) and X-RateLimit-Reset (primary);
                # with neither usable, 60 s.
                deadlines = []
                try:
                    if retry_after:
                        deadlines.append(time.time() + float(retry_after))
                except ValueError:
                    pass
                try:
                    reset = float(hget("X-RateLimit-Reset") or 0)
                    if reset:
                        deadlines.append(reset)
                except ValueError:
                    pass
                self.rate_reset = max(deadlines) if deadlines else time.time() + 60
            return Response(exc.code, None, hdrs)
        except (urllib.error.URLError, OSError, ValueError):
            return Response(0, None, {})


def iso(ts: str) -> datetime:
    return datetime.fromisoformat(ts.replace("Z", "+00:00"))


def is_bot(user: Any) -> bool:
    return isinstance(user, dict) and user.get("login") == BOT and user.get("type") == "Bot"


@dataclass
class Snapshot:
    head: str
    ci: str  # pending | success | failure
    cr: str  # pending | reviewed | no_review | rate_limited
    detail: str = ""

    def key(self) -> tuple[str, str, str]:
        return (self.head, self.ci, self.cr)


def ci_state(statuses: list[dict], check_runs: list[dict]) -> str:
    """Combined CI on the head commit. CodeRabbit's own status is not CI."""
    states: list[str] = []
    latest: dict[str, dict] = {}
    for s in statuses:  # newest first
        ctx = s.get("context") or ""
        if ctx == "CodeRabbit" or ctx in latest:
            continue
        latest[ctx] = s
    for s in latest.values():
        st = s.get("state")
        states.append("success" if st == "success" else "pending" if st == "pending" else "failure")
    for run in check_runs:
        name = (run.get("app") or {}).get("slug") or ""
        if "coderabbit" in name:
            continue
        if run.get("status") != "completed":
            states.append("pending")
        elif run.get("conclusion") in ("success", "neutral", "skipped"):
            states.append("success")
        else:
            states.append("failure")
    if not states:
        return "pending"
    if "failure" in states:
        return "failure"
    if "pending" in states:
        return "pending"
    return "success"


_REVIEW_HEADER = re.compile(r"(?m)^\**Actionable comments posted:\s*\d+")
_COVERAGE_RE = re.compile(r"(?m)^<!-- final_review_risk_coverage:(\{[^\n]*?\}) -->$")


def summary_covers(comment: dict, head: str) -> bool:
    """The bot's PR summary marks `head` as reviewed (an incremental review with nothing to post).

    Same rule as scripts/pr-merge-gate.py summary_reviewed: the bot itself, its summary comment
    (`<!-- walkthrough_start -->`), a whole-line marker naming the full head SHA, kind "reviewed".
    """
    if not is_bot(comment.get("user")):
        return False
    body = comment.get("body") or ""
    if "<!-- walkthrough_start -->" not in body:
        return False
    for m in _COVERAGE_RE.finditer(body):
        try:
            data = json.loads(m.group(1))
        except ValueError:
            continue
        if isinstance(data, dict) and data.get("kind") == "reviewed" and data.get("coveredCommitId") == head:
            return True
    return False


# The bot's own whole-line marker in front of its "Rate limit exceeded" notice. Prose that merely mentions
# rate limits (the walkthrough of a PR about rate limits, for one) does not match.
_RATE_NOTICE = re.compile(r"(?m)^<!-- This is an auto-generated comment: rate limited by coderabbit\.ai -->$")
_REPLY_MARK = "<!-- This is an auto-generated reply by CodeRabbit -->"
_REPLY_MARK_RE = re.compile(r"(?m)^<!-- This is an auto-generated reply by CodeRabbit -->$")
_WAIT = re.compile(r"(?i)(?:wait\s+\**\s*([0-9][^*\n]{0,40}?)\s*\**\s+before|available in:?\s*\**\s*([0-9][^*\n.]{0,40}))")


def summary_rate_limited(body: str) -> bool:
    """CodeRabbit's rate-limit notice inside an (edited) summary comment."""
    return bool(_RATE_NOTICE.search(body))


def summary_wait(body: str) -> str:
    m = _WAIT.search(body)
    return f" (wait {(m.group(1) or m.group(2)).strip()})" if m else ""


def cr_state(head: str, since: datetime, reviews: list[dict], comments: list[dict]) -> tuple[str, str]:
    for r in reviews:
        if not (is_bot(r.get("user")) and r.get("commit_id") == head):
            continue
        if not _REVIEW_HEADER.search(r.get("body") or ""):
            continue  # a bot reply inside a review thread (empty body), not a review of the head
        sub = r.get("submitted_at")
        # Drafts have no submitted_at; PENDING is not a finished review. Both used to count.
        if not sub or r.get("state") == "PENDING" or iso(sub) <= since:
            continue
        return "reviewed", f"review {r.get('id')} at {sub}"
    for c in comments:  # the summary is edited in place: its updated_at says when it was refreshed
        ts = c.get("updated_at") or c.get("created_at")
        if ts and iso(ts) > since and summary_covers(c, head):
            return "reviewed", f"summary marks {head[:7]} reviewed ({c.get('html_url')})"
    # The newest bot event after `since` decides: a rate-limit notice that an automatic review writes by
    # editing the summary (its updated_at), or a command reply (its created_at).
    events: list[tuple[datetime, str, str]] = []
    for c in comments:
        if not is_bot(c.get("user")):
            continue
        body = c.get("body") or ""
        edited = c.get("updated_at") or c.get("created_at")
        if edited and iso(edited) > since and summary_rate_limited(body):
            events.append((iso(edited), "rate_limited", f"summary {c.get('html_url')}{summary_wait(body)}"))
        created = c.get("created_at")
        if created and iso(created) > since and _REPLY_MARK_RE.search(body):  # only command replies (whole-line marker)
            outcome = reply_outcome(body)
            if outcome:
                events.append((iso(created), outcome, f"reply {c.get('html_url')}"))
    if events:
        _, outcome, detail = max(events, key=lambda e: e[0])
        return outcome, detail
    return "pending", ""


def reply_outcome(body: str) -> str:
    """Outcome of a CodeRabbit command reply, from its own text only.

    Every reply carries the same boilerplate note ("> Note: ... does not
    re-review already reviewed commits ..."), so HTML comments and quoted
    lines are dropped before matching. "Review triggered." is still pending.
    """
    text = re.sub(r"<!--.*?-->", "", body, flags=re.S)
    lines = [ln for ln in text.splitlines() if not ln.lstrip().startswith(">")]
    low = " ".join(lines).lower()
    if "rate limited" in low:
        return "rate_limited"
    if "review finished" in low or "already reviewed the last commit" in low:
        return "no_review"
    return ""


def _head_sha(resp: Response) -> str | None:
    if resp.status not in (200, 304) or not isinstance(resp.body, dict):
        return None
    head = resp.body.get("head")
    if not isinstance(head, dict):
        return None
    sha = head.get("sha")
    if not isinstance(sha, str):
        return None
    return sha


def _committed_at(resp: Response) -> datetime | None:
    """Committer timestamp, or None when the payload is not an aware ISO time."""
    if resp.status not in (200, 304) or not isinstance(resp.body, dict):
        return None
    commit = resp.body.get("commit")
    if not isinstance(commit, dict):
        return None
    committer = commit.get("committer")
    if not isinstance(committer, dict):
        return None
    date = committer.get("date")
    if not isinstance(date, str):
        return None
    try:
        when = iso(date)
    except ValueError:
        return None
    if when.tzinfo is None:  # cannot be ordered against an aware `since`
        return None
    return when


def _dict_list(resp: Response, key: str) -> list[dict] | None:
    """`key` must be a list of dicts. Missing, null, or any other shape is a failed fetch."""
    if resp.status not in (200, 304) or not isinstance(resp.body, dict) or key not in resp.body:
        return None
    items = resp.body[key]
    if not isinstance(items, list) or any(not isinstance(item, dict) for item in items):
        return None
    return items


class Watcher:
    def __init__(self, http: Http, repo: str, pr: int, since: datetime | None):
        self.http = http
        self.api = f"https://api.github.com/repos/{repo}"
        self.pr = pr
        self.since = since

    def _list(self, path: str) -> list[dict] | None:
        out: list[dict] = []
        page = 1
        while True:
            r = self.http.get(f"{self.api}{path}?per_page={PER_PAGE}&page={page}")
            if r.status not in (200, 304) or not isinstance(r.body, list):
                return None
            out.extend(r.body)
            if len(r.body) < PER_PAGE:
                return out
            page += 1

    def snapshot(self) -> Snapshot | None:
        # The head can move while CI and reviews are collected. Re-read the PR and, when
        # the SHA changed, collect again (at most three times) so the result is not for a stale head.
        for _ in range(3):
            head = _head_sha(self.http.get(f"{self.api}/pulls/{self.pr}"))
            if head is None:
                return None
            committed = _committed_at(self.http.get(f"{self.api}/commits/{head}"))
            if committed is None:
                return None
            since = max(committed, self.since) if self.since else committed
            statuses = _dict_list(self.http.get(
                f"{self.api}/commits/{head}/status?per_page={PER_PAGE}"), "statuses")
            check_runs = _dict_list(self.http.get(
                f"{self.api}/commits/{head}/check-runs?per_page={PER_PAGE}"), "check_runs")
            reviews = self._list(f"/pulls/{self.pr}/reviews")
            comments = self._list(f"/issues/{self.pr}/comments")
            if statuses is None or check_runs is None or reviews is None or comments is None:
                return None
            confirmed = _head_sha(self.http.get(f"{self.api}/pulls/{self.pr}"))
            if confirmed is None:
                return None
            if confirmed != head:
                continue
            ci = ci_state(statuses, check_runs)
            cr, detail = cr_state(head, since, reviews, comments)
            return Snapshot(head, ci, cr, detail)
        return None


def done(s: Snapshot, until: str) -> bool:
    if s.ci == "failure":
        return True
    if s.cr == "pending":
        return False
    return until == "cr" or s.ci != "pending"


def notify(task: str, line: str, timeout: float = 60) -> None:
    cockpit = os.environ.get("AIO_COCKPIT") or str(Path.home() / ".agi-tools" / "bin" / "cockpit")
    try:
        subprocess.run([cockpit, "task", "send", task, "--stdin"], input=f"pr-watch: {line}\n",
                       text=True, check=False, capture_output=True, timeout=timeout)
    except (OSError, subprocess.SubprocessError) as exc:  # CLI missing or hung: the result still stands
        print(f"pr-watch: could not notify task {task}: {exc}", file=sys.stderr)


def run(args: argparse.Namespace, http: Http, sleep: Callable[[float], None] = time.sleep,
        clock: Callable[[], float] = time.monotonic, out=sys.stdout) -> int:
    # Seven requests per poll: PR, commit, status, checks, reviews, comments, then the PR
    # again. Without a token a 304 still counts against the 60/h anonymous quota, so
    # poll every 9 min (about 47 requests per hour); with a token, every 30 s.
    interval = args.interval or (30 if http.token else 540)
    since = iso(args.since) if args.since else None
    watcher = Watcher(http, args.repo, args.pr, since)
    start = clock()
    last: tuple[str, str, str] | None = None
    failing = False
    while True:
        snap = watcher.snapshot()
        if snap is None:
            if not failing:  # one line per outage, not one per poll
                print(json.dumps({"pr": args.pr, "event": "fetch_failed",
                                  "rate_reset": int(http.rate_reset) or None}), file=out, flush=True)
            failing = True
        else:
            failing = False
        if snap is not None and snap.key() != last:
            last = snap.key()
            line = json.dumps({"pr": args.pr, "head": snap.head[:7], "ci": snap.ci, "cr": snap.cr,
                               "detail": snap.detail, "requests": http.requests,
                               "not_modified": http.not_modified})
            print(line, file=out, flush=True)
            if done(snap, args.until):
                if args.notify_task:
                    notify(args.notify_task, line)
                return 0
        if clock() - start >= args.timeout:
            line = json.dumps({"pr": args.pr, "event": "timeout"})
            print(line, file=out, flush=True)
            if args.notify_task:
                notify(args.notify_task, line)
            return 2
        wait = interval
        if http.rate_reset:
            wait = max(wait, http.rate_reset - time.time() + 1)
            http.rate_reset = 0.0
        sleep(min(wait, max(1.0, args.timeout - (clock() - start))))


def parse(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("pr", type=int, nargs="?")
    p.add_argument("--repo", default=REPO_DEFAULT)
    p.add_argument("--since", help="ignore CodeRabbit replies before this ISO time (e.g. the review request)")
    p.add_argument("--until", choices=("cr", "all"), default="cr")
    p.add_argument("--interval", type=float, default=0)
    p.add_argument("--timeout", type=float, default=6600)
    p.add_argument("--notify-task", help="Cockpit task id to message when done")
    p.add_argument("--self-test", action="store_true")
    return p.parse_args(argv)


def main(argv: list[str] | None = None, http: Http | None = None,
         sleep: Callable[[float], None] = time.sleep, clock: Callable[[], float] = time.monotonic,
         out=sys.stdout) -> int:
    args = parse(sys.argv[1:] if argv is None else argv)
    if args.self_test:
        return self_test()
    if args.pr is None:
        print("pr-watch: PR number required", file=sys.stderr)
        return 3
    if args.since:
        try:
            parsed = iso(args.since)
        except ValueError:
            parsed = None
        if parsed is None or parsed.tzinfo is None:
            print(f"pr-watch: --since needs an ISO time with a zone, e.g. 2026-10-29T09:00:00Z (got {args.since!r})",
                  file=sys.stderr)
            return 3
    # 0 is legal: --interval 0 means the default poll, --timeout 0 means one poll then stop.
    for flag in ("interval", "timeout"):
        value = getattr(args, flag)
        if not math.isfinite(value) or value < 0:
            print(f"pr-watch: --{flag} must be a finite number >= 0 (got {value!r})", file=sys.stderr)
            return 3
    if http is None:
        try:
            http = Http(load_token())
        except TokenError as exc:
            print(f"pr-watch: {exc}", file=sys.stderr)
            return 3
    return run(args, http, sleep, clock, out)


# --------------------------------------------------------------------------- self-test

class _FakeResp:
    def __init__(self, body: Any, etag: str):
        self._raw = json.dumps(body).encode()
        self.headers = {"ETag": etag}

    def read(self, *a):
        raw, self._raw = self._raw, b""
        return raw


class _FakeGitHub:
    """Serves a mutable PR world; answers 304 when the ETag still matches."""

    HEAD = "a" * 40

    def __init__(self) -> None:
        self.statuses: list[dict] = []
        self.check_runs: list[dict] = []
        self.reviews: list[dict] = []
        self.comments: list[dict] = []
        self.seen_auth: list[str] = []
        self.version = 0

    def body(self, url: str) -> Any:
        path = url.split("/repos/o/r", 1)[1].split("?", 1)[0]
        if path == "/pulls/7":
            return {"head": {"sha": self.HEAD}}
        if path.startswith("/commits/") and path.count("/") == 2:  # any SHA, not only the current head
            return {"commit": {"committer": {"date": "2026-10-29T10:00:00Z"}}}
        if path.endswith("/status"):
            return {"statuses": self.statuses}
        if path.endswith("/check-runs"):
            return {"check_runs": self.check_runs}
        if path == "/pulls/7/reviews":
            return self.reviews
        if path == "/issues/7/comments":
            return self.comments
        raise AssertionError(url)

    def __call__(self, req: urllib.request.Request):
        self.seen_auth.append(req.get_header("Authorization") or "")
        body = self.body(req.full_url)
        etag = f'"{hash(json.dumps(body, sort_keys=True))}"'
        if req.get_header("If-none-match") == etag:
            raise urllib.error.HTTPError(req.full_url, 304, "Not Modified", {}, None)
        return _FakeResp(body, etag)


BOT_USER = {"login": BOT, "type": "Bot"}


def _lower_headers(reset: int):
    """Rate-limit headers in lower case, as some proxies send them."""
    from email.message import Message
    m = Message()
    m["x-ratelimit-remaining"] = "0"
    m["x-ratelimit-reset"] = str(reset)
    return m


def self_test() -> int:
    import io
    failures: list[str] = []

    def check(name: str, cond: bool, info: Any = "") -> None:
        if not cond:
            failures.append(f"{name}: {info}")

    # State functions.
    since = iso("2026-10-29T10:00:00Z")
    head = _FakeGitHub.HEAD
    check("review on head", cr_state(head, since, [{"user": BOT_USER, "commit_id": head, "state": "COMMENTED",
                                                    "submitted_at": "2026-10-29T10:05:00Z",
                                                    "body": "**Actionable comments posted: 0**"}], [])[0] == "reviewed")
    unsent = {"user": BOT_USER, "commit_id": head, "state": "COMMENTED", "body": "**Actionable comments posted: 0**"}
    check("review without submitted_at", cr_state(head, since, [unsent], [])[0] == "pending")
    check("review with null submitted_at", cr_state(head, since, [dict(unsent, submitted_at=None)], [])[0] == "pending")
    check("pending review is not submitted",
          cr_state(head, since, [dict(unsent, submitted_at="2026-10-29T10:05:00Z", state="PENDING")], [])[0] == "pending")
    check("thread reply on head is not a review", cr_state(head, since, [{"user": BOT_USER, "commit_id": head, "body": ""}], [])[0] == "pending")
    old_rev = {"user": BOT_USER, "commit_id": head, "submitted_at": "2026-10-29T09:30:00Z", "body": "**Actionable comments posted: 0**"}
    check("review before since", cr_state(head, since, [old_rev], [])[0] == "pending")
    new_rev = {"user": BOT_USER, "commit_id": head, "submitted_at": "2026-10-29T10:01:00Z", "body": "**Actionable comments posted: 0**"}
    check("review after since", cr_state(head, since, [old_rev, new_rev], [])[0] == "reviewed")
    check("review on old head", cr_state(head, since, [{"user": BOT_USER, "commit_id": "b" * 40}], [])[0] == "pending")
    lookalike = {"login": BOT, "type": "User"}
    check("look-alike user", cr_state(head, since, [{"user": lookalike, "commit_id": head}], [])[0] == "pending")
    old = {"user": BOT_USER, "created_at": "2026-10-29T09:00:00Z", "body": _REPLY_MARK + "\nReview finished."}
    check("reply before trigger", cr_state(head, since, [], [old])[0] == "pending")
    fin = {"user": BOT_USER, "created_at": "2026-10-29T10:05:00Z", "body": _REPLY_MARK + "\nReview finished."}
    check("finished, no review", cr_state(head, since, [], [fin])[0] == "no_review")
    rl = {"user": BOT_USER, "created_at": "2026-10-29T10:06:00Z", "body": _REPLY_MARK + "\nReview rate limited."}
    check("newest reply wins", cr_state(head, since, [], [fin, rl])[0] == "rate_limited")
    note = ("\n\n> Note: CodeRabbit is an incremental review system and does not re-review already reviewed "
            "commits. This command is applicable only when automatic reviews are paused.\n")
    def reply(text: str) -> str:  # shape of a real CodeRabbit command reply (PR #17)
        return ("<!-- This is an auto-generated reply by CodeRabbit -->\n<details>\n<summary>Action performed"
                f"</summary>\n<dl>\n<dd>\n\n{text}{note}\n</dd>\n</dl>\n</details>")
    check("triggered is pending", reply_outcome(reply("Review triggered.")) == "", reply_outcome(reply("Review triggered.")))
    check("finished", reply_outcome(reply("Review finished.")) == "no_review")
    check("already reviewed", reply_outcome(reply("Already reviewed the last commit. Use `@coderabbitai full review` "
                                                  "to rerun a review of the entire changeset.")) == "no_review")
    check("rate limited", reply_outcome(reply("Review rate limited.")) == "rate_limited")
    trig = {"user": BOT_USER, "created_at": "2026-10-29T10:07:00Z", "body": reply("Review triggered.")}
    check("triggered after finished stays pending", cr_state(head, since, [], [trig])[0] == "pending")
    summary_body = ("<!-- walkthrough_start -->\n<!-- final_review_risk_coverage:{\"sourceCommitId\":\"%s\","
                    "\"coveredCommitId\":\"%s\",\"kind\":\"reviewed\"} -->\n" % (head, head))
    summary = {"user": BOT_USER, "created_at": "2026-10-28T00:00:00Z", "updated_at": "2026-10-29T10:20:00Z",
               "body": summary_body}
    check("summary marker on head", cr_state(head, since, [], [summary])[0] == "reviewed")
    stale = dict(summary, updated_at="2026-10-29T09:00:00Z")
    check("summary refreshed before since", cr_state(head, since, [], [stale])[0] == "pending")
    other = dict(summary, body=summary_body.replace(head, "b" * 40))
    check("summary marker for another SHA", cr_state(head, since, [], [other])[0] == "pending")
    not_summary = dict(summary, body=summary_body.replace("<!-- walkthrough_start -->\n", ""))
    check("marker outside the summary", cr_state(head, since, [], [not_summary])[0] == "pending")
    rl_body = ("<!-- This is an auto-generated comment: rate limited by coderabbit.ai -->\n> [!WARNING]\n"
               "> ## Rate limit exceeded\n>\n> Please wait **26 minutes and 15 seconds** before requesting another review.\n")
    rl_summary = {"user": BOT_USER, "created_at": "2026-10-28T00:00:00Z", "updated_at": "2026-10-29T10:20:00Z",
                  "body": rl_body, "html_url": "u"}
    st, det = cr_state(head, since, [], [rl_summary])
    check("rate limit in an edited summary", st == "rate_limited" and "26 minutes and 15 seconds" in det, (st, det))
    check("old rate-limit summary ignored",
          cr_state(head, since, [], [dict(rl_summary, updated_at="2026-10-29T09:00:00Z")])[0] == "pending")
    later_fin = {"user": BOT_USER, "created_at": "2026-10-29T10:30:00Z", "body": _REPLY_MARK + "\nReview finished.", "html_url": "f"}
    check("a later reply beats an older rate-limit summary",
          cr_state(head, since, [], [rl_summary, later_fin])[0] == "no_review")
    check("a later rate-limit summary beats an older reply",
          cr_state(head, since, [], [dict(rl_summary, updated_at="2026-10-29T10:40:00Z"), later_fin])[0] == "rate_limited")
    check("review on head beats a rate-limit summary",
          cr_state(head, since, [new_rev], [rl_summary])[0] == "reviewed")
    quoted_mark = {"user": BOT_USER, "created_at": "2026-10-29T10:50:00Z", "html_url": "q",
                   "body": "> " + _REPLY_MARK + "\nReview finished."}
    check("a quoted reply marker is not a command reply", cr_state(head, since, [], [quoted_mark])[0] == "pending")
    new_notice = ("<!-- This is an auto-generated comment: rate limited by coderabbit.ai -->\n\n> [!WARNING]\n"
                  "> ## Review limit reached\n> \n> **Next included review available in 5 minutes.**\n")
    st2, det2 = cr_state(head, since, [], [dict(rl_summary, body=new_notice)])
    check("wait time from the 'available in' notice", st2 == "rate_limited" and "5 minutes" in det2, (st2, det2))
    colon = new_notice.replace("available in 5 minutes", "available in: **12 minutes**")
    st3, det3 = cr_state(head, since, [], [dict(rl_summary, body=colon)])
    check("wait time from 'available in:'", st3 == "rate_limited" and "12 minutes" in det3, (st3, det3))
    plain = {"user": BOT_USER, "created_at": "2026-10-29T10:50:00Z", "body": "Review finished. Rate limited.", "html_url": "p"}
    check("a bot comment that is not a command reply is ignored", cr_state(head, since, [], [plain])[0] == "pending")
    prose = dict(rl_summary, body="<!-- walkthrough_start -->\n> ## Rate limit exceeded\nThe watcher handles rate limited replies.")
    check("rate-limit prose without the marker is ignored", cr_state(head, since, [], [prose])[0] == "pending")
    check("ci none", ci_state([], []) == "pending")
    check("ci coderabbit ignored", ci_state([{"context": "CodeRabbit", "state": "success"}], []) == "pending")
    check("ci newest status", ci_state([{"context": "ci", "state": "success"},
                                        {"context": "ci", "state": "failure"}], []) == "success")
    check("ci check run fail", ci_state([], [{"status": "completed", "conclusion": "failure",
                                              "app": {"slug": "circleci"}}]) == "failure")
    check("ci check run pending", ci_state([{"context": "ci", "state": "success"}],
                                           [{"status": "in_progress", "app": {"slug": "x"}}]) == "pending")

    # End to end through main(): fake GitHub, fake clock, no real sleep.
    def scenario(steps: list[Callable[[_FakeGitHub], None]], argv: list[str], token: str = "") -> tuple[int, list[dict], _FakeGitHub, Http]:
        gh = _FakeGitHub()
        http = Http(token, opener=gh)
        now = [0.0]
        it = iter(steps)

        def fake_sleep(sec: float) -> None:
            now[0] += sec
            step = next(it, None)
            if step:
                step(gh)

        buf = io.StringIO()
        code = main(["7", "--repo", "o/r", *argv], http=http, sleep=fake_sleep, clock=lambda: now[0], out=buf)
        return code, [json.loads(l) for l in buf.getvalue().splitlines()], gh, http

    def add_review(gh: _FakeGitHub) -> None:
        gh.reviews.append({"id": 1, "user": BOT_USER, "commit_id": gh.HEAD, "submitted_at": "2026-10-29T10:10:00Z", "body": "**Actionable comments posted: 0**"})

    def ci_ok(gh: _FakeGitHub) -> None:
        gh.statuses.insert(0, {"context": "ci/circleci", "state": "success"})

    code, lines, gh, http = scenario([lambda g: None, add_review], [])
    check("exit on review", code == 0 and lines[-1]["cr"] == "reviewed", lines)
    check("one line per change", len(lines) == 2, lines)
    check("304 reused", http.not_modified > 0, http.not_modified)
    check("no token, no header", all(a == "" for a in gh.seen_auth), gh.seen_auth[:2])

    code, lines, gh, http = scenario([add_review, lambda g: None, ci_ok], ["--until", "all"], token="t0k")
    check("until all waits for ci", code == 0 and lines[-1] == {**lines[-1], "cr": "reviewed", "ci": "success"}, lines)
    check("token header", all(a == "Bearer t0k" for a in gh.seen_auth), gh.seen_auth[:1])

    def ci_fail(gh: _FakeGitHub) -> None:
        gh.statuses.insert(0, {"context": "ci/circleci", "state": "failure"})

    code, lines, _, _ = scenario([ci_fail], [])
    check("exit on ci failure", code == 0 and lines[-1]["ci"] == "failure" and lines[-1]["cr"] == "pending", lines)

    def reply_finished(gh: _FakeGitHub) -> None:
        gh.comments.append({"user": BOT_USER, "created_at": "2026-10-29T10:30:00Z",
                            "body": _REPLY_MARK + "\nReview finished.", "html_url": "u"})

    code, lines, _, _ = scenario([reply_finished], [])
    check("exit on reply-only", code == 0 and lines[-1]["cr"] == "no_review", lines)
    code, lines, _, _ = scenario([reply_finished], ["--since", "2026-10-29T11:00:00Z", "--timeout", "500",
                                                    "--interval", "100"])
    check("--since ignores older reply", code == 2 and lines[-1].get("event") == "timeout", lines)

    # A rate-limited poll is reported once and waits for the reset.
    class _Limited(_FakeGitHub):
        def __init__(self) -> None:
            super().__init__()
            self.limited = 2

        def __call__(self, req):
            if self.limited:
                self.limited -= 1
                raise urllib.error.HTTPError(req.full_url, 403, "rate limited",
                                             _lower_headers(int(time.time()) + 300), None)
            return super().__call__(req)

    gh = _Limited()
    add_review(gh)
    http = Http("", opener=gh)
    waits: list[float] = []
    now = [0.0]

    def rec_sleep(sec: float) -> None:
        waits.append(sec)
        now[0] += sec

    buf = io.StringIO()
    code = main(["7", "--repo", "o/r", "--interval", "30"], http=http, sleep=rec_sleep,
                clock=lambda: now[0], out=buf)
    lines = [json.loads(l) for l in buf.getvalue().splitlines()]
    fails = [l for l in lines if l.get("event") == "fetch_failed"]
    check("rate limit reported once", len(fails) == 1, lines)
    check("rate limit waits for reset", waits and waits[0] >= 250, waits)
    check("recovers after reset", code == 0 and lines[-1].get("cr") == "reviewed", lines)

    # Bad --since values are rejected before any request.
    for bad in ("ontem", "2026-10-29T09:00:00"):
        import contextlib
        with contextlib.redirect_stderr(io.StringIO()):
            code = main(["7", "--repo", "o/r", "--since", bad], http=Http("", opener=_FakeGitHub()),
                        sleep=lambda s: None, clock=lambda: 0.0, out=io.StringIO())
        check(f"--since {bad!r} rejected", code == 3, code)

    # Negative, NaN and infinite durations are rejected before any request. 0 stays legal.
    class _Explode(_FakeGitHub):
        def __call__(self, req):
            raise RuntimeError(f"request before validation: {req.full_url}")

    for flag, bad in (("--interval", "-1"), ("--interval", "nan"), ("--interval", "inf"),
                      ("--interval", "-inf"), ("--timeout", "-5"), ("--timeout", "nan"),
                      ("--timeout", "inf"), ("--timeout", "-inf")):
        try:
            with contextlib.redirect_stderr(io.StringIO()) as err:
                code = main(["7", "--repo", "o/r", f"{flag}={bad}"], http=Http("", opener=_Explode()),
                            sleep=lambda s: None, clock=lambda: 0.0, out=io.StringIO())
            text = err.getvalue()
            check(f"{flag} {bad} rejected before any request",
                  code == 3 and flag in text and "finite" in text, (code, text))
        except Exception as exc:
            check(f"{flag} {bad} rejected before any request", False, exc)

    # Default intervals: 30 s with a token, 540 s without (304 still counts).
    for tok, want in (("t", 30), ("", 540)):
        gh = _FakeGitHub()
        waits2: list[float] = []
        clock2 = [0.0]

        def sl(sec: float, waits2=waits2, clock2=clock2) -> None:
            waits2.append(sec)
            clock2[0] += sec

        main(["7", "--repo", "o/r", "--timeout", "1000"], http=Http(tok, opener=gh), sleep=sl,
             clock=lambda clock2=clock2: clock2[0], out=io.StringIO())
        check(f"interval token={bool(tok)}", waits2 and waits2[0] == want, waits2[:2])

    # Explicit 0 keeps that default (it is not a rejected duration).
    for tok, want in (("t", 30), ("", 540)):
        gh = _FakeGitHub()
        waits0: list[float] = []
        clock0 = [0.0]

        def sl0(sec: float, waits0=waits0, clock0=clock0) -> None:
            waits0.append(sec)
            clock0[0] += sec

        main(["7", "--repo", "o/r", "--timeout", "1000", "--interval", "0"], http=Http(tok, opener=gh),
             sleep=sl0, clock=lambda clock0=clock0: clock0[0], out=io.StringIO())
        check(f"interval 0 token={bool(tok)}", waits0 and waits0[0] == want, waits0[:2])

    # Retry-After (secondary limit) wins when it is later than X-RateLimit-Reset.
    class _Secondary(_FakeGitHub):
        def __init__(self) -> None:
            super().__init__()
            self.limited = 1

        def __call__(self, req):
            if self.limited:
                self.limited -= 1
                from email.message import Message
                m = Message()
                m["Retry-After"] = "600"
                raise urllib.error.HTTPError(req.full_url, 403, "secondary rate limit", m, None)
            return super().__call__(req)

    class _SecondaryNoHeader(_FakeGitHub):
        def __init__(self) -> None:
            super().__init__()
            self.limited = 1

        def __call__(self, req):
            if self.limited:
                self.limited -= 1
                import io as _io
                from email.message import Message
                m = Message()
                m["X-RateLimit-Remaining"] = "4000"
                raise urllib.error.HTTPError(req.full_url, 403, "Forbidden", m,
                                             _io.BytesIO(b'{"message":"You have exceeded a secondary rate limit."}'))
            return super().__call__(req)

    gh2 = _SecondaryNoHeader()
    add_review(gh2)
    waits4: list[float] = []
    now4 = [0.0]

    def rec4(sec: float) -> None:
        waits4.append(sec)
        now4[0] += sec

    main(["7", "--repo", "o/r", "--interval", "5"], http=Http("", opener=gh2), sleep=rec4,
         clock=lambda: now4[0], out=io.StringIO())
    check("secondary limit without headers waits 60 s", waits4 and waits4[0] >= 55, waits4[:2])

    gh = _Secondary()
    add_review(gh)
    waits3: list[float] = []
    now3 = [0.0]

    def rec3(sec: float) -> None:
        waits3.append(sec)
        now3[0] += sec

    main(["7", "--repo", "o/r", "--interval", "30"], http=Http("", opener=gh), sleep=rec3,
         clock=lambda: now3[0], out=io.StringIO())
    check("Retry-After honoured", waits3 and waits3[0] >= 590, waits3[:2])

    # A missing Cockpit CLI does not break the result; a timeout is also sent to --notify-task.
    import contextlib
    saved_c = os.environ.get("AIO_COCKPIT")
    os.environ["AIO_COCKPIT"] = "/nonexistent/cockpit"
    try:
        with contextlib.redirect_stderr(io.StringIO()) as err:
            code = main(["7", "--repo", "o/r", "--timeout", "10", "--interval", "5", "--notify-task", "t1"],
                        http=Http("", opener=_FakeGitHub()), sleep=lambda s: None,
                        clock=iter([0, 0, 20, 20, 20]).__next__, out=io.StringIO())
        check("timeout notifies and survives a missing CLI", code == 2 and "could not notify task t1" in err.getvalue(),
              (code, err.getvalue()))
    finally:
        if saved_c is None:
            os.environ.pop("AIO_COCKPIT", None)
        else:
            os.environ["AIO_COCKPIT"] = saved_c

    # A hung Cockpit CLI times out without breaking the watch.
    import tempfile as _tf
    with _tf.TemporaryDirectory() as tmpd:
        hang = Path(tmpd) / "cockpit"
        hang.write_text("#!/bin/sh\nsleep 5\n")
        hang.chmod(0o700)
        saved_h = os.environ.get("AIO_COCKPIT")
        os.environ["AIO_COCKPIT"] = str(hang)
        try:
            with contextlib.redirect_stderr(io.StringIO()) as err2:
                notify("t2", "{}", timeout=0.3)
            check("hung Cockpit CLI times out", "could not notify task t2" in err2.getvalue(), err2.getvalue())
        finally:
            if saved_h is None:
                os.environ.pop("AIO_COCKPIT", None)
            else:
                os.environ["AIO_COCKPIT"] = saved_h

    # Token file must be private.
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        tf = Path(tmp) / "token"
        tf.write_text("abc\n")
        tf.chmod(0o644)
        saved = {k: os.environ.pop(k, None) for k in ("GITHUB_TOKEN", "GH_TOKEN")}
        try:
            try:
                load_token(tf)
                check("open token file rejected", False, "no error")
            except TokenError:
                pass
            tf.chmod(0o600)
            check("private token file read", load_token(tf) == "abc")
            check("missing token file", load_token(Path(tmp) / "none") == "")
        finally:
            for k, v in saved.items():
                if v is not None:
                    os.environ[k] = v

    class _BrokenPath:
        """Stand-in path: exists, while stat() or read_text() raises OSError."""

        def __init__(self, op: str) -> None:
            self.op = op

        def exists(self) -> bool:
            return True

        def stat(self):
            if self.op == "stat":
                raise OSError(13, "Permission denied", "token")

            class _Stat:
                st_mode = 0o100600

            return _Stat()

        def read_text(self, encoding: str = "utf-8") -> str:
            raise OSError(13, "Permission denied", "token")

    # An environment token is returned before the path is touched, so these checks
    # clear both names (and restore them). The planted values make a missing clear fail.
    saved_tok = {k: os.environ.pop(k, None) for k in ("GITHUB_TOKEN", "GH_TOKEN")}
    try:
        os.environ["GITHUB_TOKEN"] = "from-env"
        os.environ["GH_TOKEN"] = "from-gh"
        os.environ.pop("GITHUB_TOKEN", None)
        os.environ.pop("GH_TOKEN", None)
        for op in ("stat", "read"):
            try:
                load_token(_BrokenPath(op))
                check(f"token {op} OSError is TokenError", False, "no error")
            except TokenError as exc:
                check(f"token {op} OSError is TokenError", "Permission denied" in str(exc), exc)
            except OSError as exc:
                check(f"token {op} OSError is TokenError", False, f"raw {exc}")

        with tempfile.TemporaryDirectory() as tmp2:
            folder = Path(tmp2) / "token-dir"
            folder.mkdir()
            folder.chmod(0o700)
            try:
                load_token(folder)
                check("token directory is TokenError", False, "no error")
            except TokenError as exc:
                check("token directory is TokenError", "Is a directory" in str(exc), exc)
            except OSError as exc:
                check("token directory is TokenError", False, f"raw {exc}")
    finally:
        os.environ.pop("GITHUB_TOKEN", None)
        os.environ.pop("GH_TOKEN", None)
        for k, v in saved_tok.items():
            if v is not None:
                os.environ[k] = v

    # A bad nested payload is fetch_failed, never an exception.
    class _Shaped(_FakeGitHub):
        def __init__(self, kind: str) -> None:
            super().__init__()
            self.kind = kind
            if kind == "naive-date":
                self.reviews.append({"id": 1, "user": BOT_USER, "commit_id": self.HEAD, "state": "COMMENTED",
                                     "submitted_at": "2026-10-29T10:10:00Z",
                                     "body": "**Actionable comments posted: 0**"})

        def body(self, url: str) -> Any:
            path = url.split("/repos/o/r", 1)[1].split("?", 1)[0]
            commit = path.startswith("/commits/") and path.count("/") == 2
            if self.kind == "head-type" and path == "/pulls/7":
                return {"head": "nope"}
            if self.kind == "sha-type" and path == "/pulls/7":
                return {"head": {"sha": 123}}
            if self.kind == "missing-head" and path == "/pulls/7":
                return {}
            if self.kind == "no-date" and commit:
                return {"commit": {"committer": {"date": "yesterday"}}}
            if self.kind == "naive-date" and commit:
                return {"commit": {"committer": {"date": "2026-10-29T10:00:00"}}}
            if self.kind == "commit-shape" and commit:
                return {"commit": []}
            if self.kind == "status-body" and path.endswith("/status"):
                return []
            if self.kind == "checks-body" and path.endswith("/check-runs"):
                return []
            if self.kind == "statuses-type" and path.endswith("/status"):
                return {"statuses": ["pending"]}
            if self.kind == "checks-type" and path.endswith("/check-runs"):
                return {"check_runs": {"id": 1}}
            if self.kind == "statuses-null" and path.endswith("/status"):
                return {"statuses": None}
            if self.kind == "checks-null" and path.endswith("/check-runs"):
                return {"check_runs": None}
            if self.kind == "statuses-missing" and path.endswith("/status"):
                return {}
            if self.kind == "checks-missing" and path.endswith("/check-runs"):
                return {}
            return super().body(url)

    def watch_bad(kind: str) -> tuple[int, list[dict]]:
        now = [0.0]

        def sl(sec: float, now=now) -> None:
            now[0] += 10

        buf = io.StringIO()
        code = main(["7", "--repo", "o/r", "--timeout", "15", "--interval", "5"],
                    http=Http("", opener=_Shaped(kind)), sleep=sl, clock=lambda: now[0], out=buf)
        return code, [json.loads(l) for l in buf.getvalue().splitlines()]

    for kind in ("head-type", "sha-type", "missing-head", "no-date", "naive-date", "commit-shape",
                 "status-body", "checks-body", "statuses-type", "checks-type", "statuses-null",
                 "checks-null", "statuses-missing", "checks-missing"):
        try:
            code, lines = watch_bad(kind)
            check(f"bad shape {kind} is fetch_failed",
                  code == 2 and any(l.get("event") == "fetch_failed" for l in lines)
                  and all("head" not in l for l in lines), lines)
        except Exception as exc:
            check(f"bad shape {kind} is fetch_failed", False, f"{type(exc).__name__}: {exc}")

    # The head moved while CI and reviews were collected: report the new head, not the old one.
    class _SwapHead(_FakeGitHub):
        NEW = "b" * 40

        def __init__(self) -> None:
            super().__init__()
            self.HEAD = "a" * 40

        def body(self, url: str) -> Any:
            path = url.split("/repos/o/r", 1)[1].split("?", 1)[0]
            sha = path.split("/")[2] if path.startswith("/commits/") else ""
            if path.endswith("/status") and sha == "a" * 40:
                self.HEAD = self.NEW
                return {"statuses": [{"context": "ci/circleci", "state": "failure"}]}
            if path.endswith("/status") and sha == self.NEW:
                return {"statuses": [{"context": "ci/circleci", "state": "success"}]}
            return super().body(url)

    gh = _SwapHead()
    gh.reviews.append({"id": 9, "user": BOT_USER, "commit_id": _SwapHead.NEW, "state": "COMMENTED",
                       "submitted_at": "2026-10-29T10:10:00Z", "body": "**Actionable comments posted: 0**"})
    buf = io.StringIO()
    code = main(["7", "--repo", "o/r", "--timeout", "100", "--interval", "10"], http=Http("", opener=gh),
                sleep=lambda s: None, clock=lambda: 0.0, out=buf)
    lines = [json.loads(l) for l in buf.getvalue().splitlines()]
    check("head moved mid-poll reports the new head",
          code == 0 and lines[-1]["head"] == "b" * 7 and lines[-1]["ci"] == "success"
          and lines[-1]["cr"] == "reviewed", lines)

    # Every re-read shows a new SHA. Three attempts, then fetch_failed — not a stale snapshot.
    class _NeverStable(_FakeGitHub):
        def __init__(self) -> None:
            super().__init__()
            self.n = 0

        def body(self, url: str) -> Any:
            path = url.split("/repos/o/r", 1)[1].split("?", 1)[0]
            if path == "/pulls/7":
                sha = f"{self.n % 16:x}" * 40
                self.n += 1
                return {"head": {"sha": sha}}
            return super().body(url)

    moving = _NeverStable()
    http_m = Http("", opener=moving)
    buf = io.StringIO()
    code = main(["7", "--repo", "o/r", "--timeout", "0", "--interval", "5"], http=http_m,
                sleep=lambda s: None, clock=lambda: 0.0, out=buf)
    lines = [json.loads(l) for l in buf.getvalue().splitlines()]
    # 3 attempts × (PR, commit, status, checks, reviews, comments, PR again) = 21.
    check("changing head gives up",
          code == 2 and bool(lines) and lines[0].get("event") == "fetch_failed" and "head" not in lines[0]
          and http_m.requests == 21, (lines, http_m.requests))

    if failures:
        for f in failures:
            print(f"FAIL {f}")
        return 1
    print("ok   pr-watch self-test")
    return 0


if __name__ == "__main__":
    sys.exit(main())
