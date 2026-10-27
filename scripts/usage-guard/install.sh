#!/usr/bin/env bash
# Installs usage-guard for the current user: script copy, systemd user service (Restart=always, outside
# Cockpit's process group so a Cockpit restart cannot kill it), and the two Cockpit hooks.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
D="$HOME/.local/share/aio-usage-guard"
mkdir -p "$D" "$HOME/.local/state/aio-usage-guard" "$HOME/.config/systemd/user"
install -m 0755 "$here/../usage-guard.py" "$D/usage-guard.py"
install -m 0755 "$here/start-daemon.sh" "$D/start-daemon.sh"
install -m 0644 "$here/aio-usage-guard.service" "$HOME/.config/systemd/user/aio-usage-guard.service"
if command -v systemctl >/dev/null 2>&1; then
  systemctl --user daemon-reload
  systemctl --user enable --now aio-usage-guard.service
  systemctl --user restart aio-usage-guard.service
fi
C="${AIO_GUARD_COCKPIT:-$HOME/.agi-tools/bin/cockpit}"
hooks="$("$C" hooks list)"
if ! grep -q '"usage-guard-record"' <<<"$hooks"; then
  "$C" hooks add --name usage-guard-record --event task.waiting --waiting-reason usage_limit --timeout 30 \
    --run 'python3 "$HOME/.local/share/aio-usage-guard/usage-guard.py" record' >/dev/null
fi
if ! grep -q '"usage-guard-daemon"' <<<"$hooks"; then
  "$C" hooks add --name usage-guard-daemon --event app.ready --timeout 20 \
    --run '"$HOME/.local/share/aio-usage-guard/start-daemon.sh"' >/dev/null
fi
python3 "$D/usage-guard.py" status
