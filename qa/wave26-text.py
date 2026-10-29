#!/usr/bin/env python3
"""Wave 26 D1–D3 judges for blockTextLayout (text, formats, hyphenation).

D1: a chapter prefix shifts later format ranges, and note_restart paints the
page ordinal in the marker (not only in the format-range length).
D2: each run is hyphenated once with its own language.
D3: a 100_000-character paragraph previews in under 10 seconds.

PNG pixels come from qa/png_probe.py (stdlib only). LD_PRELOAD is dropped
before the CLI. Temporary files live in a private mktemp directory.
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "qa"))

from png_probe import decode_png, find_color_bbox, self_check  # noqa: E402

MAGENTA = (255, 0, 255)
COLOR_TOLERANCE = 48


def child_env() -> dict[str, str]:
    env = os.environ.copy()
    env.pop("LD_PRELOAD", None)
    env["AIOFFICE_HEADLESS"] = "1"
    env["QT_QPA_PLATFORM"] = "offscreen"
    return env


def play(cli: Path, steps: list[dict], *, timeout: float) -> dict:
    with tempfile.TemporaryDirectory(prefix="wave26-text-") as tmp:
        path = Path(tmp) / "play.json"
        path.write_text(json.dumps(steps), encoding="utf-8")
        try:
            proc = subprocess.run(
                [str(cli), "play", str(path)],
                capture_output=True,
                text=True,
                timeout=timeout,
                cwd=REPO,
                env=child_env(),
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            raise TimeoutError(f"play exceeded {timeout:.0f}s") from exc
        except FileNotFoundError as exc:
            raise SystemExit(f"command not found: {cli}") from exc
    raw = (proc.stdout or "").strip()
    if not raw:
        err = (proc.stderr or "").strip()
        raise SystemExit(f"play failed ({proc.returncode}): {err or 'empty stdout'}")
    try:
        out = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"play bad JSON: {exc}\n{raw[:400]}") from exc
    if not out.get("ok"):
        raise SystemExit(f"play: {out.get('error', out)}")
    return out


def result_at(out: dict, index: int) -> dict:
    results = out.get("results") or []
    if index >= len(results):
        raise SystemExit(f"play result {index} missing")
    item = results[index]
    if not item.get("ok", True) and "png_base64" not in item and "x_mm" not in item and "ids" not in item:
        raise SystemExit(f"step {index} failed: {item.get('error', item)}")
    return item


def mm_to_px(mm: float, zoom: float) -> float:
    return mm * 96.0 / 25.4 * zoom


def png_of(preview: dict) -> tuple[int, int, bytes, float]:
    b64 = preview.get("png_base64")
    if not b64:
        raise SystemExit("page_preview did not return png_base64")
    width, height, rgba = decode_png(base64.b64decode(b64))
    zoom = float(preview.get("_zoom", 2))
    reported_w = preview.get("width")
    if reported_w and int(reported_w) != width:
        raise SystemExit(f"preview width {reported_w} != png {width}")
    return width, height, rgba, zoom


def require_magenta_at_caret(preview: dict, caret_x_mm: float, caret_y_mm: float, char_mm: float, label: str) -> str | None:
    preview = dict(preview)
    zoom = float(preview.pop("_zoom"))
    _w, height, rgba, _zoom = png_of({**preview, "_zoom": zoom})
    width = _w
    x = mm_to_px(caret_x_mm, zoom)
    y = mm_to_px(caret_y_mm, zoom)
    band_top = max(0, int(y - mm_to_px(1.5, zoom)))
    band_bot = min(height, int(y + mm_to_px(char_mm * 3.0 + 2.0, zoom)))
    box = find_color_bbox(width, height, rgba, MAGENTA, COLOR_TOLERANCE, region=(0, band_top, width, band_bot))
    if box is None:
        return f"{label}: no magenta pixels in the text band y={band_top}..{band_bot}"
    left_px = box[0]
    slack_px = mm_to_px(char_mm * 0.55, zoom)
    delta = left_px - x
    if abs(delta) > slack_px:
        return (
            f"{label}: magenta starts at x={left_px:.1f}px, caret at {x:.1f}px, "
            f"delta {delta:.1f}px exceeds {slack_px:.1f}px ({char_mm:.2f} mm/char)"
        )
    return None


def d1_chapter(cli: Path) -> str | None:
    steps = [
        {"tool": "document_new", "args": {"title": "Wave26 D1 chapter", "force": True}},
        {"tool": "set_page_layout", "args": {"chapter_numbers": True, "margins_mm": [18, 16, 18, 16]}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {
                        "id": "h26",
                        "type": "heading",
                        "level": 1,
                        "style": "Normal",
                        "runs": [
                            {
                                "text": "AAAA",
                                "font": "Liberation Mono",
                                "size_pt": 24,
                                "color": "#1A1F24",
                            },
                            {
                                "text": "MMMMMMMM",
                                "font": "Liberation Mono",
                                "size_pt": 24,
                                "color": "#FF00FF",
                                "highlight": "#FF00FF",
                            },
                        ],
                    }
                ],
            },
        },
        {"tool": "caret_geometry", "args": {"id": "h26", "offset": 4}},
        {"tool": "caret_geometry", "args": {"id": "h26", "offset": 5}},
        {"tool": "page_preview", "args": {"page": 1, "zoom": 2}},
    ]
    out = play(cli, steps, timeout=60)
    c0 = result_at(out, 3)
    c1 = result_at(out, 4)
    preview = result_at(out, 5)
    char_mm = abs(float(c1["x_mm"]) - float(c0["x_mm"]))
    if char_mm < 0.4:
        return f"D1 chapter: caret advance {char_mm:.3f} mm is too small to judge"
    preview["_zoom"] = 2
    return require_magenta_at_caret(preview, float(c0["x_mm"]), float(c0["y_mm"]), char_mm, "D1 chapter prefix")


def d1_note_restart(cli: Path) -> str | None:
    steps = [
        {"tool": "document_new", "args": {"title": "Wave26 D1 notes", "force": True}},
        {"tool": "set_page_layout", "args": {"note_restart": True, "margins_mm": [18, 16, 18, 16]}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {
                        "id": "host26",
                        "type": "paragraph",
                        "style": "Normal",
                        "runs": [
                            {"text": "SEE", "font": "Liberation Mono", "size_pt": 22, "color": "#1A1F24"},
                            {
                                "text": "99",
                                "note_id": "fn26",
                                "superscript": True,
                                "font": "Liberation Mono",
                                "size_pt": 22,
                                "color": "#1A1F24",
                            },
                            {"text": "TAIL", "font": "Liberation Mono", "size_pt": 22, "color": "#1A1F24"},
                        ],
                    },
                    {"id": "fn26", "type": "footnote", "runs": [{"text": "body of the note"}]},
                ],
            },
        },
    ]
    # Export from the same session as the inserts. The path is filled below.
    with tempfile.TemporaryDirectory(prefix="wave26-note-") as tmp:
        pdf_path = str(Path(tmp) / "note.pdf")
        steps.append({"tool": "document_export_pdf", "args": {"path": pdf_path}})
        out = play(cli, steps, timeout=60)
        result_at(out, 2)
        exported = result_at(out, 3)
        if not exported.get("ok", True):
            return f"D1 note_restart: export failed: {exported.get('error', exported)}"
        if not Path(pdf_path).is_file():
            return "D1 note_restart: PDF was not written"
        try:
            proc = subprocess.run(
                ["pdftotext", "-layout", "-f", "1", "-l", "1", pdf_path, "-"],
                capture_output=True,
                text=True,
                timeout=30,
                env=child_env(),
                check=False,
            )
        except FileNotFoundError:
            return "D1 note_restart: pdftotext is not on PATH"
        except subprocess.TimeoutExpired:
            return "D1 note_restart: pdftotext timed out"
        if proc.returncode != 0:
            return f"D1 note_restart: pdftotext failed ({proc.returncode}): {proc.stderr.strip()}"
        text = proc.stdout or ""
        folded = "".join(text.split())
        if "99" in text:
            return f"D1 note_restart: painted marker still shows the stored number 99 ({folded!r})"
        if "SEE1TAIL" not in folded and "SEE1" not in folded:
            return f"D1 note_restart: page ordinal 1 is not in the painted marker ({folded!r})"
    return None


def d2_run_language(cli: Path) -> str | None:
    """'hyphenation' on a 30 mm measure breaks after 'hyphen' in English and after 'hyphena' in Portuguese.

    The caret of the character that follows the break sits on the soft hyphen,
    so the first caret on the next line is one model offset past that character.
    Run language en → offset 7. Block language pt → offset 8.
    """
    steps = [
        {"tool": "document_new", "args": {"title": "Wave26 D2 hyphen", "force": True}},
        {
            "tool": "set_page_layout",
            "args": {
                "hyphenate": True,
                "width_mm": 42,
                "height_mm": 80,
                "margins_mm": [6, 6, 6, 6],
            },
        },
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {
                        "id": "hy26",
                        "type": "paragraph",
                        "lang": "pt",
                        "style": "Normal",
                        "runs": [
                            {
                                "text": "hyphenation",
                                "lang": "en",
                                "font": "Liberation Mono",
                                "size_pt": 18,
                            }
                        ],
                    }
                ],
            },
        },
    ]
    for offset in range(0, 12):
        steps.append({"tool": "caret_geometry", "args": {"id": "hy26", "offset": offset}})
    out = play(cli, steps, timeout=60)
    lines = []
    for offset in range(0, 12):
        item = result_at(out, 3 + offset)
        lines.append(int(item["line"]))
    next_line = next((offset for offset, line in enumerate(lines) if line >= 1), None)
    if next_line is None:
        return f"D2 hyphenation: 'hyphenation' did not wrap (lines {lines})"
    if next_line != 7:
        return (
            "D2 hyphenation: run lang en should break after 'hyphen' "
            f"(first next-line caret at offset 7); got offset {next_line}, lines {lines}"
        )
    return None


def d2_color(cli: Path) -> str | None:
    steps = [
        {"tool": "document_new", "args": {"title": "Wave26 D2 color", "force": True}},
        {
            "tool": "set_page_layout",
            "args": {"hyphenate": True, "margins_mm": [16, 14, 16, 14]},
        },
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {
                        "id": "c26",
                        "type": "paragraph",
                        "lang": "pt",
                        "style": "Normal",
                        "runs": [
                            {
                                "text": "internationalization",
                                "lang": "en",
                                "font": "Liberation Mono",
                                "size_pt": 20,
                                "color": "#1A1F24",
                            },
                            {
                                "text": "22222222",
                                "font": "Liberation Mono",
                                "size_pt": 20,
                                "color": "#FF00FF",
                                "highlight": "#FF00FF",
                            },
                        ],
                    }
                ],
            },
        },
        {"tool": "caret_geometry", "args": {"id": "c26", "offset": 20}},
        {"tool": "caret_geometry", "args": {"id": "c26", "offset": 21}},
        {"tool": "page_preview", "args": {"page": 1, "zoom": 2}},
    ]
    out = play(cli, steps, timeout=60)
    c0 = result_at(out, 3)
    c1 = result_at(out, 4)
    preview = result_at(out, 5)
    if int(c0["line"]) != int(c1["line"]):
        return "D2 color: the colored run wrapped, so the band is not one line"
    char_mm = abs(float(c1["x_mm"]) - float(c0["x_mm"]))
    if char_mm < 0.4:
        return f"D2 color: caret advance {char_mm:.3f} mm is too small to judge"
    preview["_zoom"] = 2
    return require_magenta_at_caret(preview, float(c0["x_mm"]), float(c0["y_mm"]), char_mm, "D2 run hyphenation")


def d3_linear(cli: Path) -> str | None:
    paragraph = "a" * 100_000
    steps = [
        {"tool": "document_new", "args": {"title": "Wave26 D3", "force": True}},
        {"tool": "set_page_layout", "args": {"margins_mm": [12, 12, 12, 12]}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [{"type": "paragraph", "runs": [{"text": paragraph}]}],
            },
        },
        {"tool": "page_preview", "args": {"page": 1, "zoom": 0.4}},
    ]
    started = time.monotonic()
    try:
        out = play(cli, steps, timeout=10)
    except TimeoutError:
        return "D3 modelToDisplay: page_preview of 100000 characters exceeded 10s"
    elapsed = time.monotonic() - started
    preview = result_at(out, 3)
    if not preview.get("png_base64"):
        return "D3 modelToDisplay: page_preview returned no png"
    if elapsed >= 10:
        return f"D3 modelToDisplay: page_preview took {elapsed:.2f}s (limit 10s)"
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", required=True)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    self_check()
    if args.self_test:
        print("ok   wave26-text self-check")
        return 0
    cli = Path(args.cli)
    if not cli.is_file():
        raise SystemExit(f"missing cli: {cli} (build aioffice_cli first)")
    failures: list[str] = []
    checks = (
        ("D1 chapter prefix", d1_chapter),
        ("D1 note_restart", d1_note_restart),
        ("D2 run language", d2_run_language),
        ("D2 color shift", d2_color),
        ("D3 linear map", d3_linear),
    )
    for name, fn in checks:
        try:
            err = fn(cli)
        except SystemExit as exc:
            err = f"{name}: {exc}"
        if err:
            failures.append(err)
            print(f"FAIL {err}")
        else:
            print(f"ok   {name}")
    if failures:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
