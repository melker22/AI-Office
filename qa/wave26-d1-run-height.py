#!/usr/bin/env python3
"""Wave 26: measured line height includes each run's font, matching paint.

A later run in a large size must reserve that height in layout (the following
paragraph moves to the next page) and in the preview (it does not cover the
large glyphs). caret_geometry and hit_test use the same boxes.
"""

from __future__ import annotations

import base64
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from png_probe import bbox_where, decode_png

REPO = Path(__file__).resolve().parent.parent
TIMEOUT = 90
MM_TO_PX = 96.0 / 25.4
ZOOM = 2.0


def child_env() -> dict[str, str]:
    env = os.environ.copy()
    env.pop("LD_PRELOAD", None)
    env.pop("LD_LIBRARY_PATH", None)
    env.pop("DISPLAY", None)
    env["AIOFFICE_HEADLESS"] = "1"
    env["QT_QPA_PLATFORM"] = "offscreen"
    return env


def find_cli(explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit)
        if not path.is_file():
            raise SystemExit(f"cli not found: {path}")
        return path
    env = os.environ.get("AIOFFICE_CLI")
    if env and Path(env).is_file():
        return Path(env)
    candidate = REPO / "build" / "linux" / "aioffice-cli"
    if candidate.is_file():
        return candidate
    raise SystemExit("aioffice-cli not found (pass --cli)")


def play(cli: Path, steps: list[dict]) -> list[dict]:
    handle = tempfile.NamedTemporaryFile(
        "w", suffix=".json", prefix="wave26-height-", delete=False, encoding="utf-8"
    )
    path = Path(handle.name)
    try:
        json.dump(steps, handle)
        handle.close()
        try:
            proc = subprocess.run(
                [str(cli), "play", str(path)],
                capture_output=True,
                timeout=TIMEOUT,
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
    if not raw:
        raise SystemExit(f"preview has no png: {list(result)[:8]}")
    return decode_png(base64.b64decode(raw))


def is_red(r: int, g: int, b: int, _a: int) -> bool:
    return r > 180 and g < 80 and b < 80


def is_blue(r: int, g: int, b: int, _a: int) -> bool:
    return r < 80 and g < 120 and b > 160


def mm_of_px(px: float, zoom: float = ZOOM) -> float:
    return float(px) / (MM_TO_PX * zoom)


def px_of_mm(mm: float, zoom: float = ZOOM) -> int:
    return int(round(float(mm) * MM_TO_PX * zoom))


def require(ok: bool, messages: list[str], text: str) -> None:
    print(("ok  " if ok else "bad ") + text)
    if not ok:
        messages.append(text)


def para_block(block_id: str, runs: list[dict]) -> dict:
    return {
        "id": block_id,
        "type": "paragraph",
        "style": "Normal",
        "space_before_pt": 0,
        "space_after_pt": 0,
        "runs": runs,
    }


def check_page_break(cli: Path, messages: list[str]) -> None:
    # Content height is about 20 mm. A 12 pt line fits twice; a 72 pt line
    # does not leave room for the following paragraph, so that paragraph
    # moves to page 2. Measurement that ignores the large run keeps both
    # paragraphs on page 1.
    results = play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "W26 run height page", "force": True}},
            {
                "tool": "set_page_layout",
                "args": {"width_mm": 120, "height_mm": 30, "margins_mm": [5, 5, 5, 5]},
            },
            {
                "tool": "insert_blocks",
                "args": {
                    "after_id": "end",
                    "blocks": [
                        para_block(
                            "pBig",
                            [
                                {"text": "aa ", "size_pt": 12, "font": "Liberation Serif"},
                                {
                                    "text": "Mg",
                                    "size_pt": 72,
                                    "font": "Liberation Serif",
                                    "color": "#FF0000",
                                },
                            ],
                        ),
                        para_block(
                            "pNext",
                            [
                                {
                                    "text": "NEXT",
                                    "size_pt": 12,
                                    "font": "Liberation Serif",
                                    "color": "#0000FF",
                                }
                            ],
                        ),
                    ],
                },
            },
            {"tool": "delete_range", "args": {"ids": ["p1"]}},
            {"tool": "caret_geometry", "args": {"id": "pBig", "offset": 0}},
            {"tool": "caret_geometry", "args": {"id": "pNext", "offset": 0}},
        ],
    )
    big = results[-2]
    nxt = results[-1]
    print(f"page break big page={big.get('page')} y={big.get('y_mm')} next page={nxt.get('page')} y={nxt.get('y_mm')}")
    require(int(big.get("page") or 0) == 1, messages, f"large paragraph caret page={big.get('page')} (want 1)")
    require(
        int(nxt.get("page") or 0) == 2,
        messages,
        f"following paragraph caret page={nxt.get('page')} (want 2; measured height ignored the 72 pt run)",
    )


def check_paint(cli: Path, messages: list[str]) -> None:
    results = play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "W26 run height paint", "force": True}},
            {
                "tool": "insert_blocks",
                "args": {
                    "after_id": "end",
                    "blocks": [
                        para_block(
                            "pBig",
                            [
                                {"text": "aa ", "size_pt": 12, "font": "Liberation Serif"},
                                {
                                    "text": "Mg",
                                    "size_pt": 72,
                                    "font": "Liberation Serif",
                                    "color": "#FF0000",
                                },
                            ],
                        ),
                        para_block(
                            "pNext",
                            [
                                {
                                    "text": "NEXT",
                                    "size_pt": 14,
                                    "font": "Liberation Serif",
                                    "color": "#0000FF",
                                }
                            ],
                        ),
                    ],
                },
            },
            {"tool": "delete_range", "args": {"ids": ["p1"]}},
            {"tool": "caret_geometry", "args": {"id": "pBig", "offset": 3}},
            {"tool": "caret_geometry", "args": {"id": "pNext", "offset": 0}},
            {"tool": "page_preview", "args": {"page": 1, "zoom": ZOOM}},
        ],
    )
    big_c = results[-3]
    next_c = results[-2]
    width, height, rgba = png_of(results[-1])
    red = bbox_where(rgba, width, height, is_red)
    blue = bbox_where(rgba, width, height, is_blue)
    if red is None or blue is None:
        require(False, messages, f"missing ink red={red} blue={blue}")
        return
    red_h = red[3] - red[1]
    print(f"paint red={red} h={red_h} blue={blue} next_y={next_c.get('y_mm')}")
    require(red_h > 80, messages, f"72 pt glyph height={red_h}px (want >80)")
    require(red[3] <= blue[1] + 2, messages, f"following text overlaps large run red_bottom={red[3]} blue_top={blue[1]}")
    next_top = px_of_mm(float(next_c["y_mm"]))
    require(
        next_top + 4 >= red[3],
        messages,
        f"caret of NEXT at {next_top}px is above the painted glyphs ending at {red[3]}px",
    )
    red_x = (red[0] + red[2]) / 2
    red_y = (red[1] + red[3]) / 2
    blue_x = (blue[0] + blue[2]) / 2
    blue_y = (blue[1] + blue[3]) / 2
    hits = play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "W26 run height paint", "force": True}},
            {
                "tool": "insert_blocks",
                "args": {
                    "after_id": "end",
                    "blocks": [
                        para_block(
                            "pBig",
                            [
                                {"text": "aa ", "size_pt": 12, "font": "Liberation Serif"},
                                {
                                    "text": "Mg",
                                    "size_pt": 72,
                                    "font": "Liberation Serif",
                                    "color": "#FF0000",
                                },
                            ],
                        ),
                        para_block(
                            "pNext",
                            [
                                {
                                    "text": "NEXT",
                                    "size_pt": 14,
                                    "font": "Liberation Serif",
                                    "color": "#0000FF",
                                }
                            ],
                        ),
                    ],
                },
            },
            {"tool": "delete_range", "args": {"ids": ["p1"]}},
            {
                "tool": "hit_test",
                "args": {"page": 1, "x_mm": mm_of_px(red_x), "y_mm": mm_of_px(red_y)},
            },
            {
                "tool": "hit_test",
                "args": {"page": 1, "x_mm": mm_of_px(blue_x), "y_mm": mm_of_px(blue_y)},
            },
        ],
    )
    red_hit = hits[-2]
    blue_hit = hits[-1]
    print(f"hit red={red_hit.get('id')} blue={blue_hit.get('id')} big_caret_y={big_c.get('y_mm')}")
    require(red_hit.get("id") == "pBig", messages, f"hit_test on the large glyphs returned {red_hit.get('id')}")
    require(blue_hit.get("id") == "pNext", messages, f"hit_test on NEXT returned {blue_hit.get('id')}")


def check_cell(cli: Path, messages: list[str]) -> None:
    results = play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "W26 run height cell", "force": True}},
            {"tool": "insert_table", "args": {"after_id": "end", "rows": 2, "cols": 1, "header": False}},
            {"tool": "delete_range", "args": {"ids": ["p1"]}},
            {
                "tool": "set_cell",
                "args": {
                    "id": "t1",
                    "row": 0,
                    "col": 0,
                    "runs": [
                        {"text": "a ", "size_pt": 10, "font": "Liberation Serif"},
                        {
                            "text": "Mg",
                            "size_pt": 72,
                            "font": "Liberation Serif",
                            "color": "#FF0000",
                        },
                    ],
                },
            },
            {
                "tool": "set_cell",
                "args": {
                    "id": "t1",
                    "row": 1,
                    "col": 0,
                    "runs": [
                        {
                            "text": "NEXT",
                            "size_pt": 14,
                            "font": "Liberation Serif",
                            "color": "#0000FF",
                        }
                    ],
                },
            },
            {"tool": "caret_geometry", "args": {"id": "t1", "offset": 0, "row": 0, "col": 0}},
            {"tool": "caret_geometry", "args": {"id": "t1", "offset": 0, "row": 1, "col": 0}},
            {"tool": "page_preview", "args": {"page": 1, "zoom": ZOOM}},
        ],
    )
    top = results[-3]
    bot = results[-2]
    gap = float(bot["y_mm"]) - float(top["y_mm"])
    print(f"cell row0 y={top.get('y_mm')} row1 y={bot.get('y_mm')} gap={gap:.2f}mm")
    require(gap >= 15.0, messages, f"cell row gap {gap:.2f} mm (want >=15; row height ignored the 72 pt run)")
    width, height, rgba = png_of(results[-1])
    red = bbox_where(rgba, width, height, is_red)
    blue = bbox_where(rgba, width, height, is_blue)
    if red is None or blue is None:
        require(False, messages, f"cell missing ink red={red} blue={blue}")
        return
    red_h = red[3] - red[1]
    require(red_h > 80, messages, f"cell 72 pt glyph height={red_h}px (want >80, not clipped to the small row)")
    require(
        red[3] <= blue[1] + 2,
        messages,
        f"next cell overlaps large run red_bottom={red[3]} blue_top={blue[1]}",
    )
    hits = play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "W26 run height cell", "force": True}},
            {"tool": "insert_table", "args": {"after_id": "end", "rows": 2, "cols": 1, "header": False}},
            {"tool": "delete_range", "args": {"ids": ["p1"]}},
            {
                "tool": "set_cell",
                "args": {
                    "id": "t1",
                    "row": 0,
                    "col": 0,
                    "runs": [
                        {"text": "a ", "size_pt": 10, "font": "Liberation Serif"},
                        {
                            "text": "Mg",
                            "size_pt": 72,
                            "font": "Liberation Serif",
                            "color": "#FF0000",
                        },
                    ],
                },
            },
            {
                "tool": "set_cell",
                "args": {
                    "id": "t1",
                    "row": 1,
                    "col": 0,
                    "runs": [
                        {
                            "text": "NEXT",
                            "size_pt": 14,
                            "font": "Liberation Serif",
                            "color": "#0000FF",
                        }
                    ],
                },
            },
            {
                "tool": "hit_test",
                "args": {
                    "page": 1,
                    "x_mm": mm_of_px((red[0] + red[2]) / 2),
                    "y_mm": mm_of_px((red[1] + red[3]) / 2),
                },
            },
            {
                "tool": "hit_test",
                "args": {
                    "page": 1,
                    "x_mm": mm_of_px((blue[0] + blue[2]) / 2),
                    "y_mm": mm_of_px((blue[1] + blue[3]) / 2),
                },
            },
        ],
    )
    red_hit = hits[-2]
    blue_hit = hits[-1]
    print(f"cell hit red row={red_hit.get('row')} blue row={blue_hit.get('row')}")
    require(red_hit.get("id") == "t1" and red_hit.get("row") == 0, messages, f"hit_test on large cell glyphs returned {red_hit}")
    require(blue_hit.get("id") == "t1" and blue_hit.get("row") == 1, messages, f"hit_test on next cell returned {blue_hit}")


def main() -> int:
    cli_arg = None
    if "--cli" in sys.argv:
        cli_arg = sys.argv[sys.argv.index("--cli") + 1]
    cli = find_cli(cli_arg)
    messages: list[str] = []
    check_page_break(cli, messages)
    check_paint(cli, messages)
    check_cell(cli, messages)
    if messages:
        print("wave26 run height FAIL", file=sys.stderr)
        for err in messages:
            print(f"  {err}", file=sys.stderr)
        return 1
    print("wave26 run height ok")
    return 0


if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    raise SystemExit(main())
