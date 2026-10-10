#!/usr/bin/env bash
# Local mirror of the CircleCI gate: build aioffice-cli and run CTest playbooks +
# roundtrip fixtures. Exit code is the whole contract (Cockpit Fleet command gates).
#
#   scripts/qa-all.sh          # cli target + ctest (playbook|roundtrip)
#   scripts/qa-all.sh --gui    # also compile the full GUI target
set -uo pipefail
cd "$(dirname "$0")/.."

gui=0
[ "${1:-}" = "--gui" ] && gui=1

export AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen

run() {
  if [ -n "${IN_NIX_SHELL:-}" ]; then bash -c "$1"; else nix-shell --run "$1"; fi
}

run "cmake --preset linux >/dev/null && cmake --build --preset linux --target aioffice_cli" || { echo "BUILD FAILED: aioffice_cli"; exit 1; }
if [ "$gui" = 1 ]; then
  run "cmake --build --preset linux --target aioffice" || { echo "BUILD FAILED: aioffice (GUI)"; exit 1; }
fi

status=0
if ! run "ctest --test-dir build/linux --output-on-failure -L 'playbook|roundtrip'"; then
  status=1
fi
exit $status
