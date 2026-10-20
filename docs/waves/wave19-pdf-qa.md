# Wave 19 — PDF QA harness (D1)

Executor node `t3-pdf-qa` adds the compliance gate **before** T1/T2 land export fixes. Until then, `ctest -L pdf` is expected red; `pdf_check_selftest` and `scripts/qa-all.sh --gui` stay green.

## Tools (pinned `shell.nix`)

- **veraPDF** — reference judge (`--flavour 1b`, `ua1`; XML/MRR `isCompliant` + failed rules)
- **qpdf** — `qpdf --json=1` struct tree (`/Table`, `/Lang`, no fallback `/Part`, reading order)
- **poppler** — `pdffonts` (`emb yes` for every face)

## Runner

```bash
python3 qa/check_pdf.py --self-test
python3 qa/check_pdf.py --cli build/linux/aioffice-cli --playbook qa/wave19-pdf-pdfa.json
```

Expectations live beside the playbook: `qa/wave19-pdf-*.pdf.json` (field `pdf` path, optional `verapdf`, `qpdf`, `pdffonts`, `export_warnings`).

Subprocess timeouts: play 300s, veraPDF 120s, qpdf/pdffonts 60s (L1/L10).

## CTest / CI

- Label **`pdf`**: every `qa/wave19-pdf-*.json` + `check_pdf.py`
- Label **`playbook`**: includes `pdf_check_selftest`
- `scripts/qa-all.sh --pdf` adds the `pdf` label to the ctest filter (combinable with `--gui` / `--interop`)
- CircleCI: `ctest -L 'playbook|roundtrip|pdf'`

## Suite map (critério de pronto)

| Playbook | Covers |
|----------|--------|
| `wave19-pdf-pdfa.json` | veraPDF PDF/A-1b |
| `wave19-pdf-ua.json` | PDF/UA-1, table, list, figure+alt, note, TOC, header/footer, span lang |
| `wave19-pdf-reading-order.json` | `reading_order` + frame (`.aio.json`) |
| `wave19-pdf-fonts-ar-cjk.json` | Arabic/CJK embedded fonts |
| `wave19-pdf-watermark-pdfa.json` | Translucent watermark + PDF/A + `warnings[]` |
