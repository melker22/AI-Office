#!/usr/bin/env python3
"""expect must be a non-empty object, and play must reject it before the call."""

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def play(cli, calls):
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as handle:
        json.dump(calls, handle)
        path = handle.name
    proc = subprocess.run([cli, "play", path], capture_output=True, text=True)
    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError:
        data = {"ok": None, "error": proc.stdout + proc.stderr, "results": None}
    return proc.returncode, data


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_play_expect.py /path/to/aioffice-cli")
    cli = sys.argv[1]
    if not Path(cli).is_file():
        raise SystemExit(f"missing {cli}")
    ok = True
    cases = [
        ("empty object", {}),
        ("array", []),
        ("string", "no"),
        ("number", 1),
        ("null", None),
    ]
    for label, expect in cases:
        code, data = play(
            cli,
            [
                {"tool": "document_new", "args": {"title": "shape", "force": True}},
                {"tool": "document_set_title", "args": {"title": "SHOULD-NOT-RUN"}, "expect": expect},
            ],
        )
        results = data.get("results")
        good = (
            code != 0
            and data.get("ok") is False
            and "expect" in (data.get("error") or "")
            and isinstance(results, list)
            and len(results) == 1
            and results[0].get("ok") is True
        )
        print(("ok  " if good else "FAIL") + " " + label)
        if not good:
            print(json.dumps(data, ensure_ascii=False)[:500])
        ok = good and ok
    code, data = play(
        cli,
        [{"tool": "document_new", "args": {"title": "kept", "force": True}, "expect": {"ok": True}}],
    )
    good = code == 0 and data.get("ok") is True
    print(("ok  " if good else "FAIL") + " valid expect")
    if not good:
        print(json.dumps(data, ensure_ascii=False)[:500])
    ok = good and ok
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
