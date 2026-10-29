#!/usr/bin/env python3
"""Wave 26: a selection on the second TOC entry is painted on that entry.

Each TOC run is its own marker. The selection passed to that marker is the
overlap with the marker's [textFrom, textTo). Other blocks still paint the
full selection. U+000A does not open a QTextLine, so the second heading
starts with U+2028 and the judged range is only the letters on that new line.
"""

from __future__ import annotations

import base64
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from png_probe import count_close, decode_png

REPO = Path(__file__).resolve().parent.parent
TIMEOUT = 90
MM_TO_PX = 96.0 / 25.4
ZOOM = 2.0
# #B5651D40 over paper #FDFBF7, same blend as qa/wave26-paint.py.
SEL_ON_PAPER = (145, 93, 117)
SEL_TOL = 30.0
FIRST = "ALPHAENTRY"
SECOND = "BETAENTRY"
LINE_SEP = "\u2028"


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
        "w", suffix=".json", prefix="wave26-toc-sel-", delete=False, encoding="utf-8"
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


def px_of_mm(mm: float) -> int:
    return int(round(float(mm) * MM_TO_PX * ZOOM))


def main() -> int:
    cli_arg = None
    if "--cli" in sys.argv:
        cli_arg = sys.argv[sys.argv.index("--cli") + 1]
    cli = find_cli(cli_arg)
    # toc_update stores one run per heading. Level 2 has two spaces. The
    # line separator sits on the first entry's line; BETAENTRY starts the next.
    first_text = FIRST + "\n"
    second_text = "  " + LINE_SEP + SECOND + "\n"
    beta_at = len(first_text) + len("  ") + len(LINE_SEP)
    beta_end = beta_at + len(SECOND)
    results = play(
        cli,
        [
            {"tool": "document_new", "args": {"title": "W26 toc selection", "force": True}},
            {
                "tool": "insert_blocks",
                "args": {
                    "after_id": "end",
                    "blocks": [
                        {
                            "id": "hA",
                            "type": "heading",
                            "level": 1,
                            "runs": [{"text": FIRST}],
                        },
                        {
                            "id": "hB",
                            "type": "heading",
                            "level": 2,
                            "runs": [{"text": LINE_SEP + SECOND}],
                        },
                    ],
                },
            },
            {"tool": "toc_update", "args": {}},
            {"tool": "delete_range", "args": {"ids": ["p1"]}},
            {"tool": "document_get", "args": {}},
            {"tool": "caret_geometry", "args": {"id": "toc1", "offset": 0}},
            {"tool": "caret_geometry", "args": {"id": "toc1", "offset": beta_at}},
            {
                "tool": "set_selection",
                "args": {
                    "anchor_id": "toc1",
                    "anchor_off": beta_at,
                    "focus_id": "toc1",
                    "focus_off": beta_end,
                },
            },
            {"tool": "page_preview", "args": {"page": 1, "zoom": ZOOM}},
        ],
    )
    doc = results[4].get("document") or {}
    body = doc.get("body") or []
    toc = next((b for b in body if b.get("type") == "toc"), None)
    if toc is None:
        raise SystemExit("document has no toc block")
    if toc.get("id") != "toc1":
        raise SystemExit(f"toc id {toc.get('id')!r} != toc1")
    runs = toc.get("runs") or []
    if len(runs) < 2:
        raise SystemExit(f"toc has {len(runs)} runs")
    if runs[0].get("text") != first_text or runs[1].get("text") != second_text:
        raise SystemExit(f"unexpected toc runs: {[r.get('text') for r in runs]!r}")
    first_c = results[5]
    second_c = results[6]
    width, height, rgba = png_of(results[-1])
    y0 = px_of_mm(float(first_c["y_mm"]))
    y1 = px_of_mm(float(second_c["y_mm"]))
    gap = y1 - y0
    print(
        f"toc carets y0={y0} line={first_c.get('line')} "
        f"y1={y1} line={second_c.get('line')} gap={gap}"
    )
    errors: list[str] = []
    if gap < 8:
        errors.append(f"second TOC entry shares the first entry's line (gap={gap}px)")
    else:
        # Stay 3 px inside each line. Marker clips pad by 2 px, so a sample
        # that starts on the shared boundary would see the other entry.
        first_box = (0, max(0, y0), width, min(height, y1 - 3))
        second_box = (0, min(height, y1 + 3), width, min(height, y1 + max(12, gap // 2)))
        first_n = count_close(rgba, width, height, SEL_ON_PAPER, SEL_TOL, first_box)
        second_n = count_close(rgba, width, height, SEL_ON_PAPER, SEL_TOL, second_box)
        print(f"toc selection first_band={first_box} n={first_n} second_band={second_box} n={second_n}")
        if second_n < 20:
            errors.append(f"second TOC entry selection pixels={second_n} (want >=20)")
        if first_n > 8:
            errors.append(f"first TOC entry selection pixels={first_n} (want <=8)")
    if errors:
        print("wave26 toc selection FAIL", file=sys.stderr)
        for err in errors:
            print(f"  {err}", file=sys.stderr)
        return 1
    print("wave26 toc selection ok")
    return 0


if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    raise SystemExit(main())
