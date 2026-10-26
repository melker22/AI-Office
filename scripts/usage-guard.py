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
def locked_state():
    """Read-modify-write of state.json under an exclusive lock (hook and daemon run concurrently)."""
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
               run: str | None = None) -> bool:
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
                             "agent": agent, "reset_at": iso(reset_at), "created": iso(now_utc()),
                             "done": None})
    return True


def record_event(event: dict) -> str:
    """Hook entry: a task entered waiting_confirmation. Only usage-limit waits are recorded."""
    task = event.get("task") or {}
    if task.get("waitingReason") != "usage_limit":
        return "ignored (not a usage limit)"
    reset_at = parse_time(task.get("usageLimitResetAt"))
    with locked_state() as state:
        if task.get("isFleet") and task.get("fleetRunId"):
            added = add_marker(state, "fleet", task["fleetRunId"], task.get("name") or "",
                               task.get("agentType") or "", reset_at, run=task["fleetRunId"])
        else:
            added = add_marker(state, "task", task.get("id") or "", task.get("name") or "",
                               task.get("agentType") or "", reset_at)
    what = f"{'fleet ' + task.get('fleetRunId', '') if task.get('isFleet') else 'task ' + str(task.get('id'))}"
    msg = f"recorded {what} until {iso(reset_at) or 'unknown'}" if added else f"already recorded {what}"
    log(msg)
    return msg


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
        if pct >= threshold and key not in state["warned"]:
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
        if m or "usage limit" in err.lower():
            found.append((n.get("id") or "?", parse_time(m.group(1)) if m else None))
    return found


def scan_paused_fleets(state: dict) -> None:
    """Fallback: Runs paused by a usage limit that no hook recorded (e.g. before the guard ran)."""
    data = cockpit("fleet", "list", "--runs")
    runs = data.get("runs") if isinstance(data, dict) else None
    for r in runs if isinstance(runs, list) else []:
        if not isinstance(r, dict) or r.get("status") != "paused" or not r.get("interruptedNodeCount"):
            continue
        st = cockpit("fleet", "status", r["runId"])
        run = st.get("run") if isinstance(st, dict) else None
        nodes = usage_interrupted_nodes(run) if isinstance(run, dict) else []
        if nodes:
            reset = max((t for _, t in nodes if t), default=None)
            if add_marker(state, "fleet", r["runId"], r.get("fleetName") or "", "", reset, run=r["runId"]):
                log(f"scan: fleet {r['runId']} paused by a usage limit until {iso(reset) or 'unknown'}")


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
        if not usage_interrupted_nodes(run):
            log(f"fleet {marker['run']} is paused, but not by a usage limit: left alone")
            return True
        if act.resume_fleet(marker["run"]):
            act.notify(f"Limite renovou: retomei a Fleet {marker['name'] or marker['run']} de onde parou.")
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
    with locked_state() as state:
        warn_thresholds(state, usage, threshold, act)
        last = parse_time(state.get("last_scan"))
        if force_scan or last is None or now - last >= SCAN_EVERY:
            scan_paused_fleets(state)
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


def daemon(interval: float, threshold: float) -> int:
    lock = open(state_dir() / "daemon.lock", "a+")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        print("usage-guard daemon already running")
        return 0
    lock.seek(0)
    lock.truncate()
    lock.write(str(os.getpid()))
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
    pid = lockf.read_text().strip() if lockf.exists() else ""
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

            # 9. --dry-run reports without acting.
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
