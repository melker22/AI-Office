#!/usr/bin/env bash
# Fleet command-gate helper: proves that every upstream executor node's reported commit
# is really part of the branch being tested (HEAD of the gate's working directory).
#
# Cockpit passes each upstream node's validated output_contract JSON as
# FLEET_NEEDS_<ID>_OUTPUT. Every executor node in our Fleets reports {"commit": "<40-hex sha>", ...}.
# This catches the two wave-17 failure modes:
#   - an executor that crashed and reported nothing (it never gets here: its output_contract fails
#     the node), or reported a commit that does not exist;
#   - an integrate node that merged a stale tip of a branch instead of the reported commit.
#
#   scripts/fleet-verify-merged.sh <min-count>
#     <min-count>: how many executor commits this gate must see (fails if fewer were reported).
#   scripts/fleet-verify-merged.sh --self-test
set -uo pipefail
if [[ "${1:-}" == "--self-test" ]]; then
  cd "$(dirname "$0")/.." || exit 2
  head_sha=$(git rev-parse HEAD)
  bogus="0000000000000000000000000000000000000001"
  rm -f /tmp/fleet-verify-self-pass.txt /tmp/fleet-verify-self-fail.txt
  pass_out=$(mktemp)
  fail_out=$(mktemp)
  trap 'rm -f "$pass_out" "$fail_out"' EXIT
  export FLEET_NEEDS_DEMO_OUTPUT=$(
    cat <<EOF
{
  "commit": "${head_sha}",
  "qa_green": true,
  "summary": "self-test pass"
}
EOF
  )
  if ! scripts/fleet-verify-merged.sh 1 >"$pass_out" 2>&1; then
    echo "FAIL self-test pass case (HEAD commit should verify)"
    cat "$pass_out"
    exit 1
  fi
  echo "ok   self-test pass case (multiline JSON commit in HEAD)"
  export FLEET_NEEDS_DEMO_OUTPUT='{"commit":"'"${bogus}"'","qa_green":true}'
  if scripts/fleet-verify-merged.sh 1 >"$fail_out" 2>&1; then
    echo "FAIL self-test fail case (bogus commit should fail)"
    cat "$fail_out"
    exit 1
  fi
  echo "ok   self-test fail case (missing commit rejected)"
  if [[ -e /tmp/fleet-verify-self-pass.txt || -e /tmp/fleet-verify-self-fail.txt ]]; then
    echo "FAIL self-test recreated fixed /tmp paths"
    exit 1
  fi
  exit 0
fi
min=${1:-1}
if [[ ! $min =~ ^[0-9]+$ ]] || (( ${#min} > 9 )) || (( min < 1 )); then
  echo "FAIL min-count must be a positive integer, got '$min'" >&2
  exit 2
fi
cd "$(dirname "$0")/.." || exit 2

seen=0
status=0
while read -r name; do
  case "$name" in FLEET_NEEDS_*_OUTPUT) ;; *) continue ;; esac
  value=${!name}
  sha=$(printf '%s' "$value" | python3 -c 'import json,sys
try:
    d = json.load(sys.stdin)
except Exception:
    sys.exit(0)
c = d.get("commit") if isinstance(d, dict) else None
print(c or "")')
  [ -z "$sha" ] && continue
  seen=$((seen + 1))
  node=${name#FLEET_NEEDS_}; node=${node%_OUTPUT}
  if ! git cat-file -e "${sha}^{commit}" 2>/dev/null; then
    echo "FAIL $node: reported commit $sha does not exist"
    status=1
  elif ! git merge-base --is-ancestor "$sha" HEAD; then
    echo "FAIL $node: reported commit $sha is not in HEAD ($(git rev-parse --short HEAD)) — stale or missing merge"
    status=1
  else
    echo "ok   $node: $sha is in HEAD"
  fi
done < <(compgen -e)

if [ "$seen" -lt "$min" ]; then
  echo "FAIL expected at least $min executor commit(s), saw $seen"
  status=1
fi
exit $status
