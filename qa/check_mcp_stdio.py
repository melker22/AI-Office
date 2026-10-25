#!/usr/bin/env python3
"""D1: MCP stdio is one compact JSON object per line, spoken by the real server.

Starts aioffice-cli mcp as a subprocess, sends initialize, notifications/initialized,
tools/list and tools/call, then one legacy Content-Length request. Every stdout
response is parsed strictly line by line. A Content-Length header on output fails.
"""

from __future__ import annotations

import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import wave23_support as w23


def fail(msg: str) -> None:
    print(f"FAIL {msg}")
    raise SystemExit(1)


def expect_id(msg: dict, want) -> None:
    if msg.get("id") != want:
        fail(f"response id {msg.get('id')!r} != {want!r}: {json.dumps(msg)[:300]}")
    if "error" in msg and "result" not in msg:
        fail(f"rpc error for id {want}: {json.dumps(msg)[:400]}")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_mcp_stdio.py /path/to/aioffice-cli", file=sys.stderr)
        return 2
    cli = sys.argv[1]
    if not Path(cli).is_file():
        print(f"missing {cli}", file=sys.stderr)
        return 2

    runtime = w23.make_runtime()
    try:
        env = w23.child_env(Path(runtime.name))
        proc = w23.spawn(cli, ["mcp"], env)
        try:
            w23.send_line(
                proc,
                {
                    "jsonrpc": "2.0",
                    "id": 1,
                    "method": "initialize",
                    "params": {
                        "protocolVersion": "2024-11-05",
                        "capabilities": {},
                        "clientInfo": {"name": "wave23", "version": "0"},
                    },
                },
            )
            init = w23.read_json_line(proc, 15)
            expect_id(init, 1)
            result = init.get("result")
            if not isinstance(result, dict) or result.get("protocolVersion") != "2024-11-05":
                fail(f"initialize result: {json.dumps(init)[:400]}")

            w23.send_line(proc, {"jsonrpc": "2.0", "method": "notifications/initialized"})
            if w23.line_waiting(proc, 0.3):
                extra = w23.read_exact_line(proc, 2)
                fail(f"notifications/initialized produced a stdout line: {extra[:120]!r}")

            w23.send_line(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
            listed = w23.read_json_line(proc, 15)
            expect_id(listed, 2)
            tools = listed.get("result", {}).get("tools")
            if not isinstance(tools, list) or not tools:
                fail("tools/list returned no tools")

            w23.send_line(
                proc,
                {
                    "jsonrpc": "2.0",
                    "id": 3,
                    "method": "tools/call",
                    "params": {"name": "document_get", "arguments": {}},
                },
            )
            called = w23.read_json_line(proc, 15)
            expect_id(called, 3)
            inner = w23.tool_text(called)
            if inner.get("ok") is not True or "document" not in inner:
                fail(f"tools/call document_get: {json.dumps(inner)[:400]}")

            # Legacy input is accepted. The reply is still one JSON line, never a header.
            w23.send_content_length(proc, {"jsonrpc": "2.0", "id": 4, "method": "ping"})
            ping = w23.read_json_line(proc, 15)
            expect_id(ping, 4)
            if ping.get("result") != {}:
                fail(f"ping result: {json.dumps(ping)[:300]}")

            if w23.line_waiting(proc, 0.2):
                extra = w23.read_exact_line(proc, 2)
                fail(f"unexpected extra stdout line: {extra[:120]!r}")

            proc.stdin.close()
            deadline = time.monotonic() + 5
            while proc.poll() is None and time.monotonic() < deadline:
                time.sleep(0.05)
            if proc.poll() is None:
                fail("server did not exit after stdin closed")
            if proc.returncode != 0:
                err = proc.stderr.read().decode()[:400]
                fail(f"server exit {proc.returncode}: {err}")
        finally:
            w23.stop_proc(proc)
    except w23.ProtocolError as exc:
        fail(str(exc))
    finally:
        runtime.cleanup()
    print("ok   mcp stdio newline JSON")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
