#!/usr/bin/env bash
# Ensures the usage-guard daemon is running. Preferred: the systemd user service (outside Cockpit's
# process group, so a Cockpit restart cannot take it down), confirmed ready. Otherwise a detached daemon,
# confirmed by its launch token. Exit 1 if no guard daemon ends up running.
D="$HOME/.local/share/aio-usage-guard"
# The installer records which Cockpit CLI and state dir to use (the systemd unit carries the same values);
# load them BEFORE resolving the state dir.
if [ -f "$D/env" ]; then set -a; . "$D/env"; set +a; fi
# Same resolution as state_dir() in usage-guard.py.
S="${AIO_GUARD_STATE:-${XDG_STATE_HOME:-$HOME/.local/state}/aio-usage-guard}"

lock_pid() { awk '{print $1; exit}' "$S/daemon.lock" 2>/dev/null; }
is_guard() {  # pid is alive and is the usage-guard daemon (not a reused pid)
  [ -n "$1" ] && kill -0 "$1" 2>/dev/null \
    && tr '\0' ' ' < "/proc/$1/cmdline" 2>/dev/null | grep -q 'usage-guard.py daemon'
}

if command -v systemctl >/dev/null 2>&1 && systemctl --user cat aio-usage-guard.service >/dev/null 2>&1; then
  if systemctl --user start aio-usage-guard.service; then
    # Type=simple returns before the daemon runs: wait until the service is active AND its main pid owns
    # the lock.
    for _ in $(seq 1 40); do
      main="$(systemctl --user show -p MainPID --value aio-usage-guard.service 2>/dev/null || true)"
      if systemctl --user is-active --quiet aio-usage-guard.service && [ -n "$main" ] && [ "$main" != 0 ] \
         && [ "$(lock_pid)" = "$main" ] && is_guard "$main"; then
        exit 0
      fi
      sleep 0.25
    done
    echo "usage-guard: systemd service did not become ready; starting a detached daemon instead" >&2
  else
    echo "usage-guard: systemctl --user start failed; starting a detached daemon instead" >&2
  fi
fi

py="$D/run-python"; [ -x "$py" ] || py="$(command -v python3 || true)"
[ -n "$py" ] || { echo "usage-guard: no python3 to run the daemon" >&2; exit 1; }
[ -f "$D/usage-guard.py" ] || { echo "usage-guard: $D/usage-guard.py is missing" >&2; exit 1; }
command -v setsid >/dev/null 2>&1 || { echo "usage-guard: setsid not found" >&2; exit 1; }
mkdir -p "$S" || exit 1
token="$$-$(date +%s%N)-$RANDOM"
AIO_GUARD_LAUNCH_TOKEN="$token" setsid nohup "$py" "$D/usage-guard.py" daemon >>"$S/daemon.out" 2>&1 < /dev/null &
child=$!
# Ready = the lock carries THIS launch's token, or our child found another guard daemon already holding
# the lock (it exits with "already running") and that daemon is verifiably the guard.
for _ in $(seq 1 40); do
  if grep -q -- " $token\$" "$S/daemon.lock" 2>/dev/null && is_guard "$(lock_pid)"; then
    exit 0
  fi
  if ! kill -0 "$child" 2>/dev/null && is_guard "$(lock_pid)"; then
    exit 0
  fi
  sleep 0.25
done
echo "usage-guard: detached daemon did not come up; see $S/daemon.out" >&2
exit 1
