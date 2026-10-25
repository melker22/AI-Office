#!/usr/bin/env python3
"""D17: invalid JSON does not run the tool; play is sequential; page is int|"all".

Also checks the prose in docs/mcp-tools.md and skills/SKILL.md so a closed
decision is not described as the behaviour it replaced.
"""

from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import wave23_support as w23

REPO = Path(__file__).resolve().parent.parent


def fail(msg: str) -> None:
    print(f"FAIL {msg}")
    raise SystemExit(1)


def schema_accepts_all(prop: dict) -> bool:
    """True when page is an integer or the string "all", not integer-only."""
    if prop.get("type") == "integer" and "anyOf" not in prop and "oneOf" not in prop:
        return False
    blobs = [prop]
    for key in ("anyOf", "oneOf"):
        if isinstance(prop.get(key), list):
            blobs.extend(x for x in prop[key] if isinstance(x, dict))
    saw_int = False
    saw_all = False
    for item in blobs:
        typ = item.get("type")
        types = typ if isinstance(typ, list) else [typ]
        if "integer" in types:
            saw_int = True
        if "string" in types:
            enum = item.get("enum")
            if enum is None or "all" in enum:
                saw_all = True
    return saw_int and saw_all


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_wave23_cli.py /path/to/aioffice-cli", file=sys.stderr)
        return 2
    cli = sys.argv[1]
    if not Path(cli).is_file():
        print(f"missing {cli}", file=sys.stderr)
        return 2

    runtime_cm = w23.make_runtime()
    runtime = Path(runtime_cm.name)
    try:
        env = w23.child_env(runtime)
        _invalid_json(cli, env, runtime)
        _play_and_prose(cli, env, runtime)
        _schema(cli, env)
    finally:
        runtime_cm.cleanup()
    print("ok   cli json, sequential play, page schema")
    return 0


def _invalid_json(cli: str, env: dict, runtime: Path) -> None:
    cases = ["not-json", "[1,2]", '"hello"', "1", "null", "true", '{"title":']
    for raw in cases:
        proc = w23.run_cli(cli, ["call", "document_get", raw], env, timeout=15)
        text = proc.stdout.decode()
        if proc.returncode == 0:
            fail(f"invalid JSON {raw!r} ran the tool, exit 0: {text[:200]}")
        if "invalid JSON" not in text and "invalid JSON" not in proc.stderr.decode():
            fail(f"invalid JSON {raw!r} missing error: {text[:200]} {proc.stderr.decode()[:200]}")
        try:
            obj = json.loads(text)
        except json.JSONDecodeError:
            fail(f"invalid JSON {raw!r} stdout is not an error object: {text[:200]}")
        if obj.get("ok") is not False:
            fail(f"invalid JSON {raw!r} returned ok: {text[:200]}")
        if "document" in obj:
            fail(f"invalid JSON {raw!r} still returned a document")
    print("ok   invalid CLI JSON does not run the tool")

    proc = w23.run_cli(cli, ["call", "document_get"], env, input_bytes=b"[]", timeout=15)
    if proc.returncode == 0 or "invalid JSON" not in proc.stdout.decode():
        fail(f"stdin array ran the tool: {proc.returncode} {proc.stdout.decode()[:200]}")
    print("ok   invalid stdin JSON does not run the tool")

    proc = w23.run_cli(cli, ["call", "document_get", "{}"], env, timeout=15)
    if proc.returncode != 0:
        fail(f"empty object was rejected: {proc.stdout.decode()[:200]}")
    proc = w23.run_cli(cli, ["call", "document_get", '  {"zoom": 1}  '], env, timeout=15)
    if proc.returncode != 0:
        fail(f"object with whitespace was rejected: {proc.stdout.decode()[:200]}")
    print("ok   valid JSON object still runs")

    # MCP: a non-object arguments string must not mark the session dirty.
    proc_mcp = w23.spawn(cli, ["mcp"], env)
    try:
        w23.send_line(proc_mcp, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        w23.read_json_line(proc_mcp, 15)
        w23.send_line(proc_mcp, {"jsonrpc": "2.0", "method": "notifications/initialized"})
        w23.send_line(
            proc_mcp,
            {
                "jsonrpc": "2.0",
                "id": 2,
                "method": "tools/call",
                "params": {"name": "document_set_title", "arguments": "not-json"},
            },
        )
        bad = w23.read_json_line(proc_mcp, 15)
        if "error" not in bad:
            fail(f"MCP invalid arguments string was not an error: {json.dumps(bad)[:300]}")
        w23.send_line(
            proc_mcp,
            {
                "jsonrpc": "2.0",
                "id": 3,
                "method": "tools/call",
                "params": {"name": "document_get", "arguments": {}},
            },
        )
        got = w23.tool_text(w23.read_json_line(proc_mcp, 15))
        if got.get("dirty") is not False:
            fail(f"invalid MCP arguments ran document_set_title, dirty={got.get('dirty')}")
    finally:
        w23.stop_proc(proc_mcp)
    print("ok   invalid MCP arguments do not run the tool")
    del runtime


def _play_and_prose(cli: str, env: dict, runtime: Path) -> None:
    saved = runtime / "played.aio.json"
    calls = [
        {"tool": "document_new", "args": {"title": "Kept", "force": True}},
        {"tool": "document_set_title", "args": {"title": "Changed"}},
        {"tool": "document_save", "args": {"path": str(saved)}},
        {"tool": "document_get", "args": {}, "expect": {"title": "NOPE"}},
    ]
    play = runtime / "play.json"
    play.write_text(json.dumps(calls), encoding="utf-8")
    proc = w23.run_cli(cli, ["play", str(play)], env, timeout=20)
    if proc.returncode == 0:
        fail("play succeeded despite a failing expect")
    if not saved.is_file():
        fail("play rolled back the save; it must keep earlier steps")
    body = json.loads(saved.read_text(encoding="utf-8"))
    title = body.get("meta", {}).get("title") or body.get("title")
    if title != "Changed":
        fail(f"saved title after a later expect failure is {title!r}")
    print("ok   play keeps earlier steps and stops on expect")

    help_proc = w23.run_cli(cli, ["--help"], env, timeout=15)
    usage = help_proc.stderr.decode() + help_proc.stdout.decode()
    if "atomic batch" in usage or "atomic" in usage:
        fail(f"help still calls play atomic: {usage[:400]}")
    if "first failing expect" not in usage:
        fail("help does not say play stops at the first failing expect")
    print("ok   help describes sequential play")

    docs = (REPO / "docs/mcp-tools.md").read_text(encoding="utf-8")
    if "runs `batch`" in docs or "atomic batch" in docs:
        fail("docs/mcp-tools.md still describes play as a batch")
    if "first failing expect" not in docs:
        fail("docs/mcp-tools.md does not say play stops at the first failing expect")
    transport = docs.split("## MCP server", 1)[-1].split("## ", 1)[0]
    if "Content-Length" in transport and "never" not in transport:
        fail("MCP transport section still offers Content-Length as an output framing")
    if "newline" not in transport.lower() and "per line" not in transport.lower():
        fail("MCP transport section does not say messages are one JSON line")
    print("ok   mcp-tools.md matches the closed decisions")

    skill = (REPO / "skills/SKILL.md").read_text(encoding="utf-8")
    if "Content-Length" in skill:
        fail("skills/SKILL.md still documents Content-Length framing")
    if "blank" not in skill.lower() and "IPC failure" not in skill:
        fail("skills/SKILL.md does not say an IPC failure avoids a blank document")
    print("ok   skill does not describe the old framing")


def _schema(cli: str, env: dict) -> None:
    proc = w23.spawn(cli, ["mcp"], env)
    try:
        w23.send_line(proc, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        w23.read_json_line(proc, 15)
        w23.send_line(proc, {"jsonrpc": "2.0", "method": "notifications/initialized"})
        w23.send_line(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        listed = w23.read_json_line(proc, 15)
        tools = listed.get("result", {}).get("tools") or []
        page = None
        for tool in tools:
            if tool.get("name") == "page_preview":
                page = tool.get("inputSchema", {}).get("properties", {}).get("page")
                break
        if not isinstance(page, dict):
            fail("page_preview.page schema missing")
        if not schema_accepts_all(page):
            fail(f"page schema rejects \"all\": {json.dumps(page)}")
    finally:
        w23.stop_proc(proc)
    print("ok   page_preview schema accepts integer or all")


if __name__ == "__main__":
    raise SystemExit(main())
