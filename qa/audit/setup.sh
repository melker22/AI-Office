#!/usr/bin/env bash
# Prepares /tmp/aio-audit for the qa/audit reproduction playbooks.
set -euo pipefail
cd "$(dirname "$0")/../.."
d=/tmp/aio-audit
rm -rf "$d" && mkdir -p "$d"
for ext in docx odt rtf; do ln -s /dev/full "$d/fail.$ext"; done   # write succeeds to open, fails to write
cp qa/audit/fixtures/include-source.aio.json "$d/include-source.aio.json"
