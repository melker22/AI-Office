#!/usr/bin/env python3
"""Wave 26 paint judges (D4, D5, D6).

Each check renders page_preview and looks for a color in a region. Antialiasing
is allowed (Euclidean tolerance). Pixels are never compared one-for-one.
PNG bytes are decoded by qa/png_probe.py (stdlib only). The CLI path comes from
--cli. LD_PRELOAD and DISPLAY are dropped (L22). Temp files are private.
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from png_probe import bbox_close, bbox_where, count_close, count_where, decode_png

REPO = Path(__file__).resolve().parent.parent
MM_TO_PX = 96.0 / 25.4
# #B5651D40 is AARRGGBB: src-over paper #FDFBF7 and cell peach #F3E0CC.
SEL_ON_PAPER = (145, 93, 117)
SEL_ON_PEACH = (142, 86, 105)
# Gray antialiasing of body text sits about 38 away from the paper blend.
# A filled selection is within a few levels of the blend. 30 rejects the gray.
SEL_TOL = 30.0


def child_env() -> dict[str, str]:
    env = os.environ.copy()
    env.pop("LD_PRELOAD", None)
    env.pop("DISPLAY", None)
    env["AIOFFICE_HEADLESS"] = "1"
    env["QT_QPA_PLATFORM"] = "offscreen"
    return env


def play(cli: Path, steps: list[dict]) -> list[dict]:
    handle = tempfile.NamedTemporaryFile(
        "w", suffix=".json", prefix="wave26-paint-", dir=None, delete=False, encoding="utf-8"
    )
    path = Path(handle.name)
    try:
        json.dump(steps, handle)
        handle.close()
        try:
            proc = subprocess.run(
                [str(cli), "play", str(path)],
                capture_output=True,
                timeout=90,
                cwd=REPO,
                env=child_env(),
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            raise SystemExit("play timed out") from exc
        except FileNotFoundError as exc:
            raise SystemExit(f"command not found: {cli}") from exc
    finally:
        path.unlink(missing_ok=True)
    raw = proc.stdout.decode("utf-8", errors="replace").strip()
    if not raw:
        err = proc.stderr.decode("utf-8", errors="replace").strip()
        raise SystemExit(f"play failed ({proc.returncode}): {err or 'empty stdout'}")
    try:
        payload = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"play bad JSON: {exc}\n{raw[:400]}") from exc
    if not payload.get("ok"):
        raise SystemExit(f"play: {payload.get('error', payload)}")
    results = payload.get("results") or []
    if len(results) != len(steps):
        raise SystemExit(f"play result count {len(results)} != {len(steps)}")
    for index, result in enumerate(results):
        if not result.get("ok", True):
            raise SystemExit(f"step {index} {steps[index]['tool']}: {result.get('error', result)}")
    return results


def png_of(result: dict) -> tuple[int, int, bytes]:
    raw = result.get("png_base64")
    if not raw and result.get("previews"):
        raw = result["previews"][0].get("png_base64")
    if not raw:
        raise SystemExit(f"preview has no png: {list(result)[:8]}")
    return decode_png(base64.b64decode(raw))


def is_red(r: int, g: int, b: int, _a: int) -> bool:
    return r > 200 and g < 50 and b < 50


def is_yellow(r: int, g: int, b: int, _a: int) -> bool:
    return r > 240 and g > 230 and b < 40


def is_dark(r: int, g: int, b: int, _a: int) -> bool:
    return r < 60 and g < 60 and b < 60


def is_orange(r: int, g: int, b: int, _a: int) -> bool:
    return r > 210 and 60 < g < 160 and b < 50


def is_magenta(r: int, g: int, b: int, _a: int) -> bool:
    return r > 180 and b > 180 and g < 80


def is_blue(r: int, g: int, b: int, _a: int) -> bool:
    return r < 50 and g < 90 and b > 180


def is_green(r: int, g: int, b: int, _a: int) -> bool:
    return r < 50 and g > 150 and b < 80


def fail(messages: list[str]) -> None:
    raise SystemExit("FAIL\n" + "\n".join(messages))


def require(ok: bool, messages: list[str], text: str) -> None:
    print(("ok  " if ok else "bad ") + text)
    if not ok:
        messages.append(text)


def check_d4(cli: Path) -> None:
    messages: list[str] = []
    toc = play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "W26 D4 toc", "force": True}},
            {
                "tool": "insert_blocks",
                "args": {
                    "after_id": "end",
                    "blocks": [
                        {
                            "type": "toc",
                            "runs": [
                                {"text": "plain entry ", "color": "#1A1F24", "size_pt": 12},
                                {
                                    "text": "RED HEAD",
                                    "color": "#FF0000",
                                    "size_pt": 28,
                                    "bold": True,
                                    "underline": True,
                                    "strike": True,
                                    "highlight": "#FFFF00",
                                },
                            ],
                        }
                    ],
                },
            },
            {"tool": "delete_range", "args": {"ids": ["p1"]}},
            {"tool": "page_preview", "args": {"page": 1, "zoom": 2}},
        ],
    )
    width, height, rgba = png_of(toc[-1])
    reds = count_where(rgba, width, height, is_red)
    yellows = count_where(rgba, width, height, is_yellow)
    red_box = bbox_where(rgba, width, height, is_red)
    red_h = 0 if red_box is None else red_box[3] - red_box[1]
    require(reds > 40 and red_h > 40, messages, f"toc second run red pixels={reds} bbox_h={red_h} (want >40 and height >40)")
    require(yellows > 8, messages, f"toc highlight yellow pixels={yellows} (want >8)")

    # Newlines do not force a QTextLine break in this layout, so the paragraph
    # wraps on spaces. The red run is the last word and lands on a later page.
    words = "word " * 220
    split = play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "W26 D4 split", "force": True}},
            {
                "tool": "set_page_layout",
                "args": {"width_mm": 120, "height_mm": 45, "margins_mm": [5, 5, 5, 5]},
            },
            {
                "tool": "insert_blocks",
                "args": {
                    "after_id": "end",
                    "blocks": [
                        {
                            "type": "paragraph",
                            "runs": [
                                {"text": words, "size_pt": 12},
                                {"text": "REDRUN", "color": "#FF0000", "size_pt": 22, "bold": True},
                            ],
                        }
                    ],
                },
            },
            {"tool": "delete_range", "args": {"ids": ["p1"]}},
            {"tool": "caret_geometry", "args": {"id": "p2", "offset": len(words)}},
        ],
    )
    red_page = int(split[-1].get("page") or 0)
    if red_page < 2:
        require(False, messages, f"page-split REDRUN caret landed on page {red_page}")
    else:
        preview = play(
            cli,
            [
                {"tool": "document_new", "args": {"title": "W26 D4 split", "force": True}},
                {
                    "tool": "set_page_layout",
                    "args": {"width_mm": 120, "height_mm": 45, "margins_mm": [5, 5, 5, 5]},
                },
                {
                    "tool": "insert_blocks",
                    "args": {
                        "after_id": "end",
                        "blocks": [
                            {
                                "type": "paragraph",
                                "runs": [
                                    {"text": words, "size_pt": 12},
                                    {"text": "REDRUN", "color": "#FF0000", "size_pt": 22, "bold": True},
                                ],
                            }
                        ],
                    },
                },
                {"tool": "delete_range", "args": {"ids": ["p1"]}},
                {"tool": "page_preview", "args": {"page": red_page, "zoom": 2}},
            ],
        )[-1]
        sw, sh, srgba = png_of(preview)
        split_red = count_where(srgba, sw, sh, is_red)
        # A continued paragraph keeps textFrom < 0 and is drawn with pdfDrawLine,
        # which already honors format ranges. This assertion guards that path.
        require(
            split_red > 30,
            messages,
            f"page-split red pixels on page {red_page}={split_red} (want >30)",
        )
        if split_red > 30:
            print("note page-split formats already reach the next page through pdfDrawLine")
    if messages:
        fail(messages)


def mm_px(mm: float, zoom: float = 1.0) -> int:
    return int(round(float(mm) * MM_TO_PX * zoom))


def sel_count(rgba: bytes, width: int, height: int, box: tuple[int, int, int, int]) -> int:
    return count_close(rgba, width, height, SEL_ON_PAPER, SEL_TOL, box) + count_close(
        rgba, width, height, SEL_ON_PEACH, SEL_TOL, box
    )


def check_d5(cli: Path) -> None:
    messages: list[str] = []
    # Spaces wrap; a stored newline stays on the same QTextLine. Offsets below
    # are the probed wrap of this string at 12 pt on the default A4 measure.
    fill = "AAAA " * 16
    text = fill + "TAIL " + fill + "HEAD " + fill + "CCCC"
    tail = text.index("TAIL")
    head = text.index("HEAD")
    head_end = head + len("HEAD")
    cccc = text.index("CCCC")
    line_left = 60
    para = play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "W26 D5 para", "force": True}},
            {
                "tool": "insert_blocks",
                "args": {
                    "after_id": "end",
                    "blocks": [{"type": "paragraph", "runs": [{"text": text}]}],
                },
            },
            {"tool": "delete_range", "args": {"ids": ["p1"]}},
            {"tool": "caret_geometry", "args": {"id": "p2", "offset": line_left}},
            {"tool": "caret_geometry", "args": {"id": "p2", "offset": tail}},
            {"tool": "caret_geometry", "args": {"id": "p2", "offset": head}},
            {"tool": "caret_geometry", "args": {"id": "p2", "offset": cccc}},
            {
                "tool": "set_selection",
                "args": {
                    "anchor_id": "p2",
                    "anchor_off": tail,
                    "focus_id": "p2",
                    "focus_off": head_end,
                },
            },
            {"tool": "page_preview", "args": {"page": 1, "zoom": 1}},
        ],
    )
    left_c, tail_c, head_c, cccc_c = para[3:7]
    print(
        "para carets"
        f" left=({left_c.get('x_mm')},{left_c.get('y_mm')})"
        f" tail=({tail_c.get('x_mm')},{tail_c.get('y_mm')})"
        f" head=({head_c.get('x_mm')},{head_c.get('y_mm')})"
        f" cccc=({cccc_c.get('x_mm')},{cccc_c.get('y_mm')})"
    )
    left_y = float(left_c["y_mm"])
    tail_y = float(tail_c["y_mm"])
    head_y = float(head_c["y_mm"])
    cccc_y = float(cccc_c["y_mm"])
    require(
        abs(left_y - tail_y) < 0.8 and tail_y + 2.0 < head_y and head_y + 2.0 < cccc_y,
        messages,
        f"wrap geometry left_y={left_y} tail_y={tail_y} head_y={head_y} cccc_y={cccc_y}",
    )
    width, height, rgba = png_of(para[-1])

    def band(caret: dict, dx0: int, dx1: int) -> tuple[int, int, int, int]:
        x = mm_px(caret["x_mm"])
        y = mm_px(caret["y_mm"])
        return x + dx0, y + 1, x + dx1, y + 14

    left_n = sel_count(rgba, width, height, band(left_c, 2, 36))
    tail_n = sel_count(rgba, width, height, band(tail_c, 2, 40))
    head_n = sel_count(rgba, width, height, band(head_c, 2, 40))
    cccc_n = sel_count(rgba, width, height, band(cccc_c, 2, 36))
    require(tail_n >= 15, messages, f"paragraph selection on TAIL pixels={tail_n} (want >=15)")
    require(head_n >= 15, messages, f"paragraph selection on HEAD line pixels={head_n} (want >=15)")
    require(left_n <= 3, messages, f"paragraph selection leaked left of TAIL pixels={left_n} (want <=3)")
    require(cccc_n <= 3, messages, f"paragraph selection leaked onto CCCC pixels={cccc_n} (want <=3)")

    # A newline does not open a second visual line. BOTLINE is the colored
    # span; the selection is located from that ink, not from the cell caret.
    cell_text = "TOPLINE\nBOTLINE"
    bot = cell_text.index("BOTLINE")
    cell = play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "W26 D5 cell", "force": True}},
            {
                "tool": "insert_table",
                "args": {"after_id": "end", "rows": 1, "cols": 1, "header": False},
            },
            {"tool": "delete_range", "args": {"ids": ["p1"]}},
            {
                "tool": "set_cell",
                "args": {
                    "id": "t1",
                    "row": 0,
                    "col": 0,
                    "runs": [
                        {"text": "TOPLINE\n"},
                        {"text": "BOTLINE", "color": "#2244FF", "size_pt": 16},
                    ],
                },
            },
            {
                "tool": "set_selection",
                "args": {
                    "anchor_id": "t1",
                    "anchor_off": bot,
                    "focus_id": "t1",
                    "focus_off": len(cell_text),
                    "row": 0,
                    "col": 0,
                },
            },
            {"tool": "page_preview", "args": {"page": 1, "zoom": 2}},
        ],
    )
    cw, ch, crgba = png_of(cell[-1])
    blue_box = bbox_where(crgba, cw, ch, is_blue)
    if blue_box is None:
        require(False, messages, "cell BOTLINE produced no blue ink to locate the line")
    else:
        bx0, by0, bx1, by1 = blue_box
        selected = sel_count(
            crgba, cw, ch, (max(0, bx0 - 4), max(0, by0 - 2), bx1 + 12, by1 + 14)
        )
        above = sel_count(
            crgba, cw, ch, (max(0, bx0 - 4), max(0, by0 - 36), bx1 + 12, max(0, by0 - 14))
        )
        require(selected >= 8, messages, f"cell selection on BOTLINE pixels={selected} (want >=8)")
        require(above <= 4, messages, f"cell selection leaked above BOTLINE pixels={above} (want <=4)")

    words = "word " * 180
    page2_text = words + "PAGE2WORD"
    word_at = len(words)
    word_end = word_at + len("PAGE2WORD")
    located = play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "W26 D5 split", "force": True}},
            {
                "tool": "set_page_layout",
                "args": {"width_mm": 120, "height_mm": 45, "margins_mm": [5, 5, 5, 5]},
            },
            {
                "tool": "insert_blocks",
                "args": {
                    "after_id": "end",
                    "blocks": [{"type": "paragraph", "runs": [{"text": page2_text}]}],
                },
            },
            {"tool": "delete_range", "args": {"ids": ["p1"]}},
            {"tool": "caret_geometry", "args": {"id": "p2", "offset": word_at}},
        ],
    )
    caret = located[-1]
    word_page = int(caret.get("page") or 0)
    print(f"PAGE2WORD caret page={word_page} x={caret.get('x_mm')} y={caret.get('y_mm')}")
    if word_page < 2:
        require(False, messages, f"PAGE2WORD caret landed on page {word_page}")
    else:
        preview = play(
            cli,
            [
                {"tool": "document_new", "args": {"title": "W26 D5 split", "force": True}},
                {
                    "tool": "set_page_layout",
                    "args": {"width_mm": 120, "height_mm": 45, "margins_mm": [5, 5, 5, 5]},
                },
                {
                    "tool": "insert_blocks",
                    "args": {
                        "after_id": "end",
                        "blocks": [{"type": "paragraph", "runs": [{"text": page2_text}]}],
                    },
                },
                {"tool": "delete_range", "args": {"ids": ["p1"]}},
                {
                    "tool": "set_selection",
                    "args": {
                        "anchor_id": "p2",
                        "anchor_off": word_at,
                        "focus_id": "p2",
                        "focus_off": word_end,
                    },
                },
                {"tool": "page_preview", "args": {"page": word_page, "zoom": 1}},
            ],
        )[-1]
        pw, ph, prgba = png_of(preview)
        x = mm_px(caret["x_mm"])
        y = mm_px(caret["y_mm"])
        on_word = sel_count(prgba, pw, ph, (x + 2, y + 1, x + 50, y + 14))
        above_word = sel_count(prgba, pw, ph, (x + 2, max(0, y - 22), x + 50, max(0, y - 6)))
        require(on_word >= 15, messages, f"continued-page selection pixels={on_word} (want >=15)")
        require(above_word <= 3, messages, f"continued-page selection one line above pixels={above_word} (want <=3)")
    if messages:
        fail(messages)


def region_of(rgba: bytes, width: int, height: int, rgb: tuple[int, int, int], tol: float) -> tuple[int, int, int, int]:
    box = bbox_close(rgba, width, height, rgb, tol)
    if box is None:
        raise SystemExit(f"background {rgb} not found")
    x0, y0, x1, y1 = box
    return x0 + 3, y0 + 3, max(x0 + 4, x1 - 3), max(y0 + 4, y1 - 3)


def check_d6(cli: Path) -> None:
    messages: list[str] = []
    results = play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "W26 D6", "force": True}},
            {
                "tool": "insert_table",
                "args": {"after_id": "end", "rows": 3, "cols": 3, "header": False},
            },
            {"tool": "delete_range", "args": {"ids": ["p1"]}},
            {
                "tool": "set_cell",
                "args": {
                    "id": "t1",
                    "row": 0,
                    "col": 0,
                    "background": "#DDDDDD",
                    "runs": [{"text": "Aa"}],
                },
            },
            {
                "tool": "set_cell",
                "args": {
                    "id": "t1",
                    "row": 0,
                    "col": 1,
                    "runs": [{"text": "RED", "color": "#FF0000", "size_pt": 28, "bold": True}],
                },
            },
            {
                "tool": "set_cell",
                "args": {
                    "id": "t1",
                    "row": 0,
                    "col": 2,
                    "background": "#7EC8FF",
                    "runs": [{"text": "SECRET", "hidden": True, "size_pt": 28}],
                },
            },
            {
                "tool": "set_cell",
                "args": {
                    "id": "t1",
                    "row": 1,
                    "col": 0,
                    "background": "#FFB6C1",
                    "runs": [{"text": "HIDDEN", "show_if": "nope", "size_pt": 28}],
                },
            },
            {
                "tool": "set_cell",
                "args": {
                    "id": "t1",
                    "row": 1,
                    "col": 1,
                    "runs": [{"text": "ORANGE", "show_if": "showme", "color": "#FF6600", "size_pt": 18}],
                },
            },
            {
                "tool": "set_cell",
                "args": {
                    "id": "t1",
                    "row": 1,
                    "col": 2,
                    "runs": [{"text": "SECRET", "redact": True, "color": "#FF00FF", "size_pt": 18}],
                },
            },
            {
                "tool": "set_cell",
                "args": {
                    "id": "t1",
                    "row": 2,
                    "col": 0,
                    "runs": [{"text": "MARK", "highlight": "#FFFF00", "size_pt": 16}],
                },
            },
            {
                "tool": "set_cell",
                "args": {
                    "id": "t1",
                    "row": 2,
                    "col": 1,
                    "runs": [{"text": "", "field": "page_number", "color": "#0000FF", "size_pt": 28, "bold": True}],
                },
            },
            {"tool": "nest_table", "args": {"id": "t1", "row": 2, "col": 2, "rows": 1, "cols": 1}},
            {
                "tool": "set_cell",
                "args": {
                    "id": "t1",
                    "row": 2,
                    "col": 2,
                    "nested_row": 0,
                    "nested_col": 0,
                    "runs": [{"text": "NEST", "color": "#00CC00", "size_pt": 22, "bold": True}],
                },
            },
            {"tool": "set_vars", "args": {"name": "showme", "value": "yes"}},
            {"tool": "page_preview", "args": {"page": 1, "zoom": 2}},
        ],
    )
    width, height, rgba = png_of(results[-1])
    plain = region_of(rgba, width, height, (221, 221, 221), 12)
    hidden_box = region_of(rgba, width, height, (126, 200, 255), 18)
    show_box = region_of(rgba, width, height, (255, 182, 193), 18)
    reds = count_where(rgba, width, height, is_red)
    red_box = bbox_where(rgba, width, height, is_red)
    red_h = 0 if red_box is None else red_box[3] - red_box[1]
    plain_dark = count_where(rgba, width, height, is_dark, plain)
    hidden_dark = count_where(rgba, width, height, is_dark, hidden_box)
    show_dark = count_where(rgba, width, height, is_dark, show_box)
    oranges = count_where(rgba, width, height, is_orange)
    magentas = count_where(rgba, width, height, is_magenta)
    yellows = count_where(rgba, width, height, is_yellow)
    blues = count_where(rgba, width, height, is_blue)
    blue_box = bbox_where(rgba, width, height, is_blue)
    blue_h = 0 if blue_box is None else blue_box[3] - blue_box[1]
    greens = count_where(rgba, width, height, is_green)
    require(plain_dark > 20, messages, f"plain cell dark ink={plain_dark} (want >20)")
    require(reds > 40 and red_h > 45, messages, f"red 28pt cell pixels={reds} bbox_h={red_h} (want >40 and height >45)")
    require(hidden_dark < 12, messages, f"hidden cell dark ink={hidden_dark} (want <12)")
    require(show_dark < 12, messages, f"show_if-false cell dark ink={show_dark} (want <12)")
    require(oranges > 20, messages, f"show_if-true orange pixels={oranges} (want >20)")
    require(magentas > 20, messages, f"redact magenta pixels={magentas} (want >20)")
    require(yellows > 15, messages, f"highlight yellow pixels={yellows} (want >15)")
    require(blues > 20 and blue_h > 40, messages, f"page field blue pixels={blues} bbox_h={blue_h} (want >20 and height >40)")
    require(greens > 15, messages, f"nested green pixels={greens} (want >15)")
    if messages:
        fail(messages)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", choices=("d4", "d5", "d6"))
    parser.add_argument("--cli")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        from png_probe import self_test

        self_test()
        return
    if not args.check or not args.cli:
        raise SystemExit("usage: wave26-paint.py --check d4|d5|d6 --cli PATH")
    cli = Path(args.cli)
    {"d4": check_d4, "d5": check_d5, "d6": check_d6}[args.check](cli)
    print(f"ok   wave26 {args.check}")


if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    main()
