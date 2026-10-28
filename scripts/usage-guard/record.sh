#!/usr/bin/env bash
# task.waiting hook action: record a usage-limit stop. Loads the environment the installer chose
# (AIO_GUARD_STATE, AIO_GUARD_COCKPIT) so the hook writes to the same inbox the daemon reads.
D="$HOME/.local/share/aio-usage-guard"
if [ -f "$D/env" ]; then set -a; . "$D/env"; set +a; fi
py="$D/run-python"; [ -x "$py" ] || py="$(command -v python3)"
exec "$py" "$D/usage-guard.py" record
