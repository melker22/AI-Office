#!/usr/bin/env python3
"""usage-guard — waits out provider usage limits and resumes the work that stopped.

When an agent hits its usage limit, Cockpit stops it: a Fleet Run pauses with the node `interrupted`
("usage limit reached until <time>"), and a plain task waits with `waitingReason: usage_limit`.
Nothing resumes on its own. This guard does:

  1. `record` (Cockpit hook on task.waiting, waitingReason=usage_limit): stores the stopped task and
     its reset time in the state file.
  2. `daemon` (started by a Cockpit app.ready hook; one instance per user via flock) every minute:
     - warns once per window when Claude (5 h / 7 days) or Cursor (monthly) passes the threshold (90%);
     - after the reset time, resumes what stopped *because of the limit*: `cockpit fleet resume <run>`
       for a Fleet, `cockpit task send <task> "continue"` for a task (this includes the orchestrator);
     - every few minutes scans for Fleet Runs paused by a usage limit that no hook recorded.
     A Run paused by hand, or a task waiting for another reason, is never touched.
  3. `status` prints the state; `once` runs one cycle (with `--dry-run`, it only prints the actions).

Stdlib only. State: $XDG_STATE_HOME/aio-usage-guard (default ~/.local/state/aio-usage-guard).
Cockpit CLI: $AIO_GUARD_COCKPIT, else ~/.agi-tools/bin/cockpit, else `cockpit` on PATH.

  usage-guard.py record < event.json
  usage-guard.py daemon [--interval 60] [--threshold 90]
  usage-guard.py once [--dry-run]
  usage-guard.py status
  usage-guard.py --self-test
"""
from __future__ import annotations

import argparse
import contextlib
import fcntl
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path

RESUME_GRACE = timedelta(seconds=90)      # wait a little past the advertised reset
UNKNOWN_RESET_RETRY = timedelta(minutes=15)
SCAN_EVERY = timedelta(minutes=5)
RECENT_DONE = timedelta(minutes=10)       # a stop with unknown reset this soon after a resume = same stop
CONTINUE_TEXT = ("O limite de uso renovou (usage-guard). Continue exatamente de onde parou: "
                 "retome a última etapa interrompida sem refazer o que já foi concluído.")
USAGE_RE = re.compile(r"usage limit reached until ([0-9T:.+\-Z]+)", re.I)


# ----------------------------------------------------------------------------------------- time
def now_utc() -> datetime:
    override = os.environ.get("AIO_GUARD_NOW")
    return parse_time(override) if override else datetime.now(timezone.utc)


def parse_time(value: str | None) -> datetime | None:
    if not value:
        return None
    try:
        dt = datetime.fromisoformat(value.strip().replace("Z", "+00:00"))
    except ValueError:
        return None
    return dt if dt.tzinfo else dt.replace(tzinfo=timezone.utc)


def iso(dt: datetime | None) -> str | None:
    return dt.astimezone(timezone.utc).isoformat(timespec="seconds") if dt else None


# ---------------------------------------------------------------------------------------- state
def state_dir() -> Path:
    base = os.environ.get("AIO_GUARD_STATE") or os.path.join(
        os.environ.get("XDG_STATE_HOME") or os.path.expanduser("~/.local/state"), "aio-usage-guard")
    p = Path(base)
    p.mkdir(parents=True, exist_ok=True, mode=0o700)
    return p


@contextlib.contextmanager
def locked_state(persist: bool = True):
    """Read-modify-write of state.json under an exclusive lock (hook and daemon run concurrently).
    persist=False (dry runs) reads the state and discards every change."""
    d = state_dir()
    with open(d / "state.lock", "a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        path = d / "state.json"
        try:
            state = json.loads(path.read_text(encoding="utf-8"))
        except (FileNotFoundError, json.JSONDecodeError):
            state = {}
        state.setdefault("markers", [])   # {key, kind, id, run, name, agent, reset_at, created, done}
        state.setdefault("warned", [])    # window keys already announced
        state.setdefault("last_scan", None)
        try:
            yield state
        finally:
            # Persist even when the body raised: a marker finished before the error must stay done.
            # (No `return` here: it would swallow the body's exception.)
            if persist:
                tmp = path.with_suffix(".tmp")
                tmp.write_text(json.dumps(state, ensure_ascii=False, indent=1), encoding="utf-8")
                os.replace(tmp, path)


def log(msg: str) -> None:
    line = f"{iso(now_utc())} {msg}"
    print(line, flush=True)
    try:
        with open(state_dir() / "guard.log", "a", encoding="utf-8") as f:
            f.write(line + "\n")
    except OSError:
        pass


# -------------------------------------------------------------------------------------- cockpit
def cockpit_bin() -> str:
    env = os.environ.get("AIO_GUARD_COCKPIT")
    if env:
        return env
    home = os.path.expanduser("~/.agi-tools/bin/cockpit")
    return home if os.path.exists(home) else (shutil.which("cockpit") or "cockpit")


def cockpit(*args: str, timeout: float = 60) -> dict | None:
    """Run the Cockpit CLI and return its `data`; None when the call fails (logged, never raised)."""
    try:
        proc = subprocess.run([cockpit_bin(), *args], capture_output=True, text=True, timeout=timeout,
                              check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        log(f"cockpit {' '.join(args[:2])} failed: {exc!r}")
        return None
    try:
        out = json.loads(proc.stdout)
    except json.JSONDecodeError:
        log(f"cockpit {' '.join(args[:2])}: not JSON (exit {proc.returncode}): {proc.stdout[-200:]!r}")
        return None
    if not out.get("ok"):
        log(f"cockpit {' '.join(args[:2])}: {out.get('error')}")
        return None
    return out.get("data")


class Actions:
    """Side effects, so `--dry-run` and the self-test can observe them."""

    def __init__(self, dry_run: bool):
        self.dry_run = dry_run
        self.done: list[str] = []

    def _do(self, desc: str, *args: str) -> bool:
        self.done.append(desc)
        if self.dry_run:
            log(f"DRY-RUN {desc}")
            return True
        log(desc)
        return cockpit(*args) is not None

    def notify(self, text: str) -> bool:
        return self._do(f"notify: {text}", "display", "--title", "Guardião de uso", "--text", text)

    def resume_fleet(self, run_id: str) -> bool:
        return self._do(f"fleet resume {run_id}", "fleet", "resume", run_id)

    def continue_task(self, task_id: str) -> bool:
        return self._do(f"task send {task_id}", "task", "send", task_id, "--text", CONTINUE_TEXT)


# ----------------------------------------------------------------------------------- recording
def marker_key(kind: str, ident: str, reset_at: str | None) -> str:
    return f"{kind}:{ident}:{reset_at or '?'}"


def add_marker(state: dict, kind: str, ident: str, name: str, agent: str, reset_at: datetime | None,
               run: str | None = None, seen: datetime | None = None) -> bool:
    key = marker_key(kind, ident, iso(reset_at))
    now = now_utc()
    for m in state["markers"]:
        if m["key"] != key:
            continue
        if m["done"] is None:
            return False                      # already waiting to be resumed
        if reset_at is not None:
            return False                      # this exact limit was already resumed
        # Unknown reset time: the same key returns when the item stops again after a resume. Only a
        # stop right after the resume (the Run may still report "paused") counts as the same one.
        if now - (parse_time(m["done"]) or now) < RECENT_DONE:
            return False
    state["markers"].append({"key": key, "kind": kind, "id": ident, "run": run, "name": name,
                             "agent": agent, "reset_at": iso(reset_at), "created": iso(seen or now_utc()),
                             "done": None})
    return True


def record_event(event: dict) -> str:
    """Hook entry: a task entered waiting_confirmation. Only usage-limit waits are recorded.

    The hook never takes the state lock (the daemon may hold it for minutes of Cockpit calls and the
    hook has a short timeout): it appends one line to inbox.jsonl under its own short lock, and the
    daemon ingests the inbox at the start of each cycle."""
    task = event.get("task") or {}
    if task.get("waitingReason") != "usage_limit":
        return "ignored (not a usage limit)"
    entry = {"id": task.get("id") or "", "name": task.get("name") or "", "agent": task.get("agentType") or "",
             "is_fleet": bool(task.get("isFleet")), "run": task.get("fleetRunId") or "",
             "reset_at": task.get("usageLimitResetAt"), "seen": iso(now_utc())}
    d = state_dir()
    with open(d / "inbox.lock", "a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        with open(d / "inbox.jsonl", "a", encoding="utf-8") as f:
            f.write(json.dumps(entry, ensure_ascii=False) + "\n")
    what = f"fleet {entry['run']}" if entry["is_fleet"] and entry["run"] else f"task {entry['id']}"
    msg = f"queued {what} until {entry['reset_at'] or 'unknown'}"
    log(msg)
    return msg


def ingest_inbox(state: dict) -> int:
    """Turn the hook's queued stops into markers (called with the state lock held). Returns how many bytes
    of inbox.jsonl were read; ack_inbox() drops them only after state.json was written, so a crash in
    between replays them (markers are deduplicated, so a replay is harmless)."""
    d = state_dir()
    with open(d / "inbox.lock", "a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            raw = (d / "inbox.jsonl").read_bytes()
        except FileNotFoundError:
            return 0
    lines = raw.decode("utf-8", errors="replace").splitlines()
    for line in lines:
        try:
            e = json.loads(line)
        except json.JSONDecodeError:
            e = None
        if not isinstance(e, dict):
            # A bad line must not abort the cycle: the inbox would never be acknowledged and every
            # later stop would be stuck behind it.
            log(f"inbox: dropped a malformed line {line[:80]!r}")
            continue
        reset_at = parse_time(e.get("reset_at"))
        if e.get("is_fleet") and not e.get("run"):
            # A Fleet node without its Run id: resuming the node task would bypass the Run. The scan
            # finds the paused Run and resumes it properly.
            log(f"inbox: Fleet task {e.get('id')} without a Run id; left to the Fleet scan")
            continue
        if e.get("is_fleet") and e.get("run"):
            added = add_marker(state, "fleet", e["run"], e.get("name") or "", e.get("agent") or "", reset_at,
                               run=e["run"], seen=parse_time(e.get("seen")))
        else:
            added = add_marker(state, "task", e.get("id") or "", e.get("name") or "", e.get("agent") or "", reset_at,
                               seen=parse_time(e.get("seen")))
        if added:
            log(f"recorded {'fleet ' + e['run'] if e.get('is_fleet') else 'task ' + str(e.get('id'))} "
                f"until {iso(reset_at) or 'unknown'}")
    return len(raw)


def ack_inbox(consumed: int) -> None:
    """Drop the first `consumed` bytes of the inbox (already persisted as markers); keep later lines."""
    if consumed <= 0:
        return
    d = state_dir()
    with open(d / "inbox.lock", "a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        path = d / "inbox.jsonl"
        try:
            raw = path.read_bytes()
        except FileNotFoundError:
            return
        tmp = path.with_suffix(".tmp")
        tmp.write_bytes(raw[consumed:])
        os.replace(tmp, path)


# ------------------------------------------------------------------------------------- the cycle
def usage_windows(usage: dict) -> list[tuple[str, float, datetime | None]]:
    out = []
    claude = usage.get("claude") or {}
    for key, label in (("fiveHour", "Claude (janela de 5 h)"), ("sevenDay", "Claude (semanal)")):
        w = claude.get(key) or {}
        if "usedPercent" in w:
            out.append((label, float(w["usedPercent"]), parse_time(w.get("resetsAt"))))
    cursor = (usage.get("cursor") or {}).get("plan") or {}
    if "usedPercent" in cursor:
        out.append(("Cursor (mensal)", float(cursor["usedPercent"]), parse_time(cursor.get("resetsAt"))))
    return out


def warn_thresholds(state: dict, usage: dict | None, threshold: float, act: Actions) -> None:
    if not usage:
        return
    for label, pct, reset in usage_windows(usage):
        key = f"{label}@{iso(reset) or '?'}"
        # Cockpit reports the reset with a little jitter (18:30 vs 18:29:59): a reset within 15 minutes of
        # one already warned about is the same window.
        same_window = False
        for w in state["warned"]:
            wl, _, wt = w.rpartition("@")
            if wl != label:
                continue
            prev = parse_time(wt)
            if (prev is None and reset is None) or (prev and reset and abs(prev - reset) <= timedelta(minutes=15)):
                same_window = True
                break
        if pct >= threshold and not same_window:
            when = reset.astimezone().strftime("%d/%m %H:%M") if reset else "?"
            act.notify(f"{label} em {pct:.0f}%. Se o limite encher, o trabalho pausa e eu retomo "
                       f"sozinho depois do reset ({when}).")
            state["warned"].append(key)
    state["warned"] = state["warned"][-50:]


def usage_interrupted_nodes(run: dict) -> list[tuple[str, datetime | None]]:
    """(node id, reset time) for nodes interrupted by a usage limit."""
    found = []
    for n in run.get("nodes") or []:
        if n.get("status") != "interrupted":
            continue
        err = n.get("error") or ""
        m = USAGE_RE.search(err)
        # Cockpit's structured reason first; the error text is a fallback for older records.
        if n.get("failureReason") == "usage_limit" or m or "usage limit" in err.lower():
            reset = parse_time(m.group(1)) if m else parse_time(n.get("usageLimitResetAt"))
            found.append((n.get("id") or "?", reset))
    return found


RESTART_MARK = "did not survive the restart"


def restart_interrupted_nodes(run: dict) -> list[str]:
    """Nodes interrupted only because Cockpit restarted ("task did not survive the restart"). Like a usage
    limit, nothing was wrong with the work itself, so the guard resumes them (right away)."""
    return [n.get("id") or "?" for n in run.get("nodes") or []
            if n.get("status") == "interrupted" and RESTART_MARK in (n.get("error") or "").lower()]


def interruption_signature(run: dict) -> str:
    """Identity of a Run's current set of interrupted nodes, to report a manual-review Run only once."""
    return "|".join(sorted(f"{n.get('id')}:{n.get('failureReason') or ''}:{(n.get('error') or '')[:80]}"
                           for n in run.get("nodes") or [] if n.get("status") == "interrupted"))


def other_interrupted_nodes(run: dict) -> list[str]:
    """Interrupted nodes WITHOUT a usage-limit signal (e.g. stopped by the user with Steer/cancel).
    `fleet resume` reruns every interrupted node, so such a Run is left for a human."""
    usage = {nid for nid, _ in usage_interrupted_nodes(run)} | set(restart_interrupted_nodes(run))
    return [n.get("id") or "?" for n in run.get("nodes") or []
            if n.get("status") == "interrupted" and (n.get("id") or "?") not in usage
            # A loop group is marked interrupted *because* a body node was ("loop body was interrupted in
            # iteration N"); it is a consequence, not a separate stop (seen live on wave 24).
            and not (n.get("error") or "").lower().startswith("loop body was interrupted")]


def scan_paused_fleets(state: dict) -> None:
    """Fallback: Runs paused by a usage limit that no hook recorded (e.g. before the guard ran)."""
    data = cockpit("fleet", "list", "--runs")
    runs = data.get("runs") if isinstance(data, dict) else None
    for r in runs if isinstance(runs, list) else []:
        if not isinstance(r, dict) or r.get("status") != "paused" or not r.get("interruptedNodeCount"):
            continue
        if not r.get("runId"):
            log(f"scan: skipped a paused Run entry without runId: {str(r)[:120]}")
            continue
        st = cockpit("fleet", "status", r["runId"])
        run = st.get("run") if isinstance(st, dict) else None
        nodes = usage_interrupted_nodes(run) if isinstance(run, dict) else []
        restarted = restart_interrupted_nodes(run) if isinstance(run, dict) else []
        if (nodes or restarted) and any(m.get("outcome") == "manual" and m.get("run") == r["runId"]
                         and m.get("sig") == interruption_signature(run) for m in state["markers"]):
            continue   # already reported for manual review; wait until the interruptions change
        # Remember when each unknown-reset node was first observed, even if the marker is not due yet
        # (another node's later reset would otherwise start that wait hours late).
        for m in state["markers"]:
            if m.get("run") == r["runId"] and m["done"] is None:
                seen = m.setdefault("unknown_seen", {})
                for nid, t in nodes:
                    if t is None:
                        seen.setdefault(nid, iso(now_utc()))
        # Record even a mixed Run: handle_marker() leaves it paused and tells the user after the reset.
        if nodes or restarted:
            # Only restart interruptions: nothing to wait for (resume after the usual grace period).
            reset = max((t for _, t in nodes if t), default=None) if nodes else now_utc()
            if add_marker(state, "fleet", r["runId"], r.get("fleetName") or "", "", reset, run=r["runId"]):
                log(f"scan: fleet {r['runId']} paused by a usage limit until {iso(reset) or 'unknown'}")
                state["markers"][-1]["unknown_seen"] = {nid: iso(now_utc()) for nid, t in nodes if t is None}


def due(marker: dict, now: datetime) -> bool:
    reset = parse_time(marker.get("reset_at"))
    if reset is None:
        created = parse_time(marker.get("created")) or now
        return now >= created + UNKNOWN_RESET_RETRY
    return now >= reset + RESUME_GRACE


def handle_marker(marker: dict, act: Actions) -> bool:
    """Resume one stopped item. True = finished with this marker (resumed or nothing left to do)."""
    if marker["kind"] == "fleet":
        st = cockpit("fleet", "status", marker["run"])
        run = st.get("run") if isinstance(st, dict) else None
        if not isinstance(run, dict):
            return False
        status = run.get("status")
        if status != "paused":
            log(f"fleet {marker['run']} is {status}; nothing to resume")
            return True
        usage_nodes = usage_interrupted_nodes(run)
        if not usage_nodes and not restart_interrupted_nodes(run):
            log(f"fleet {marker['run']} is paused, but not by a usage limit or a restart: left alone")
            return True
        latest = max((t for _, t in usage_nodes if t), default=None)
        if latest is not None and now_utc() < latest + RESUME_GRACE:
            log(f"fleet {marker['run']}: another node's limit lasts until {iso(latest)}; waiting")
            return False   # every interrupted node must be past its own reset
        unknown = [nid for nid, t in usage_nodes if t is None]
        if unknown:
            # A node with no known reset: the other nodes' resets say nothing about it. Each such node
            # waits the unknown-reset retry period from when the guard FIRST saw it stopped (a second
            # node may stop long after the Run was first recorded).
            seen = marker.setdefault("unknown_seen", {})
            first_seen = {nid: parse_time(seen.get(nid)) or now_utc() for nid in unknown}
            for nid, t in first_seen.items():
                seen.setdefault(nid, iso(t))
            if any(now_utc() < t + UNKNOWN_RESET_RETRY for t in first_seen.values()):
                log(f"fleet {marker['run']}: usage-limited node(s) {unknown} have no reset time; waiting")
                return False
        others = other_interrupted_nodes(run)
        if others:
            log(f"fleet {marker['run']} also has nodes interrupted for another reason {others}: left for a human")
            if not act.notify(f"O limite renovou, mas a Fleet {marker['name'] or marker['run']} tem nós parados por "
                              f"outro motivo ({', '.join(others)}). Não retomei; revise e use `cockpit fleet resume`."):
                return False   # retry the notice next cycle; do not mark the Run as reported
            marker["outcome"] = "manual"
            marker["sig"] = interruption_signature(run)
            return True
        if act.resume_fleet(marker["run"]):
            why = "O limite renovou" if usage_nodes else "O Cockpit reiniciou"
            act.notify(f"{why}: retomei a Fleet {marker['name'] or marker['run']} de onde parou.")
            return True
        return False
    t = cockpit("task", "get", marker["id"])
    task = t.get("task", t) if isinstance(t, dict) else None
    if not isinstance(task, dict):
        return False
    if task.get("status") != "waiting_confirmation" or task.get("waitingReason") != "usage_limit":
        log(f"task {marker['id']} is {task.get('status')}/{task.get('waitingReason')}; nothing to resume")
        return True
    if act.continue_task(marker["id"]):
        act.notify(f"Limite renovou: pedi à task {marker['name'] or marker['id']} para continuar.")
        return True
    return False


def cycle(threshold: float, act: Actions, force_scan: bool = False) -> None:
    now = now_utc()
    usage = cockpit("usage", "--all")
    consumed = 0
    with locked_state(persist=not act.dry_run) as state:
        if not act.dry_run:
            consumed = ingest_inbox(state)
        warn_thresholds(state, usage, threshold, act)
        last = parse_time(state.get("last_scan"))
        if force_scan or last is None or now - last >= SCAN_EVERY:
            try:
                scan_paused_fleets(state)
            except Exception as exc:  # noqa: BLE001 - a bad scan must not block recorded resumes
                log(f"scan failed: {exc!r}")
            state["last_scan"] = iso(now)
        for m in state["markers"]:
            if m["done"] is not None or not due(m, now):
                continue
            try:
                if handle_marker(m, act):
                    m["done"] = iso(now)
            except Exception as exc:  # noqa: BLE001 - one bad marker must not block the others
                log(f"marker {m.get('key')} failed: {exc!r}")
        # keep 7 days of finished markers for `status`
        cutoff = now - timedelta(days=7)
        state["markers"] = [m for m in state["markers"]
                            if m["done"] is None or (parse_time(m["done"]) or now) >= cutoff]
    # state.json is on disk now (locked_state persisted it on exit): acknowledge the ingested inbox.
    ack_inbox(consumed)


def daemon(interval: float, threshold: float) -> int:
    lock = open(state_dir() / "daemon.lock", "a+")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        print("usage-guard daemon already running")
        return 0
    lock.seek(0)
    lock.truncate()
    # "<pid> <launch token>": the launcher waits for ITS token, so a stale pid left in the file (or a
    # reused pid) can never pass for this launch.
    lock.write(f"{os.getpid()} {os.environ.get('AIO_GUARD_LAUNCH_TOKEN', '')}".strip())
    lock.flush()
    log(f"daemon started (pid {os.getpid()}, every {interval:.0f}s, threshold {threshold:.0f}%)")
    act = Actions(dry_run=False)
    first = True
    while True:
        try:
            cycle(threshold, act, force_scan=first)
        except Exception as exc:  # noqa: BLE001 - the guard must outlive any single bad cycle
            log(f"cycle error: {exc!r}")
        first = False
        time.sleep(interval)


def status() -> int:
    with locked_state() as state:
        pending = [m for m in state["markers"] if m["done"] is None]
        done = [m for m in state["markers"] if m["done"] is not None]
    lockf = state_dir() / "daemon.lock"
    pid = (lockf.read_text().split() or [""])[0] if lockf.exists() else ""
    alive = bool(pid) and Path(f"/proc/{pid}").exists()
    print(f"daemon: {'running pid ' + pid if alive else 'not running'}")
    print(f"pending ({len(pending)}):")
    for m in pending:
        print(f"  {m['kind']} {m['id']} {m['name']!r} until {m['reset_at'] or 'unknown'}")
    print(f"resumed in the last 7 days ({len(done)}):")
    for m in done[-10:]:
        print(f"  {m['kind']} {m['id']} {m['name']!r} at {m['done']}")
    return 0


# ------------------------------------------------------------------------------------ self-test
FAKE_COCKPIT = r'''#!/usr/bin/env python3
import json, os, sys
scen = json.load(open(os.environ["FAKE_SCENARIO"]))
args = sys.argv[1:]
with open(os.environ["FAKE_CALLS"], "a") as f:
    f.write(json.dumps(args) + "\n")
def ok(data): print(json.dumps({"ok": True, "data": data})); sys.exit(0)
if args[:2] == ["usage", "--all"]: ok(scen["usage"])
if args[:3] == ["fleet", "list", "--runs"]: ok({"fleets": [], "runs": scen.get("runs", [])})
if args[:2] == ["fleet", "status"]: ok({"run": scen["fleets"][args[2]]})
if args[:2] == ["task", "get"]: ok(scen["tasks"][args[2]] if scen.get("raw_task") else {"task": scen["tasks"][args[2]]})
if args[:1] == ["display"] and scen.get("display_fails"):
    print(json.dumps({"ok": False, "error": "display failed"})); sys.exit(1)
if args[:1] in (["display"], ["task"], ["fleet"]): ok({})
print(json.dumps({"ok": False, "error": "unexpected " + " ".join(args)})); sys.exit(1)
'''


def self_test() -> int:
    failed = 0

    def check(name: str, cond: bool, detail: str = "") -> None:
        nonlocal failed
        print(("ok   " if cond else "FAIL ") + name + ("" if cond else f" {detail}"))
        failed += 0 if cond else 1

    with tempfile.TemporaryDirectory(prefix="usage-guard-test-") as tmp:
        t = Path(tmp)
        fake = t / "cockpit"
        fake.write_text(FAKE_COCKPIT, encoding="utf-8")
        fake.chmod(0o755)
        calls = t / "calls.jsonl"
        scen_path = t / "scenario.json"
        env_backup = dict(os.environ)
        os.environ.update({"AIO_GUARD_COCKPIT": str(fake), "AIO_GUARD_STATE": str(t / "state"),
                           "FAKE_CALLS": str(calls), "FAKE_SCENARIO": str(scen_path),
                           "AIO_GUARD_NOW": "2026-10-26T12:00:00+00:00"})

        def scenario(**kw):
            base = {"usage": {"claude": {"fiveHour": {"usedPercent": 10, "resetsAt": "2026-10-26T15:00:00Z"},
                                         "sevenDay": {"usedPercent": 10, "resetsAt": "2026-11-01T19:00:00Z"}},
                              "cursor": {"plan": {"usedPercent": 50, "resetsAt": "2026-11-02T16:43:02Z"}}},
                    "runs": [], "fleets": {}, "tasks": {}}
            base.update(kw)
            scen_path.write_text(json.dumps(base), encoding="utf-8")
            calls.write_text("", encoding="utf-8")

        def called() -> list[list[str]]:
            return [json.loads(x) for x in calls.read_text(encoding="utf-8").splitlines() if x]

        def run_main(*argv: str, stdin: str = "") -> int:
            old_argv, old_in = sys.argv, sys.stdin
            sys.argv, sys.stdin = ["usage-guard.py", *argv], io.StringIO(stdin)
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    return main()
            finally:
                sys.argv, sys.stdin = old_argv, old_in

        try:
            # 1. Threshold warning once per window, through main (L19).
            scenario(usage={"claude": {"fiveHour": {"usedPercent": 92, "resetsAt": "2026-10-26T15:00:00Z"}}})
            run_main("once")
            run_main("once")
            notes = [c for c in called() if c[0] == "display"]
            check("warns once at >=90% for a window", len(notes) == 1, str(notes))

            # 2. Hook records only usage-limit waits.
            ev_other = {"task": {"id": "t9", "waitingReason": "question"}}
            check("non-usage wait is ignored", "ignored" in record_event(ev_other))
            ev = {"task": {"id": "t1", "name": "orquestrador", "agentType": "claude",
                           "waitingReason": "usage_limit", "usageLimitResetAt": "2026-10-26T13:00:00Z"}}
            run_main("record", stdin=json.dumps(ev))
            inbox = (Path(os.environ["AIO_GUARD_STATE"]) / "inbox.jsonl").read_text(encoding="utf-8")
            check("record queues the stop without the state lock", '"t1"' in inbox, inbox)
            scenario(tasks={"t1": {"status": "waiting_confirmation", "waitingReason": "usage_limit"}})
            run_main("once")
            with locked_state() as st:
                keys = [m["key"] for m in st["markers"]]
            check("usage-limit wait is recorded through main", any(k.startswith("task:t1:") for k in keys), str(keys))

            # 3. Before the reset nothing happens; after it the task gets a continue prompt.
            scenario(tasks={"t1": {"status": "waiting_confirmation", "waitingReason": "usage_limit"}})
            run_main("once")
            check("no resume before the reset", not [c for c in called() if c[:2] == ["task", "send"]])
            os.environ["AIO_GUARD_NOW"] = "2026-10-26T13:05:00+00:00"
            scenario(tasks={"t1": {"status": "waiting_confirmation", "waitingReason": "usage_limit"}})
            run_main("once")
            sends = [c for c in called() if c[:2] == ["task", "send"]]
            check("task gets one continue prompt after the reset", len(sends) == 1 and sends[0][2] == "t1", str(sends))
            scenario(tasks={"t1": {"status": "waiting_confirmation", "waitingReason": "usage_limit"}})
            run_main("once")
            check("a resumed marker is not resumed twice", not [c for c in called() if c[:2] == ["task", "send"]])

            # 4. Fleet paused by a usage limit is found by the scan and resumed after the reset.
            run_usage = {"status": "paused", "nodes": [
                {"id": "t6", "status": "interrupted",
                 "error": "task usage limit reached until 2026-10-26T13:20:00.1+00:00; retry with another account"}]}
            run_manual = {"status": "paused", "nodes": [{"id": "x", "status": "completed"}]}
            runs = [{"runId": "f1", "fleetName": "wave24", "status": "paused", "interruptedNodeCount": 1},
                    {"runId": "f2", "fleetName": "manual", "status": "paused", "interruptedNodeCount": 0}]
            os.environ["AIO_GUARD_NOW"] = "2026-10-26T13:10:00+00:00"
            with locked_state() as st:
                st["last_scan"] = None
            scenario(runs=runs, fleets={"f1": run_usage, "f2": run_manual})
            run_main("once")
            check("fleet not resumed before its reset", not [c for c in called() if c[:2] == ["fleet", "resume"]])
            os.environ["AIO_GUARD_NOW"] = "2026-10-26T13:30:00+00:00"
            scenario(runs=runs, fleets={"f1": run_usage, "f2": run_manual})
            run_main("once")
            resumes = [c for c in called() if c[:2] == ["fleet", "resume"]]
            check("usage-paused fleet resumed after the reset, manual pause left alone",
                  resumes == [["fleet", "resume", "f1"]], str(resumes))

            # 5. A Run paused by hand that a stale marker points to is never resumed.
            with locked_state() as st:
                add_marker(st, "fleet", "f2", "manual", "", parse_time("2026-10-26T13:00:00Z"), run="f2")
            scenario(runs=[], fleets={"f2": run_manual})
            run_main("once")
            check("manual pause is never resumed", not [c for c in called() if c[:2] == ["fleet", "resume"]])

            # 6. Unknown reset: a task that stops again after a resume is recorded and resumed again.
            os.environ["AIO_GUARD_NOW"] = "2026-10-26T14:00:00+00:00"
            unk = {"task": {"id": "t3", "name": "sem reset", "agentType": "claude", "waitingReason": "usage_limit"}}
            run_main("record", stdin=json.dumps(unk))
            os.environ["AIO_GUARD_NOW"] = "2026-10-26T14:16:00+00:00"
            scenario(tasks={"t3": {"status": "waiting_confirmation", "waitingReason": "usage_limit"}})
            run_main("once")
            first = [c for c in called() if c[:2] == ["task", "send"]]
            os.environ["AIO_GUARD_NOW"] = "2026-10-26T14:40:00+00:00"
            run_main("record", stdin=json.dumps(unk))
            os.environ["AIO_GUARD_NOW"] = "2026-10-26T14:56:00+00:00"
            scenario(tasks={"t3": {"status": "waiting_confirmation", "waitingReason": "usage_limit"}})
            run_main("once")
            second = [c for c in called() if c[:2] == ["task", "send"]]
            check("unknown-reset stop after a resume is resumed again", len(first) == 1 and len(second) == 1,
                  f"{first} {second}")

            # 7. One malformed answer neither blocks later markers nor loses an earlier success.
            with locked_state() as st:
                add_marker(st, "task", "bad", "bad", "claude", parse_time("2026-10-26T14:00:00Z"))
                add_marker(st, "task", "good", "good", "claude", parse_time("2026-10-26T14:00:00Z"))
            scenario(raw_task=True, tasks={"bad": ["not", "a", "mapping"],
                                           "good": {"task": {"status": "waiting_confirmation",
                                                             "waitingReason": "usage_limit"}}})
            run_main("once")
            sends = [c for c in called() if c[:2] == ["task", "send"]]
            with locked_state() as st:
                good_done = [m["done"] for m in st["markers"] if m["id"] == "good"]
            check("malformed data for one marker does not block the next",
                  sends == [["task", "send", "good", "--text", CONTINUE_TEXT]] and good_done and good_done[0],
                  f"{sends} {good_done}")

            # 8. State is persisted even when the body of locked_state raises.
            try:
                with locked_state() as st:
                    st["probe"] = "kept"
                    raise RuntimeError("boom")
            except RuntimeError:
                pass
            with locked_state() as st:
                check("state persisted when the cycle body raises", st.get("probe") == "kept", str(st.get("probe")))

            # 9. Structured failureReason is recognized; a list entry without runId does not break the scan.
            os.environ["AIO_GUARD_NOW"] = "2026-10-27T10:00:00+00:00"
            with locked_state() as st:
                st["last_scan"] = None
            run_struct = {"status": "paused", "nodes": [{"id": "n1", "status": "interrupted",
                                                         "failureReason": "usage_limit", "error": "stopped",
                                                         "usageLimitResetAt": "2026-10-27T09:00:00Z"}]}
            scenario(runs=[{"status": "paused", "interruptedNodeCount": 1},
                           {"runId": "f3", "fleetName": "struct", "status": "paused", "interruptedNodeCount": 1}],
                     fleets={"f3": run_struct})
            run_main("once")
            resumes = [c for c in called() if c[:2] == ["fleet", "resume"]]
            check("failureReason=usage_limit is resumed; an entry without runId is skipped",
                  resumes == [["fleet", "resume", "f3"]], str(resumes))

            # 10. A Run with another interrupted node (user Steer/cancel) is never auto-resumed.
            os.environ["AIO_GUARD_NOW"] = "2026-10-27T12:00:00+00:00"
            with locked_state() as st:
                st["last_scan"] = None
                add_marker(st, "fleet", "f4", "mixed", "", parse_time("2026-10-27T11:00:00Z"), run="f4",
                           seen=parse_time("2026-10-27T11:00:00Z"))
            run_mixed = {"status": "paused", "nodes": [
                {"id": "a", "status": "interrupted", "failureReason": "usage_limit",
                 "usageLimitResetAt": "2026-10-27T11:00:00Z"},
                {"id": "b", "status": "interrupted", "error": "turn canceled by user"}]}
            scenario(runs=[{"runId": "f4", "fleetName": "mixed", "status": "paused", "interruptedNodeCount": 2}],
                     fleets={"f4": run_mixed})
            run_main("once")
            resumes = [c for c in called() if c[:2] == ["fleet", "resume"]]
            notes = [c for c in called() if c[0] == "display" and "outro motivo" in " ".join(c)]
            check("Run with a user-interrupted node is left alone and reported", not resumes and len(notes) == 1,
                  f"{resumes} {notes}")

            # 11. A loop group interrupted because its body was is not "another reason" (live wave 24 case).
            os.environ["AIO_GUARD_NOW"] = "2026-10-27T14:00:00+00:00"
            with locked_state() as st:
                st["last_scan"] = None
            run_loop = {"status": "paused", "nodes": [
                {"id": "review", "status": "interrupted", "error": "loop body was interrupted in iteration 1"},
                {"id": "review#1.fix-pass", "status": "interrupted",
                 "error": "task usage limit reached until 2026-10-27T13:00:00.2+00:00; retry with another account"}]}
            scenario(runs=[{"runId": "f5", "fleetName": "loop", "status": "paused", "interruptedNodeCount": 2}],
                     fleets={"f5": run_loop})
            run_main("once")
            resumes = [c for c in called() if c[:2] == ["fleet", "resume"]]
            check("loop group interrupted by its usage-limited body is resumed", resumes == [["fleet", "resume", "f5"]],
                  str(resumes))

            # 12. The scan records a mixed Run, so the user still gets the manual-review notice.
            with locked_state() as st:
                st["last_scan"] = None
            run_mixed2 = {"status": "paused", "nodes": [
                {"id": "a", "status": "interrupted", "failureReason": "usage_limit",
                 "usageLimitResetAt": "2026-10-27T13:00:00Z"},
                {"id": "b", "status": "interrupted", "error": "turn canceled by user"}]}
            scenario(runs=[{"runId": "f6", "fleetName": "mixed2", "status": "paused", "interruptedNodeCount": 2}],
                     fleets={"f6": run_mixed2})
            run_main("once")
            resumes = [c for c in called() if c[:2] == ["fleet", "resume"]]
            notes = [c for c in called() if c[0] == "display" and "mixed2" in " ".join(c)]
            check("scan-only mixed Run is recorded, not resumed, and reported", not resumes and len(notes) == 1,
                  f"{resumes} {notes}")

            # 13. Several usage-limited nodes: resume only after the latest reset.
            os.environ["AIO_GUARD_NOW"] = "2026-10-28T10:05:00+00:00"
            with locked_state() as st:
                st["last_scan"] = None
            run_two = {"status": "paused", "nodes": [
                {"id": "a", "status": "interrupted", "failureReason": "usage_limit", "usageLimitResetAt": "2026-10-28T10:00:00Z"},
                {"id": "b", "status": "interrupted", "failureReason": "usage_limit", "usageLimitResetAt": "2026-10-28T12:00:00Z"}]}
            with locked_state() as st:
                add_marker(st, "fleet", "f7", "two", "", parse_time("2026-10-28T10:00:00Z"), run="f7")
            scenario(runs=[], fleets={"f7": run_two})
            run_main("once")
            early = [c for c in called() if c[:2] == ["fleet", "resume"]]
            os.environ["AIO_GUARD_NOW"] = "2026-10-28T12:05:00+00:00"
            scenario(runs=[], fleets={"f7": run_two})
            run_main("once")
            late = [c for c in called() if c[:2] == ["fleet", "resume"]]
            check("waits for the latest reset among interrupted nodes",
                  not early and late == [["fleet", "resume", "f7"]], f"{early} {late}")

            # 14. A mixed Run is reported once, not on every scan.
            os.environ["AIO_GUARD_NOW"] = "2026-10-28T13:00:00+00:00"
            run_mix3 = {"status": "paused", "nodes": [
                {"id": "a", "status": "interrupted", "failureReason": "usage_limit"},
                {"id": "b", "status": "interrupted", "error": "turn canceled by user"}]}
            runs3 = [{"runId": "f8", "fleetName": "mix3", "status": "paused", "interruptedNodeCount": 2}]
            notes_total = 0
            for minutes in (0, 20, 40, 60, 80):
                os.environ["AIO_GUARD_NOW"] = (parse_time("2026-10-28T13:00:00Z") + timedelta(minutes=minutes)).isoformat()
                with locked_state() as st:
                    st["last_scan"] = None
                scenario(runs=runs3, fleets={"f8": run_mix3})
                run_main("once")
                notes_total += len([c for c in called() if c[0] == "display" and "mix3" in " ".join(c)])
            check("mixed Run reported once across repeated scans", notes_total == 1, str(notes_total))

            # 15. --dry-run changes no state.
            with locked_state() as st:
                before = json.dumps(st, sort_keys=True)
                add_marker(st, "task", "t9", "dry", "claude", parse_time("2026-10-28T13:00:00Z"))
            with locked_state() as st:
                before = json.dumps(st, sort_keys=True)
            scenario(tasks={"t9": {"status": "waiting_confirmation", "waitingReason": "usage_limit"}})
            run_main("once", "--dry-run")
            with locked_state() as st:
                after = json.dumps(st, sort_keys=True)
            check("dry-run persists nothing (marker stays pending)", before == after)

            # 16. record does not block while the daemon holds the state lock.
            import threading
            ready, release = threading.Event(), threading.Event()

            def hold():
                with locked_state():
                    ready.set()
                    release.wait(10)
            th = threading.Thread(target=hold)
            th.start()
            ready.wait(5)
            t0 = time.monotonic()
            run_main("record", stdin=json.dumps({"task": {"id": "t10", "waitingReason": "usage_limit"}}))
            elapsed = time.monotonic() - t0
            release.set()
            th.join()
            check("record returns while the state lock is held", elapsed < 2, f"{elapsed:.2f}s")

            # 17. A failed manual-review notice is retried, not marked as reported.
            os.environ["AIO_GUARD_NOW"] = "2026-10-29T10:00:00+00:00"
            with locked_state() as st:
                add_marker(st, "fleet", "f9", "mixn", "", parse_time("2026-10-29T09:00:00Z"), run="f9")
            run_mixn = {"status": "paused", "nodes": [
                {"id": "a", "status": "interrupted", "failureReason": "usage_limit"},
                {"id": "b", "status": "interrupted", "error": "turn canceled by user"}]}
            scenario(runs=[], fleets={"f9": run_mixn}, display_fails=True)
            run_main("once")
            with locked_state() as st:
                m9 = [m for m in st["markers"] if m["id"] == "f9"]
            check("failed manual notice leaves the marker pending", m9 and m9[0]["done"] is None
                  and m9[0].get("outcome") is None, str(m9))

            # 18. The inbox survives a failed state write: nothing is lost before the markers are durable.
            run_main("record", stdin=json.dumps({"task": {"id": "t11", "waitingReason": "usage_limit",
                                                          "usageLimitResetAt": "2026-10-30T10:00:00Z"}}))
            real_replace = os.replace

            def failing_replace(src, dst):
                if str(dst).endswith("state.json"):
                    raise OSError("disk full")
                return real_replace(src, dst)
            os.replace = failing_replace
            try:
                try:
                    run_main("once")
                except OSError:
                    pass
            finally:
                os.replace = real_replace
            inbox_text = (Path(os.environ["AIO_GUARD_STATE"]) / "inbox.jsonl").read_text(encoding="utf-8")
            check("inbox kept when state.json could not be written", '"t11"' in inbox_text, inbox_text)
            scenario(tasks={"t11": {"status": "waiting_confirmation", "waitingReason": "usage_limit"}})
            run_main("once")
            with locked_state() as st:
                has = [m for m in st["markers"] if m["id"] == "t11"]
            inbox_after = (Path(os.environ["AIO_GUARD_STATE"]) / "inbox.jsonl").read_text(encoding="utf-8")
            check("inbox acknowledged after a successful write", has and '"t11"' not in inbox_after,
                  f"{has} {inbox_after!r}")

            # 19. A Fleet task without a Run id is left to the scan, not continued as a task.
            run_main("record", stdin=json.dumps({"task": {"id": "node1", "isFleet": True, "waitingReason": "usage_limit",
                                                          "usageLimitResetAt": "2026-10-29T09:00:00Z"}}))
            scenario(tasks={"node1": {"status": "waiting_confirmation", "waitingReason": "usage_limit"}})
            run_main("once")
            with locked_state() as st:
                node_markers = [m for m in st["markers"] if m["id"] == "node1"]
            check("Fleet task without Run id creates no task marker", not node_markers, str(node_markers))

            # 20. A JSON line that is not an object is dropped; later lines are still recorded.
            inbox_p = Path(os.environ["AIO_GUARD_STATE"]) / "inbox.jsonl"
            with open(inbox_p, "a", encoding="utf-8") as f:
                f.write('["not", "an", "object"]\n')
            run_main("record", stdin=json.dumps({"task": {"id": "t12", "waitingReason": "usage_limit",
                                                          "usageLimitResetAt": "2026-10-31T10:00:00Z"}}))
            run_main("once")
            with locked_state() as st:
                t12 = [m for m in st["markers"] if m["id"] == "t12"]
            check("a non-object inbox line is dropped and later stops are recorded",
                  t12 and inbox_p.read_text(encoding="utf-8") == "", f"{t12} {inbox_p.read_text(encoding='utf-8')!r}")

            # 21. A node without a reset time holds the Run even when the other node's reset has passed.
            os.environ["AIO_GUARD_NOW"] = "2026-10-31T10:05:00+00:00"
            with locked_state() as st:
                add_marker(st, "fleet", "f10", "nores", "", parse_time("2026-10-31T10:00:00Z"), run="f10",
                           seen=parse_time("2026-10-31T10:00:00Z"))
            run_nores = {"status": "paused", "nodes": [
                {"id": "a", "status": "interrupted", "failureReason": "usage_limit", "usageLimitResetAt": "2026-10-31T10:00:00Z"},
                {"id": "b", "status": "interrupted", "failureReason": "usage_limit"}]}
            scenario(runs=[], fleets={"f10": run_nores})
            run_main("once")
            early = [c for c in called() if c[:2] == ["fleet", "resume"]]
            os.environ["AIO_GUARD_NOW"] = "2026-10-31T10:20:00+00:00"
            scenario(runs=[], fleets={"f10": run_nores})
            run_main("once")
            late = [c for c in called() if c[:2] == ["fleet", "resume"]]
            check("a node without a reset time delays the resume", not early and late == [["fleet", "resume", "f10"]],
                  f"{early} {late}")

            # 21b. A second node stopping later (no reset) restarts the wait from when it was first seen.
            os.environ["AIO_GUARD_NOW"] = "2026-10-31T11:00:00+00:00"
            with locked_state() as st:
                add_marker(st, "fleet", "f11", "late", "", parse_time("2026-10-31T10:00:00Z"), run="f11",
                           seen=parse_time("2026-10-31T10:00:00Z"))
            run_late = {"status": "paused", "nodes": [
                {"id": "a", "status": "interrupted", "failureReason": "usage_limit", "usageLimitResetAt": "2026-10-31T10:00:00Z"},
                {"id": "b", "status": "interrupted", "failureReason": "usage_limit"}]}
            scenario(runs=[], fleets={"f11": run_late})
            run_main("once")                       # first sees node b at 11:00
            os.environ["AIO_GUARD_NOW"] = "2026-10-31T11:10:00+00:00"
            scenario(runs=[], fleets={"f11": run_late})
            run_main("once")
            early2 = [c for c in called() if c[:2] == ["fleet", "resume"]]
            os.environ["AIO_GUARD_NOW"] = "2026-10-31T11:16:00+00:00"
            scenario(runs=[], fleets={"f11": run_late})
            run_main("once")
            late2 = [c for c in called() if c[:2] == ["fleet", "resume"]]
            check("a later unknown-reset node waits from when it was first seen",
                  not early2 and late2 == [["fleet", "resume", "f11"]], f"{early2} {late2}")

            # 22. The scan's first observation of an unknown-reset node counts, even while another node's
            #     later reset keeps the marker waiting.
            os.environ["AIO_GUARD_NOW"] = "2026-11-01T10:00:00+00:00"
            with locked_state() as st:
                st["last_scan"] = None
            run_obs = {"status": "paused", "nodes": [
                {"id": "a", "status": "interrupted", "failureReason": "usage_limit", "usageLimitResetAt": "2026-11-01T12:00:00Z"},
                {"id": "b", "status": "interrupted", "failureReason": "usage_limit"}]}
            runs_obs = [{"runId": "f12", "fleetName": "obs", "status": "paused", "interruptedNodeCount": 2}]
            scenario(runs=runs_obs, fleets={"f12": run_obs})
            run_main("once")                                  # scan sees b at 10:00
            os.environ["AIO_GUARD_NOW"] = "2026-11-01T12:05:00+00:00"
            scenario(runs=runs_obs, fleets={"f12": run_obs})
            run_main("once")
            resumes = [c for c in called() if c[:2] == ["fleet", "resume"]]
            check("unknown-reset wait counts from the scan's first observation",
                  resumes == [["fleet", "resume", "f12"]], str(resumes))

            # 23. A Run interrupted only by a Cockpit restart is resumed (after the grace period).
            os.environ["AIO_GUARD_NOW"] = "2026-11-02T10:00:00+00:00"
            with locked_state() as st:
                st["last_scan"] = None
            run_rs = {"status": "paused", "nodes": [
                {"id": "t1", "status": "interrupted", "error": "grok task did not survive the restart"},
                {"id": "t2", "status": "interrupted", "error": "grok task did not survive the restart"}]}
            runs_rs = [{"runId": "f13", "fleetName": "restart", "status": "paused", "interruptedNodeCount": 2}]
            scenario(runs=runs_rs, fleets={"f13": run_rs})
            run_main("once")
            early = [c for c in called() if c[:2] == ["fleet", "resume"]]
            os.environ["AIO_GUARD_NOW"] = "2026-11-02T10:02:00+00:00"
            scenario(runs=runs_rs, fleets={"f13": run_rs})
            run_main("once")
            late = [c for c in called() if c[:2] == ["fleet", "resume"]]
            notes = [c for c in called() if c[0] == "display" and "reiniciou" in " ".join(c)]
            check("a restart-only Run is resumed after the grace period",
                  not early and late == [["fleet", "resume", "f13"]] and len(notes) == 1, f"{early} {late} {notes}")

            # 24. Restart + a user-stopped node is still left for a human.
            with locked_state() as st:
                st["last_scan"] = None
            run_rs2 = {"status": "paused", "nodes": [
                {"id": "t1", "status": "interrupted", "error": "grok task did not survive the restart"},
                {"id": "t2", "status": "interrupted", "error": "turn canceled by user"}]}
            os.environ["AIO_GUARD_NOW"] = "2026-11-02T11:00:00+00:00"
            scenario(runs=[{"runId": "f14", "fleetName": "rsmix", "status": "paused", "interruptedNodeCount": 2}],
                     fleets={"f14": run_rs2})
            run_main("once")
            os.environ["AIO_GUARD_NOW"] = "2026-11-02T11:02:00+00:00"
            scenario(runs=[{"runId": "f14", "fleetName": "rsmix", "status": "paused", "interruptedNodeCount": 2}],
                     fleets={"f14": run_rs2})
            run_main("once")
            check("restart + user-stopped node is not resumed",
                  not [c for c in called() if c[:2] == ["fleet", "resume"]])

            # 25. A reset time that jitters by a minute does not repeat the threshold warning.
            os.environ["AIO_GUARD_NOW"] = "2026-11-02T12:00:00+00:00"
            jitter_notes = 0
            for reset in ("2026-11-02T18:30:00Z", "2026-11-02T18:29:59Z", "2026-11-02T18:30:01Z"):
                scenario(usage={"claude": {"fiveHour": {"usedPercent": 97, "resetsAt": reset}}})
                run_main("once")
                jitter_notes += len([c for c in called() if c[0] == "display" and "97%" in " ".join(c)])
            check("jittering reset time warns once", jitter_notes == 1, str(jitter_notes))

            # 26. --dry-run reports without acting.
            with locked_state() as st:
                add_marker(st, "task", "t2", "x", "claude", parse_time("2026-10-26T13:00:00Z"))
            scenario(tasks={"t2": {"status": "waiting_confirmation", "waitingReason": "usage_limit"}})
            run_main("once", "--dry-run")
            check("dry-run does not send", not [c for c in called() if c[:2] == ["task", "send"]])
        finally:
            os.environ.clear()
            os.environ.update(env_backup)

    if failed:
        print(f"{failed} self-test(s) failed")
        return 1
    print("ok   usage-guard self-test")
    return 0


# ------------------------------------------------------------------------------------------ main
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--self-test", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    sub.add_parser("record")
    d = sub.add_parser("daemon")
    d.add_argument("--interval", type=float, default=60)
    d.add_argument("--threshold", type=float, default=90)
    o = sub.add_parser("once")
    o.add_argument("--threshold", type=float, default=90)
    o.add_argument("--dry-run", action="store_true")
    sub.add_parser("status")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if args.cmd == "record":
        try:
            event = json.loads(sys.stdin.read() or "{}")
        except json.JSONDecodeError as exc:
            log(f"record: bad event JSON: {exc}")
            return 1
        record_event(event)
        return 0
    if args.cmd == "daemon":
        return daemon(args.interval, args.threshold)
    if args.cmd == "once":
        cycle(args.threshold, Actions(dry_run=args.dry_run), force_scan=True)
        return 0
    if args.cmd == "status":
        return status()
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
