#!/usr/bin/env bash
# Local mirror of the CircleCI gate: build aioffice-cli and run every
# qa/wave*.json playbook headlessly. Exit code is the whole contract
# (used by Cockpit Fleet command gates).
#
#   scripts/qa-all.sh                 # cli target + playbooks
#   scripts/qa-all.sh --gui           # also compile the full GUI target
#   scripts/qa-all.sh --interop       # LibreOffice round-trip when soffice exists
#   scripts/qa-all.sh --gui --interop # both; flag order does not matter
set -uo pipefail
cd "$(dirname "$0")/.."

gui=0
interop=0
for arg in "$@"; do
  case "$arg" in
    --gui) gui=1 ;;
    --interop) interop=1 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

export AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen

run() {
  if [ -n "${IN_NIX_SHELL:-}" ]; then bash -c "$1"; else nix-shell --run "$1"; fi
}

run "cmake --preset linux >/dev/null && cmake --build --preset linux --target aioffice_cli" || { echo "BUILD FAILED: aioffice_cli"; exit 1; }
if [ "$gui" = 1 ]; then
  run "cmake --build --preset linux --target aioffice" || { echo "BUILD FAILED: aioffice (GUI)"; exit 1; }
fi

status=0
out=$(mktemp)
for f in qa/wave*.json; do
  case "$f" in
    qa/wave1-foundation.json) continue ;;
    qa/*.aio.json) continue ;;
  esac
  if run "./build/linux/aioffice-cli play $f" >"$out" 2>&1; then
    echo "ok   $f"
  else
    echo "FAIL $f"
    tail -n 40 "$out"
    status=1
  fi
done
rm -f "$out"
run "./build/linux/aioffice-cli tools" >/dev/null || { echo "FAIL tools catalog"; status=1; }

if [ "$interop" = 1 ]; then
  if ! command -v soffice >/dev/null 2>&1; then
    echo "skip interop"
  else
    if run "python3 scripts/qa-interop.py ./build/linux/aioffice-cli"; then
      echo "ok   interop"
    else
      echo "FAIL interop"
      status=1
    fi
  fi
fi
exit $status
