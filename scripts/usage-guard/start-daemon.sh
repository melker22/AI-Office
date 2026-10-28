#!/usr/bin/env bash
# Ensures the usage-guard daemon is running. Preferred: the systemd user service (outside Cockpit's
# process group, so a Cockpit restart cannot take it down). If that unit is missing or fails to start,
# fall back to a detached process and confirm it is up (exit 1 if it is not).
D="$HOME/.local/share/aio-usage-guard"
# Same resolution as state_dir() in usage-guard.py.
S="${AIO_GUARD_STATE:-${XDG_STATE_HOME:-$HOME/.local/state}/aio-usage-guard}"
if command -v systemctl >/dev/null 2>&1 && systemctl --user cat aio-usage-guard.service >/dev/null 2>&1; then
  if systemctl --user start aio-usage-guard.service; then
    exit 0
  fi
  echo "usage-guard: systemctl --user start failed; starting a detached daemon instead" >&2
fi
# The installer records which Cockpit CLI to use; the systemd unit carries the same value.
if [ -f "$D/env" ]; then set -a; . "$D/env"; set +a; fi
py="$D/run-python"; [ -x "$py" ] || py="$(command -v python3 || true)"
[ -n "$py" ] || { echo "usage-guard: no python3 to run the daemon" >&2; exit 1; }
[ -f "$D/usage-guard.py" ] || { echo "usage-guard: $D/usage-guard.py is missing" >&2; exit 1; }
command -v setsid >/dev/null 2>&1 || { echo "usage-guard: setsid not found" >&2; exit 1; }
mkdir -p "$S" || exit 1
setsid nohup "$py" "$D/usage-guard.py" daemon >>"$S/daemon.out" 2>&1 < /dev/null &
# Readiness: the daemon writes its pid into daemon.lock (or an existing one already holds it).
for _ in $(seq 1 20); do
  pid="$(cat "$S/daemon.lock" 2>/dev/null || true)"
  if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
    exit 0
  fi
  sleep 0.25
done
echo "usage-guard: detached daemon did not come up; see $S/daemon.out" >&2
exit 1
