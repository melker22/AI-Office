#!/usr/bin/env bash
# Prepares the data directory of the qa/audit reproduction playbooks (default /tmp/aio-audit, the
# path the playbooks name; scripts/check-audit-playbooks.sh passes a private mktemp directory).
set -euo pipefail
cd "$(dirname "$0")/../.."
d=${1:-/tmp/aio-audit}
rm -rf "$d"
mkdir -m 700 "$d"   # fails if someone else re-created it in between; never reuse a foreign directory
for ext in docx odt rtf; do ln -s /dev/full "$d/fail.$ext"; done   # opens fine, every write fails
cp qa/audit/fixtures/include-source.aio.json "$d/include-source.aio.json"
