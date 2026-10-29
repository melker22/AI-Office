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
Without a token a 304 still counts, so the interval is 420 s (at least six
requests per poll, 60 requests per hour shared by the whole IP); with a read-only token (GITHUB_TOKEN / GH_TOKEN or
~/.config/aio-pr-watch/token, mode 600) it is 30 s (5000 per hour).

Exit codes: 0 = something to act on (see the last line), 2 = timeout,
3 = bad arguments or token file.
"""
from __future__ import annotations

import argparse
import json
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
    mode = stat.S_IMODE(path.stat().st_mode)
    if mode & 0o077:
        raise TokenError(f"{path} is readable by others (mode {mode:o}); run chmod 600 {path}")
    return path.read_text(encoding="utf-8").strip()


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
            if exc.code == 429 or (exc.code == 403 and hget("X-RateLimit-Remaining") == "0"):
                try:
                    self.rate_reset = float(hget("X-RateLimit-Reset") or 0)
                except ValueError:
                    self.rate_reset = 0.0
                if not self.rate_reset:
                    self.rate_reset = time.time() + 60
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


def cr_state(head: str, since: datetime, reviews: list[dict], comments: list[dict]) -> tuple[str, str]:
    for r in reviews:
        if not (is_bot(r.get("user")) and r.get("commit_id") == head):
            continue
        sub = r.get("submitted_at")
        if sub and iso(sub) <= since:
            continue  # an older review of this SHA, from before the request being watched
        return "reviewed", f"review {r.get('id')} at {sub}"
    for c in comments:  # the summary is edited in place: its updated_at says when it was refreshed
        ts = c.get("updated_at") or c.get("created_at")
        if ts and iso(ts) > since and summary_covers(c, head):
            return "reviewed", f"summary marks {head[:7]} reviewed ({c.get('html_url')})"
    replies = [
        c for c in comments
        if is_bot(c.get("user")) and c.get("created_at") and iso(c["created_at"]) > since
    ]
    for c in reversed(replies):  # newest reply decides
        outcome = reply_outcome(c.get("body") or "")
        if outcome:
            return outcome, f"reply {c.get('html_url')}"
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
        pr = self.http.get(f"{self.api}/pulls/{self.pr}")
        if pr.status not in (200, 304) or not isinstance(pr.body, dict):
            return None
        head = pr.body["head"]["sha"]
        commit = self.http.get(f"{self.api}/commits/{head}")
        if commit.status not in (200, 304) or not isinstance(commit.body, dict):
            return None
        committed = iso(commit.body["commit"]["committer"]["date"])
        since = max(committed, self.since) if self.since else committed
        status = self.http.get(f"{self.api}/commits/{head}/status?per_page={PER_PAGE}")
        checks = self.http.get(f"{self.api}/commits/{head}/check-runs?per_page={PER_PAGE}")
        reviews = self._list(f"/pulls/{self.pr}/reviews")
        comments = self._list(f"/issues/{self.pr}/comments")
        if (status.status not in (200, 304) or checks.status not in (200, 304)
                or reviews is None or comments is None):
            return None
        ci = ci_state(status.body.get("statuses") or [], checks.body.get("check_runs") or [])
        cr, detail = cr_state(head, since, reviews, comments)
        return Snapshot(head, ci, cr, detail)


def done(s: Snapshot, until: str) -> bool:
    if s.ci == "failure":
        return True
    if s.cr == "pending":
        return False
    return until == "cr" or s.ci != "pending"


def notify(task: str, line: str) -> None:
    cockpit = os.environ.get("AIO_COCKPIT") or str(Path.home() / ".agi-tools" / "bin" / "cockpit")
    subprocess.run([cockpit, "task", "send", task, "--stdin"], input=f"pr-watch: {line}\n",
                   text=True, check=False, capture_output=True)


def run(args: argparse.Namespace, http: Http, sleep: Callable[[float], None] = time.sleep,
        clock: Callable[[], float] = time.monotonic, out=sys.stdout) -> int:
    # At least six requests per poll (more when reviews or comments span pages).
    # Without a token a 304 still counts against the 60/h anonymous quota, so
    # poll every 7 min (about 51 requests per hour); with a token, every 30 s.
    interval = args.interval or (30 if http.token else 420)
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
            print(json.dumps({"pr": args.pr, "event": "timeout"}), file=out, flush=True)
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
        if path == f"/commits/{self.HEAD}":
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
    check("review on head", cr_state(head, since, [{"user": BOT_USER, "commit_id": head}], [])[0] == "reviewed")
    old_rev = {"user": BOT_USER, "commit_id": head, "submitted_at": "2026-10-29T09:30:00Z"}
    check("review before since", cr_state(head, since, [old_rev], [])[0] == "pending")
    new_rev = {"user": BOT_USER, "commit_id": head, "submitted_at": "2026-10-29T10:01:00Z"}
    check("review after since", cr_state(head, since, [old_rev, new_rev], [])[0] == "reviewed")
    check("review on old head", cr_state(head, since, [{"user": BOT_USER, "commit_id": "b" * 40}], [])[0] == "pending")
    lookalike = {"login": BOT, "type": "User"}
    check("look-alike user", cr_state(head, since, [{"user": lookalike, "commit_id": head}], [])[0] == "pending")
    old = {"user": BOT_USER, "created_at": "2026-10-29T09:00:00Z", "body": "Review finished."}
    check("reply before trigger", cr_state(head, since, [], [old])[0] == "pending")
    fin = {"user": BOT_USER, "created_at": "2026-10-29T10:05:00Z", "body": "Review finished."}
    check("finished, no review", cr_state(head, since, [], [fin])[0] == "no_review")
    rl = {"user": BOT_USER, "created_at": "2026-10-29T10:06:00Z", "body": "Review rate limited."}
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
        gh.reviews.append({"id": 1, "user": BOT_USER, "commit_id": gh.HEAD, "submitted_at": "2026-10-29T10:10:00Z"})

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
                            "body": "Review finished.", "html_url": "u"})

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

    # Default intervals: 30 s with a token, 420 s without (304 still counts).
    for tok, want in (("t", 30), ("", 420)):
        gh = _FakeGitHub()
        waits2: list[float] = []
        clock2 = [0.0]

        def sl(sec: float, waits2=waits2, clock2=clock2) -> None:
            waits2.append(sec)
            clock2[0] += sec

        main(["7", "--repo", "o/r", "--timeout", "1000"], http=Http(tok, opener=gh), sleep=sl,
             clock=lambda clock2=clock2: clock2[0], out=io.StringIO())
        check(f"interval token={bool(tok)}", waits2 and waits2[0] == want, waits2[:2])

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

    if failures:
        for f in failures:
            print(f"FAIL {f}")
        return 1
    print("ok   pr-watch self-test")
    return 0


if __name__ == "__main__":
    sys.exit(main())
