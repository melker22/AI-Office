#!/usr/bin/env bash
# Ensures the usage-guard daemon is running. Preferred: the systemd user service (outside Cockpit's
# process group, so a Cockpit restart cannot take it down). If that unit is missing or fails to start,
# fall back to a detached process; the daemon's own flock keeps a single instance either way.
D="$HOME/.local/share/aio-usage-guard"
if command -v systemctl >/dev/null 2>&1 && systemctl --user cat aio-usage-guard.service >/dev/null 2>&1; then
  if systemctl --user start aio-usage-guard.service; then
    exit 0
  fi
  echo "usage-guard: systemctl --user start failed; starting a detached daemon instead" >&2
fi
mkdir -p "$HOME/.local/state/aio-usage-guard"
py="$D/run-python"; [ -x "$py" ] || py="$(command -v python3)"
setsid nohup "$py" "$D/usage-guard.py" daemon >>"$HOME/.local/state/aio-usage-guard/daemon.out" 2>&1 < /dev/null &
exit 0
