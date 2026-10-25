#!/usr/bin/env bash
# Every qa/audit reproduction playbook must FAIL today on an `expect` mismatch: it asserts the correct
# behaviour of a defect that is still open. A playbook that passes, a missing CLI, a malformed playbook,
# a crash or an empty playbook set is a gate failure (CodeRabbit on PR #12).
#   scripts/check-audit-playbooks.sh [cli] [dir]
# Each run works in its own private directory (mktemp -d): the playbooks' /tmp/aio-audit paths are
# rewritten to it, so concurrent runs share nothing and no lock is needed.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
cli=${1:-./build/linux/aioffice-cli}
dir=${2:-qa/audit}
if [ ! -x "$cli" ]; then echo "FAIL aioffice-cli not found or not executable: $cli"; exit 1; fi
export AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
shopt -s nullglob
files=("$dir"/*.json)
if [ "${#files[@]}" -eq 0 ]; then echo "FAIL no playbooks in $dir (an empty set proves nothing)"; exit 1; fi
work=$(mktemp -d) || { echo "FAIL mktemp"; exit 1; }
trap 'rm -rf "$work"' EXIT
if [ -e "$dir/setup.sh" ]; then
  [ -x "$dir/setup.sh" ] || { echo "FAIL $dir/setup.sh exists but is not executable"; exit 1; }
  "$dir/setup.sh" "$work/data" || { echo "FAIL $dir/setup.sh"; exit 1; }
fi
status=0
for f in "${files[@]}"; do
  pb="$work/$(basename "$f")"
  sed "s#/tmp/aio-audit#$work/data#g" "$f" > "$pb"
  out=$("$cli" play "$pb" 2>&1); rc=$?
  if [ "$rc" -eq 0 ]; then
    echo "FAIL passes today (asserts nothing or the defect is fixed): $f"; status=1
  elif ! grep -q "expect failed" <<<"$out"; then
    echo "FAIL fails for another reason (exit $rc, no 'expect failed'): $f"; echo "$out" | tail -3; status=1
  else
    echo "ok   reproduces: $f"
  fi
done
echo "${#files[@]} audit playbook(s) checked"
exit $status
