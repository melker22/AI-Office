#!/usr/bin/env bash
# Private fixture dir for qa/wave23-io-* (CTest FIXTURES_SETUP). Not /tmp/aio-audit.
set -euo pipefail
d=${1:?fixture directory}
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
rm -rf "$d"
mkdir -m 700 "$d"
for name in fail.aio.json fail.docx fail.odt fail.rtf fail.epub fail.md fail.txt fail.html fail.pdf fail.doc; do
  ln -s /dev/full "$d/$name"
done
mkfifo -m 600 "$d/image.fifo"
mkfifo -m 600 "$d/export.pdf.fifo"
python3 "$root/qa/wave23-io-fixtures.py" "$d"
