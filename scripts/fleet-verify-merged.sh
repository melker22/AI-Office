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
set -uo pipefail
min=${1:-1}
if [[ ! $min =~ ^[0-9]+$ ]] || (( ${#min} > 9 )) || (( min < 1 )); then
  echo "FAIL min-count must be a positive integer, got '$min'" >&2
  exit 2
fi
cd "$(dirname "$0")/.." || exit 2

seen=0
status=0
while IFS='=' read -r name value; do
  case "$name" in FLEET_NEEDS_*_OUTPUT) ;; *) continue ;; esac
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
done < <(env)

if [ "$seen" -lt "$min" ]; then
  echo "FAIL expected at least $min executor commit(s), saw $seen"
  status=1
fi
exit $status
