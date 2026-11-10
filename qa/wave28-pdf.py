#!/usr/bin/env python3
"""Wave 28 D10–D13 judges for PDF font-subset marking.

D10: every /FontFile2 face carries the same six-letter prefix on the Type0
/BaseFont, the CIDFont /BaseFont, and the descriptor /FontName. A face with
no embedded program keeps an unmarked name.
D11: the prefix is stable for the same face name and font program, changes
when the glyph set changes, and is unique inside one PDF.
D12: /CIDSet is written only for pdfa, with bits 0..maxp.numGlyphs-1 set.
D13: pdftotext still returns Latin, Arabic, and CJK; pdffonts reports
emb/sub/uni yes; qpdf --check passes.

Temporary files live in a private mktemp directory. LD_PRELOAD is dropped
before the CLI. Nothing here depends on running as root.
"""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "qa"))

from check_pdf import (  # noqa: E402
    check_expectations,
    font_records,
    pdffonts_report,
    pdftotext_report,
    play,
    qpdf_check,
    run_verapdf,
    subset_prefix,
)

ProgramGroups = dict[str, list[tuple[str, bytes]]]


def paragraph(text: str, **extra: object) -> dict:
    run: dict[str, object] = {"text": text}
    run.update(extra)
    return {"type": "paragraph", "runs": [run]}


HELLO = [paragraph("Hello", font="Liberation Sans")]
HELLO_BANG = [paragraph("Hello!", font="Liberation Sans")]
HELLO_IMAGE = HELLO + [
    {
        "id": "fig1",
        "type": "image",
        "path": "qa/fixtures/wave19-dot.png",
        "width_mm": 12,
        "alt": "dot",
    }
]
TWO_FACES = [
    paragraph("Hello", font="Liberation Sans"),
    paragraph("World", font="Liberation Sans", bold=True),
]
THREE_SCRIPTS = [
    paragraph("Latin tail for contrast."),
    {"type": "paragraph", "dir": "rtl", "lang": "ar", "runs": [{"text": "مرحبا بالعالم"}]},
    {"type": "paragraph", "lang": "ja", "runs": [{"text": "日本語のテキスト"}]},
]


def export_pdf(cli: Path, blocks: list[dict], pdfa: bool, dest: Path) -> None:
    steps = [
        {"tool": "document_new", "args": {"title": "Wave28 PDF", "force": True}},
        {"tool": "insert_blocks", "args": {"after_id": "p1", "blocks": blocks}},
        {"tool": "document_export_pdf", "args": {"path": str(dest), "pdfa": pdfa}},
    ]
    play_path = dest.with_suffix(".play.json")
    play_path.write_text(json.dumps(steps), encoding="utf-8")
    play(cli, play_path)
    if not dest.is_file() or dest.stat().st_size <= 0:
        raise SystemExit(f"export missing: {dest}")


def load_groups(pdf: Path) -> tuple[ProgramGroups, list[str]]:
    records, errors = font_records(pdf)
    groups: ProgramGroups = {}
    for row in records:
        if not row.get("embedded"):
            continue
        names = [str(row.get("type0") or ""), str(row.get("cid") or ""), str(row.get("font_name") or "")]
        if len(set(names)) != 1 or subset_prefix(names[0]) is None:
            errors.append(f"unmarked embedded font {names!r}")
            continue
        groups.setdefault(names[0][7:], []).append((names[0][:6], bytes(row["program"])))
    return groups, errors


def unique_prefix_errors(groups: ProgramGroups) -> list[str]:
    seen: list[str] = []
    for pairs in groups.values():
        for prefix, _program in pairs:
            seen.append(prefix)
    if len(seen) != len(set(seen)):
        return [f"D11 two fonts share a prefix: {seen}"]
    return []


def check_d10(cli: Path) -> list[str]:
    with tempfile.TemporaryDirectory(prefix="wave28-d10-") as tmp:
        pdf = Path(tmp) / "two.pdf"
        export_pdf(cli, TWO_FACES, False, pdf)
        errors = check_expectations(
            pdf,
            {
                "pdffonts": {"all_embedded": True, "all_subset": True},
                "fonts": {"same_name_everywhere": True},
            },
        )
        check = qpdf_check(pdf)
        if check:
            errors.append(check)
        return errors


def check_d11(cli: Path) -> list[str]:
    errors: list[str] = []
    with tempfile.TemporaryDirectory(prefix="wave28-d11-") as tmp:
        root = Path(tmp)
        paths = {
            "a1": root / "a1.pdf",
            "a2": root / "a2.pdf",
            "image": root / "image.pdf",
            "bang": root / "bang.pdf",
            "two": root / "two.pdf",
        }
        export_pdf(cli, HELLO, False, paths["a1"])
        export_pdf(cli, HELLO, False, paths["a2"])
        export_pdf(cli, HELLO_IMAGE, False, paths["image"])
        export_pdf(cli, HELLO_BANG, False, paths["bang"])
        export_pdf(cli, TWO_FACES, False, paths["two"])
        groups: dict[str, ProgramGroups] = {}
        for key, path in paths.items():
            loaded, load_errors = load_groups(path)
            errors.extend(load_errors)
            groups[key] = loaded
            errors.extend(unique_prefix_errors(loaded))
        if errors:
            return errors
        left = groups["a1"]
        right = groups["a2"]
        if set(left) != set(right):
            errors.append(f"D11 repeat export faces differ: {sorted(left)} vs {sorted(right)}")
        for name in left:
            if sorted(left[name]) != sorted(right.get(name, [])):
                errors.append(f"D11 same input changed prefix for {name}")
        shared_program = 0
        image = groups["image"]
        for name in set(left) & set(image):
            by_program = {program: prefix for prefix, program in left[name]}
            for prefix, program in image[name]:
                if program not in by_program:
                    continue
                shared_program += 1
                if by_program[program] != prefix:
                    errors.append(
                        f"D11 same font program got prefixes {by_program[program]} and {prefix} for {name}"
                    )
        if shared_program < 1:
            errors.append("D11 adding an image changed the embedded font program")
        changed = 0
        bang = groups["bang"]
        for name in set(left) & set(bang):
            by_program = {program: prefix for prefix, program in left[name]}
            for prefix, program in bang[name]:
                if program in by_program:
                    if by_program[program] != prefix:
                        errors.append(f"D11 identical program changed prefix for {name}")
                    continue
                changed += 1
                if prefix in by_program.values():
                    errors.append(f"D11 a different glyph set kept prefix {prefix} for {name}")
        if changed < 1:
            errors.append("D11 Hello and Hello! embedded the same font program")
        if len(groups["two"]) < 2 and sum(len(pairs) for pairs in groups["two"].values()) < 2:
            errors.append(f"D11 expected two faces, got {groups['two']!r}")
        return errors


def check_d12(cli: Path) -> list[str]:
    errors: list[str] = []
    with tempfile.TemporaryDirectory(prefix="wave28-d12-") as tmp:
        root = Path(tmp)
        plain = root / "plain.pdf"
        pdfa = root / "pdfa.pdf"
        export_pdf(cli, HELLO, False, plain)
        export_pdf(cli, HELLO, True, pdfa)
        errors.extend(
            check_expectations(
                plain,
                {
                    "pdffonts": {"all_subset": True},
                    "fonts": {"same_name_everywhere": True, "cidset": "absent"},
                },
            )
        )
        errors.extend(
            check_expectations(
                pdfa,
                {
                    "pdffonts": {"all_subset": True},
                    "fonts": {"same_name_everywhere": True, "cidset": "present"},
                },
            )
        )
        for path in (plain, pdfa):
            check = qpdf_check(path)
            if check:
                errors.append(check)
        compliant, failed, report = run_verapdf(pdfa, "1b")
        if compliant is not True:
            rules = ", ".join(failed[:12])
            snippet = report[:4000]
            errors.append(
                f"veraPDF flavour 1b: compliant={compliant}; failed rules: {rules}\n{snippet}"
            )
        return errors


# pdftotext of the three-script fixture before subset marking (poppler in this
# nixpkgs). Arabic is shaped and CJK's first ideograph is extracted as U+2F47.
# D13 requires this string to stay byte-for-byte the same.
D13_TEXT = (
    "\u202b\u202aLatin tail for contrast.\u202c\u202c\n"
    "\u202b\ufee3\ufeae\ufea3\u0645\u0644\u0627\u0639\u0644\u0627\u0628 \u0627\u0628\u062d\u0631\u0645"
    "\u0645\u0644\u0627\u0639\u0644\u0627\u0628 \u0627\u0628\u062d\u0631\u0645 \ufe8e \u0645\u0644\u0627"
    "\u0639\u0644\u0627\u0628 \u0627\u0628\u062d\u0631\u0645 \ufe8e \ufedf\ufecc\ufe8e \ufedf\ufee2\u202c\n"
    "\n"
    "\u202b\u202a\u2f47\u672c\u8a9e\u306e\u30c6\u30ad\u30b9\u30c8\u202c\u202c\n"
    "\x0c"
)


def check_d13(cli: Path) -> list[str]:
    with tempfile.TemporaryDirectory(prefix="wave28-d13-") as tmp:
        pdf = Path(tmp) / "scripts.pdf"
        export_pdf(cli, THREE_SCRIPTS, False, pdf)
        errors = check_expectations(
            pdf,
            {
                "pdffonts": {"all_embedded": True, "all_subset": True},
                "fonts": {"same_name_everywhere": True, "max_program_bytes": 2_000_000},
            },
        )
        rows = pdffonts_report(pdf)
        if not rows:
            errors.append("pdffonts: no fonts listed")
        for row in rows:
            if (
                row.get("emb", "").lower() != "yes"
                or row.get("sub", "").lower() != "yes"
                or row.get("uni", "").lower() != "yes"
            ):
                errors.append(f"pdffonts expected emb/sub/uni yes, got {row}")
        text = pdftotext_report(pdf)
        if text != D13_TEXT:
            errors.append(
                "pdftotext changed: "
                + text.encode("unicode_escape").decode()
            )
        check = qpdf_check(pdf)
        if check:
            errors.append(check)
        return errors


CHECKS = {
    "d10": check_d10,
    "d11": check_d11,
    "d12": check_d12,
    "d13": check_d13,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--check", choices=sorted(CHECKS), required=True)
    args = parser.parse_args()
    cli = args.cli.resolve()
    if not cli.is_file():
        raise SystemExit(f"missing cli: {cli}")
    errors = CHECKS[args.check](cli)
    if errors:
        for err in errors:
            print(f"FAIL {args.check}: {err}")
        return 1
    print(f"ok   wave28 {args.check}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
