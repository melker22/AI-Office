#!/usr/bin/env bash
# Local mirror of the CircleCI gate: build aioffice-cli and run CTest playbooks +
# roundtrip fixtures. Exit code is the whole contract (Cockpit Fleet command gates).
#
#   scripts/qa-all.sh                 # cli target + ctest (playbook|roundtrip)
#   scripts/qa-all.sh --gui           # also compile the full GUI target
#   scripts/qa-all.sh --interop       # LibreOffice round-trip when soffice exists
#   scripts/qa-all.sh --pdf           # also ctest label pdf (wave 19 veraPDF suite)
#   scripts/qa-all.sh --formats       # also ctest labels epub and rtf (wave 20)
#   scripts/qa-all.sh --gui --interop # both; flag order does not matter
set -uo pipefail
cd "$(dirname "$0")/.."

gui=0
interop=0
pdf=0
formats=0
for arg in "$@"; do
  case "$arg" in
    --gui) gui=1 ;;
    --interop) interop=1 ;;
    --pdf) pdf=1 ;;
    --formats) formats=1 ;;
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

ctest_labels='playbook|roundtrip|wave24'
if [ "$pdf" = 1 ]; then
  ctest_labels="${ctest_labels}|pdf"
fi
if [ "$formats" = 1 ]; then
  ctest_labels="${ctest_labels}|epub|rtf"
fi

status=0
# Wave 24 fidelity judges (D1-D17) landed with T1-T4; run them with the rest (label wave24).
if ! run "ctest --test-dir build/linux --output-on-failure --no-tests=error -L '${ctest_labels}'"; then
  status=1
fi

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
