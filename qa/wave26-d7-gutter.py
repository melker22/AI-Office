#!/usr/bin/env python3
"""Wave 26 D7: gutter_mirror paint, caret_geometry, and hit_test share one x.

On an even mirrored page the painted word sits gutter_mm to the left of the
layout origin. caret_geometry and hit_test must use that painted point.
The odd page is the control: it is not shifted.
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
from typing import Any

from png_probe import decode_png

REPO = Path(__file__).resolve().parent.parent
TIMEOUT = 120
WORD = "REDGUTTERWORD"
# Millimetres. Antialiased edges sit inside this of the caret.
TOL_MM = 4.0


def child_env() -> dict[str, str]:
    env = os.environ.copy()
    env.pop("LD_PRELOAD", None)
    env.pop("LD_LIBRARY_PATH", None)
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


def play(cli: Path, steps: list[dict[str, Any]], folder: Path) -> list[dict[str, Any]]:
    playbook = folder / "play.json"
    playbook.write_text(json.dumps(steps), encoding="utf-8")
    try:
        proc = subprocess.run(
            [str(cli), "play", str(playbook)],
            cwd=str(REPO),
            env=child_env(),
            capture_output=True,
            text=True,
            timeout=TIMEOUT,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise SystemExit(f"timeout after {TIMEOUT}s") from exc
    raw = proc.stdout.strip()
    if not raw:
        raise SystemExit(f"play failed ({proc.returncode}): {proc.stderr.strip() or 'empty stdout'}")
    try:
        out = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"play bad JSON: {exc}\n{raw[:400]}") from exc
    if not out.get("ok"):
        raise SystemExit(f"play: {out.get('error', out)}")
    results = out.get("results") or []
    if len(results) != len(steps):
        raise SystemExit(f"play result count {len(results)} != {len(steps)}")
    return results


def px_to_mm(px: float) -> float:
    return px * 25.4 / 96.0


def red_bbox(png: bytes) -> tuple[int, int, int, int, int]:
    """Left, top, right, bottom (inclusive) of red ink, and the pixel count.

    Pure red and its antialiased edge. The brown page chrome (#B5651D) is
    excluded: red must clearly beat both green and blue.
    """
    width, height, rgba = decode_png(png)
    left = top = 10**9
    right = bottom = -1
    count = 0
    for y in range(height):
        row = y * width * 4
        for x in range(width):
            i = row + x * 4
            r, g, b, a = rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]
            if a < 128 or r < 160:
                continue
            if r <= g + 80 or r <= b + 80:
                continue
            count += 1
            if x < left:
                left = x
            if y < top:
                top = y
            if x > right:
                right = x
            if y > bottom:
                bottom = y
    if count < 20:
        raise SystemExit(f"red ink not found ({count} px, image {width}x{height})")
    return left, top, right, bottom, count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", default=None)
    args = parser.parse_args()
    cli = find_cli(args.cli)
    sys.path.insert(0, str(Path(__file__).resolve().parent))

    steps: list[dict[str, Any]] = [
        {"tool": "document_new", "args": {"title": "Wave26 D7", "force": True}},
        {"tool": "set_page_layout", "args": {"gutter_mm": 40, "gutter_mirror": True}},
        {
            "tool": "replace_range",
            "args": {
                "ids": ["p1"],
                "blocks": [
                    {
                        "id": "p1",
                        "type": "paragraph",
                        "runs": [{"text": WORD, "color": "#FF0000"}],
                    }
                ],
            },
        },
        {"tool": "insert_break", "args": {"after_id": "p1", "kind": "page"}},
        {
            "tool": "insert_blocks",
            "args": {
                "after_id": "end",
                "blocks": [
                    {
                        "id": "pEven",
                        "type": "paragraph",
                        "runs": [{"text": WORD, "color": "#FF0000"}],
                    }
                ],
            },
        },
        {"tool": "page_preview", "args": {"page": 1, "zoom": 1}},
        {"tool": "page_preview", "args": {"page": 2, "zoom": 1}},
        {"tool": "caret_geometry", "args": {"id": "p1", "offset": 0}},
        {"tool": "caret_geometry", "args": {"id": "p1", "offset": len(WORD)}},
        {"tool": "caret_geometry", "args": {"id": "pEven", "offset": 0}},
        {"tool": "caret_geometry", "args": {"id": "pEven", "offset": len(WORD)}},
    ]
    with tempfile.TemporaryDirectory(prefix="wave26-d7-") as tmp:
        results = play(cli, steps, Path(tmp))
    odd_png = base64.b64decode(results[5]["png_base64"])
    even_png = base64.b64decode(results[6]["png_base64"])
    odd_box = red_bbox(odd_png)
    even_box = red_bbox(even_png)
    odd_left, odd_right = px_to_mm(odd_box[0]), px_to_mm(odd_box[2])
    even_left, even_right = px_to_mm(even_box[0]), px_to_mm(even_box[2])
    odd_mid_y = px_to_mm((odd_box[1] + odd_box[3]) / 2.0)
    even_mid_y = px_to_mm((even_box[1] + even_box[3]) / 2.0)
    even_mid_x = (even_left + even_right) / 2.0

    c_odd0 = results[7]
    c_odd1 = results[8]
    c_even0 = results[9]
    c_even1 = results[10]
    errors: list[str] = []

    def near(got: float, want: float, label: str) -> None:
        if abs(got - want) > TOL_MM:
            errors.append(f"{label}: caret {got:.2f} mm vs ink {want:.2f} mm")

    near(float(c_odd0["x_mm"]), odd_left, "odd page caret 0 vs ink left")
    near(float(c_odd1["x_mm"]), odd_right, "odd page caret end vs ink right")
    near(float(c_even0["x_mm"]), even_left, "even page caret 0 vs ink left")
    near(float(c_even1["x_mm"]), even_right, "even page caret end vs ink right")
    if int(c_odd0.get("page", 0)) != 1:
        errors.append(f"odd caret page {c_odd0.get('page')}")
    if int(c_even0.get("page", 0)) != 2:
        errors.append(f"even caret page {c_even0.get('page')}")
    # The even page is painted a gutter to the left of the odd page.
    if float(c_even0["x_mm"]) > float(c_odd0["x_mm"]) - 20:
        errors.append(
            f"even caret x {c_even0['x_mm']:.2f} is not left of odd caret {c_odd0['x_mm']:.2f}"
        )

    hit_steps = steps + [
        {
            "tool": "hit_test",
            "args": {"page": 2, "x_mm": even_mid_x, "y_mm": even_mid_y},
        }
    ]
    with tempfile.TemporaryDirectory(prefix="wave26-d7-hit-") as tmp:
        hit = play(cli, hit_steps, Path(tmp))[-1]
    if hit.get("id") != "pEven":
        errors.append(f"hit_test id {hit.get('id')!r} != pEven")
    off = int(hit.get("offset", -1))
    if not (2 <= off <= len(WORD) - 2):
        errors.append(f"hit_test offset {off} is not inside {WORD!r} (want 2..{len(WORD) - 2})")
    # The click is the painted center. A clamped miss reports the start.
    if off == 0:
        errors.append("hit_test clamped to offset 0 left of the layout rect")

    if abs(float(c_odd0["y_mm"]) - odd_mid_y) > 15:
        errors.append(f"odd caret y {c_odd0['y_mm']} far from ink y {odd_mid_y:.2f}")

    if errors:
        print("wave26 D7 FAIL", file=sys.stderr)
        for err in errors:
            print(f"  {err}", file=sys.stderr)
        print(
            f"  odd ink x {odd_left:.2f}..{odd_right:.2f} caret {c_odd0['x_mm']:.2f}..{c_odd1['x_mm']:.2f}",
            file=sys.stderr,
        )
        print(
            f"  even ink x {even_left:.2f}..{even_right:.2f} caret {c_even0['x_mm']:.2f}..{c_even1['x_mm']:.2f} hit {hit}",
            file=sys.stderr,
        )
        return 1
    print(
        f"wave26 D7 ok even ink {even_left:.2f}..{even_right:.2f} "
        f"caret {float(c_even0['x_mm']):.2f} hit offset {off}"
    )
    return 0


if __name__ == "__main__":
    # Import png_probe from qa/ when ctest's cwd is the source root.
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    raise SystemExit(main())
