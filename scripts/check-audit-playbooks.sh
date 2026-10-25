#!/usr/bin/env bash
# Every qa/audit reproduction playbook must FAIL today on an `expect` mismatch: it asserts the correct
# behaviour of a defect that is still open. A playbook that passes, a missing CLI, a malformed playbook
# or a crash is a gate failure (CodeRabbit on PR #12).
#   scripts/check-audit-playbooks.sh [cli] [dir]
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
cli=${1:-./build/linux/aioffice-cli}
dir=${2:-qa/audit}
if [ ! -x "$cli" ]; then echo "FAIL aioffice-cli not found or not executable: $cli"; exit 1; fi
export AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
# The playbooks use fixed /tmp/aio-audit paths: one checker at a time (setup + every playbook).
exec 9>/tmp/aio-audit.lock
flock 9 || { echo "FAIL could not take /tmp/aio-audit.lock"; exit 1; }
if [ -x "$dir/setup.sh" ]; then "$dir/setup.sh" || { echo "FAIL $dir/setup.sh"; exit 1; }; fi
shopt -s nullglob
files=("$dir"/*.json)
if [ "${#files[@]}" -eq 0 ]; then echo "FAIL no playbooks in $dir (an empty set proves nothing)"; exit 1; fi
status=0
for f in "${files[@]}"; do
  out=$("$cli" play "$f" 2>&1); rc=$?
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
