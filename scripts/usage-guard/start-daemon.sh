#!/usr/bin/env bash
# Ensures the usage-guard daemon is running. It runs as a systemd user service, outside Cockpit's
# process group, so a Cockpit restart cannot take it down. Fallback: a detached process.
if command -v systemctl >/dev/null 2>&1 && systemctl --user cat aio-usage-guard.service >/dev/null 2>&1; then
  systemctl --user start aio-usage-guard.service
  exit 0
fi
D="$HOME/.local/share/aio-usage-guard"
setsid nohup python3 "$D/usage-guard.py" daemon >>"$HOME/.local/state/aio-usage-guard/daemon.out" 2>&1 < /dev/null &
exit 0
