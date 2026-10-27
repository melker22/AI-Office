#!/usr/bin/env bash
# Installs usage-guard for the current user: script copy, systemd user service when a user manager is
# available (else a detached daemon), and the two Cockpit hooks.
set -euo pipefail
USER="${USER:-$(id -un)}"
here="$(cd "$(dirname "$0")" && pwd)"
D="$HOME/.local/share/aio-usage-guard"
mkdir -p "$D" "$HOME/.local/state/aio-usage-guard"
install -m 0755 "$here/../usage-guard.py" "$D/usage-guard.py"
install -m 0755 "$here/start-daemon.sh" "$D/start-daemon.sh"

# Same lookup order as cockpit_bin() in usage-guard.py.
C="${AIO_GUARD_COCKPIT:-}"
[ -z "$C" ] && [ -x "$HOME/.agi-tools/bin/cockpit" ] && C="$HOME/.agi-tools/bin/cockpit"
[ -z "$C" ] && C="$(command -v cockpit || true)"
[ -n "$C" ] || { echo "install: Cockpit CLI not found (set AIO_GUARD_COCKPIT)" >&2; exit 1; }

# Prefer a stable profile python3 over a transient one (a nix-shell store path can be garbage-collected).
python=""
for cand in "/etc/profiles/per-user/$USER/bin/python3" "$HOME/.nix-profile/bin/python3" "$(command -v python3 || true)"; do
  [ -n "$cand" ] && [ -x "$cand" ] && { python="$cand"; break; }
done
[ -n "$python" ] || { echo "install: python3 not found" >&2; exit 1; }
# PATH for the service: python3, node (the Cockpit CLI runs on it), the CLI, bash/env (the CLI's shebang),
# the NixOS system profile when present, then the usual dirs.
svc_path=""
add_dir() {  # always returns 0: a missing directory is simply skipped (set -e)
  if [ -n "$1" ] && [ -d "$1" ]; then
    case ":$svc_path:" in *":$1:"*) ;; *) svc_path="${svc_path:+$svc_path:}$1";; esac
  fi
  return 0
}
for b in "$python" "$(command -v node || true)" "$C" "$(command -v bash || true)" "$(command -v env || true)"; do
  if [ -n "$b" ]; then add_dir "$(dirname "$b")"; fi
done
for d in "/etc/profiles/per-user/$USER/bin" "$HOME/.nix-profile/bin" /run/current-system/sw/bin /usr/local/bin /usr/bin /bin; do
  add_dir "$d"
done

service_ok=0
if command -v systemctl >/dev/null 2>&1 && systemctl --user show-environment >/dev/null 2>&1; then
  mkdir -p "$HOME/.config/systemd/user"
  sed -e "s#@PYTHON@#$python#" -e "s#@PATH@#$svc_path#" "$here/aio-usage-guard.service" \
    > "$HOME/.config/systemd/user/aio-usage-guard.service"
  if systemctl --user daemon-reload && systemctl --user enable aio-usage-guard.service \
     && systemctl --user restart aio-usage-guard.service; then
    service_ok=1
  fi
fi
if [ "$service_ok" = 0 ]; then
  echo "install: no usable systemd user manager; using a detached daemon (restarted by the app.ready hook)" >&2
  "$D/start-daemon.sh"
fi

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
