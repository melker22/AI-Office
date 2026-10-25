#!/usr/bin/env python3
"""D2: an IPC failure never falls through to a blank local session.

The real aioffice-cli is the client. A private XDG_RUNTIME_DIR keeps the
user's GUI out of the test. AIOFFICE_HEADLESS=0 asks the headless binary not
to force headless, which is the GUI-IPC policy the full aioffice binary uses.
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import wave23_support as w23


def fail(msg: str) -> None:
    print(f"FAIL {msg}")
    raise SystemExit(1)


def assert_ipc_failure(proc, label: str, out_path: Path) -> None:
    text = proc.stdout.decode()
    if proc.returncode == 0:
        fail(f"{label}: exit 0, stdout {text[:300]!r}")
    try:
        obj = json.loads(text)
    except json.JSONDecodeError:
        fail(f"{label}: stdout is not JSON: {text[:300]!r} stderr {proc.stderr.decode()[:200]!r}")
    err = obj.get("error") or ""
    if obj.get("ok") is not False or not str(err).startswith("IPC failure:"):
        fail(f"{label}: want IPC failure, got {text[:300]}")
    if out_path.exists():
        fail(f"{label}: local session wrote {out_path} after an IPC failure")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_wave23_ipc.py /path/to/aioffice-cli", file=sys.stderr)
        return 2
    cli = sys.argv[1]
    if not Path(cli).is_file():
        print(f"missing {cli}", file=sys.stderr)
        return 2

    runtime_cm = w23.make_runtime()
    runtime = Path(runtime_cm.name)
    try:
        env_head = w23.child_env(runtime, headless_zero=False)
        env_gui = w23.child_env(runtime, headless_zero=True)
        sock = w23.print_ipc_path(cli, env_gui)

        # aioffice new must honour AIOFFICE_HEADLESS even when a GUI answers.
        fake = w23.FakeIpc(sock)
        fake.result = {"ok": True, "title": "FROM-GUI", "dirty": False, "path": ""}
        try:
            proc = w23.run_cli(cli, ["new"], env_head, timeout=15)
            text = proc.stdout.decode()
            if proc.returncode != 0:
                fail(f"headless new exit {proc.returncode}: {text[:300]} {proc.stderr.decode()[:200]}")
            if "FROM-GUI" in text:
                fail("aioffice new contacted the GUI while AIOFFICE_HEADLESS=1")
            if fake.calls:
                fail(f"aioffice new sent IPC while headless: {fake.calls[:1]}")
            obj = json.loads(text)
            if obj.get("ok") is not True:
                fail(f"headless new was not a local success: {text[:300]}")
        finally:
            fake.close()
        print("ok   new respects AIOFFICE_HEADLESS")

        out = runtime / "should-not-exist.aio.json"
        save_args = json.dumps({"path": str(out)})

        for mode, label in (
            ("invalid", "invalid response"),
            ("partial", "partial frame"),
            ("hang", "timeout"),
        ):
            fake = w23.FakeIpc(sock)
            fake.mode = mode
            try:
                extra = {"AIOFFICE_IPC_READ_MS": "400"} if mode == "hang" else None
                env = w23.child_env(runtime, headless_zero=True, extra=extra)
                proc = w23.run_cli(
                    cli,
                    ["call", "document_save", save_args],
                    env,
                    timeout=8,
                )
            except w23.subprocess.TimeoutExpired:
                fail(f"{label}: client did not return within 8s")
            finally:
                fake.close()
            assert_ipc_failure(proc, label, out)
            print(f"ok   no local fallback on {label}")

        # No socket, and a refused connection, are "no GUI": the local session runs.
        if out.exists():
            out.unlink()
        proc = w23.run_cli(cli, ["call", "document_save", save_args], env_gui, timeout=15)
        if proc.returncode != 0 or not out.exists():
            fail(f"missing socket should save locally: {proc.returncode} {proc.stdout.decode()[:300]}")
        out.unlink()
        print("ok   missing socket uses the local session")

        # Stale socket file: bind and close so connect is refused.
        stale = w23.socket.socket(w23.socket.AF_UNIX, w23.socket.SOCK_STREAM)
        stale.bind(sock)
        stale.listen(1)
        stale.close()
        try:
            proc = w23.run_cli(cli, ["call", "document_save", save_args], env_gui, timeout=15)
            if proc.returncode != 0 or not out.exists():
                fail(f"refused connect should save locally: {proc.returncode} {proc.stdout.decode()[:300]}")
        finally:
            if os.path.exists(sock):
                os.remove(sock)
            if out.exists():
                out.unlink()
        print("ok   refused connect uses the local session")

        _mcp_mode(cli, runtime, sock)
    finally:
        runtime_cm.cleanup()
    print("ok   ipc failure does not edit a blank session")
    return 0


def _mcp_call(proc, req_id: int, name: str, arguments) -> dict:
    w23.send_line(
        proc,
        {
            "jsonrpc": "2.0",
            "id": req_id,
            "method": "tools/call",
            "params": {"name": name, "arguments": arguments},
        },
    )
    return w23.read_json_line(proc, 15)


def _mcp_mode(cli: str, runtime: Path, sock: str) -> None:
    # Headless MCP ignores a live GUI for the whole process.
    fake = w23.FakeIpc(sock)
    fake.result = {"ok": True, "title": "FROM-GUI", "dirty": False, "path": ""}
    env = w23.child_env(runtime, headless_zero=False)
    proc = w23.spawn(cli, ["mcp"], env)
    try:
        w23.send_line(proc, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        w23.read_json_line(proc, 15)
        w23.send_line(proc, {"jsonrpc": "2.0", "method": "notifications/initialized"})
        msg = _mcp_call(proc, 2, "document_get", {})
        inner = w23.tool_text(msg)
        if inner.get("title") == "FROM-GUI" or fake.calls:
            fail(f"headless MCP used the GUI: calls={len(fake.calls)} title={inner.get('title')}")
        if inner.get("ok") is not True:
            fail(f"headless MCP document_get: {inner}")
    finally:
        w23.stop_proc(proc)
        fake.close()
    print("ok   MCP headless ignores a live GUI")

    # First tools/call reaches a GUI. A later failure stays on IPC.
    fake = w23.FakeIpc(sock)
    fake.result = {"ok": True, "title": "FROM-GUI", "dirty": False, "path": ""}
    env = w23.child_env(runtime, headless_zero=True, extra={"AIOFFICE_IPC_READ_MS": "400"})
    proc = w23.spawn(cli, ["mcp"], env)
    try:
        w23.send_line(proc, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        w23.read_json_line(proc, 15)
        w23.send_line(proc, {"jsonrpc": "2.0", "method": "notifications/initialized"})
        # tools/list must not lock the mode. The GUI is up, so the first tools/call uses it.
        w23.send_line(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        listed = w23.read_json_line(proc, 15)
        if listed.get("id") != 2:
            fail("tools/list id")
        first = _mcp_call(proc, 3, "document_get", {})
        inner = w23.tool_text(first)
        if inner.get("title") != "FROM-GUI":
            fail(f"first tools/call did not use the GUI: {inner}")
        fake.mode = "invalid"
        before = len(fake.calls)
        second = _mcp_call(proc, 4, "document_save", {"path": str(runtime / "nope.aio.json")})
        inner2 = w23.tool_text(second)
        err = str(inner2.get("error") or "")
        if inner2.get("ok") is not False or not err.startswith("IPC failure:"):
            fail(f"MCP switched to a local session after IPC failure: {inner2}")
        if (runtime / "nope.aio.json").exists():
            fail("MCP local fallback wrote nope.aio.json")
        if len(fake.calls) <= before:
            fail("second tools/call did not stay on the GUI socket")
    finally:
        w23.stop_proc(proc)
        fake.close()
    print("ok   MCP keeps GUI mode after an IPC failure")

    # No GUI on the first tools/call: stay headless even if a GUI appears later.
    env = w23.child_env(runtime, headless_zero=True)
    proc = w23.spawn(cli, ["mcp"], env)
    fake = None
    try:
        w23.send_line(proc, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        w23.read_json_line(proc, 15)
        w23.send_line(proc, {"jsonrpc": "2.0", "method": "notifications/initialized"})
        created = _mcp_call(proc, 2, "document_new", {"title": "LocalDoc", "force": True})
        inner = w23.tool_text(created)
        if inner.get("ok") is not True or inner.get("title") != "LocalDoc":
            fail(f"first call should be local: {inner}")
        fake = w23.FakeIpc(sock)
        fake.result = {"ok": True, "title": "FROM-GUI", "dirty": False, "path": ""}
        nxt = _mcp_call(proc, 3, "document_get", {})
        inner = w23.tool_text(nxt)
        if fake.calls or inner.get("title") != "LocalDoc":
            fail(f"MCP left headless mode: title={inner.get('title')} calls={len(fake.calls)}")
    finally:
        w23.stop_proc(proc)
        if fake:
            fake.close()
    print("ok   MCP keeps headless mode after a GUI appears")


if __name__ == "__main__":
    raise SystemExit(main())
