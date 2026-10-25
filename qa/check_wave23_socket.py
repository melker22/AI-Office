#!/usr/bin/env python3
"""D3: per-user IPC socket, no steal, partial frames stay buffered.

Drives the real IpcServer inside aioffice-cli (AIOFFICE_IPC_SERVE=1) over a
private runtime directory.
"""

from __future__ import annotations

import json
import os
import signal
import socket
import stat
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import wave23_support as w23


def fail(msg: str) -> None:
    print(f"FAIL {msg}")
    raise SystemExit(1)


def rpc(path: str, name: str, arguments: dict, timeout: float = 3) -> dict:
    conn = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    conn.settimeout(timeout)
    conn.connect(path)
    req = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {"name": name, "arguments": arguments},
    }
    conn.sendall(json.dumps(req, separators=(",", ":")).encode() + b"\n")
    data = b""
    while b"\n" not in data:
        chunk = conn.recv(65536)
        if not chunk:
            break
        data += chunk
    conn.close()
    if b"\n" not in data:
        raise w23.ProtocolError(f"no IPC response: {data[:80]!r}")
    msg = json.loads(data.split(b"\n", 1)[0].decode())
    result = msg.get("result")
    if not isinstance(result, dict):
        raise w23.ProtocolError(f"IPC result missing: {data[:200]!r}")
    return result


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_wave23_socket.py /path/to/aioffice-cli", file=sys.stderr)
        return 2
    cli = sys.argv[1]
    if not Path(cli).is_file():
        print(f"missing {cli}", file=sys.stderr)
        return 2

    runtime_cm = w23.make_runtime()
    runtime = Path(runtime_cm.name)
    server = None
    second = None
    try:
        env = w23.child_env(runtime)
        path = w23.print_ipc_path(cli, env)
        token = f"aioffice-{os.getuid()}"
        if token not in path:
            fail(f"socket path {path!r} does not include {token}")
        print(f"ok   socket name includes {token}")

        server = w23.start_serve(cli, env)
        # The announced path is the one the process bound.
        announced = server._wave23_err.decode()
        if path not in announced:
            fail(f"serve announced {announced!r}, want {path}")
        mode = stat.S_IMODE(os.stat(path).st_mode)
        if mode & 0o077:
            fail(f"socket mode {oct(mode)} is not owner-only")
        print(f"ok   socket mode {oct(mode)}")

        # A frame split before the newline must not be dropped.
        conn = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        conn.settimeout(3)
        conn.connect(path)
        body = json.dumps(
            {
                "jsonrpc": "2.0",
                "id": 7,
                "method": "tools/call",
                "params": {
                    "name": "document_set_title",
                    "arguments": {"title": "OWNER"},
                },
            },
            separators=(",", ":"),
        ).encode()
        conn.sendall(body[:12])
        time.sleep(0.2)
        early = b""
        conn.settimeout(0.3)
        try:
            early = conn.recv(65536)
        except socket.timeout:
            early = b""
        if early:
            fail(f"server answered a partial frame: {early[:80]!r}")
        conn.settimeout(3)
        conn.sendall(body[12:] + b"\n")
        data = b""
        while b"\n" not in data:
            chunk = conn.recv(65536)
            if not chunk:
                break
            data += chunk
        conn.close()
        if b"\n" not in data:
            fail(f"partial frame was not completed: {data[:80]!r}")
        result = json.loads(data.split(b"\n", 1)[0].decode()).get("result") or {}
        if result.get("ok") is not True or result.get("title") != "OWNER":
            fail(f"completed frame result: {result}")
        print("ok   partial IPC frame stays buffered")

        got = rpc(path, "document_get", {})
        if got.get("title") != "OWNER":
            fail(f"title did not stick: {got}")

        second = w23.start_serve(cli, env)
        err = getattr(second, "_wave23_err", b"").decode()
        # Give the second process a moment to try (and not steal) the socket.
        time.sleep(0.3)
        if second.poll() is not None and b"without IPC" not in err:
            rest = second.stderr.read().decode() if second.stderr else ""
            err += rest
        if "without IPC" not in err and "without IPC" not in getattr(second, "_wave23_err", b"").decode():
            # Re-read anything new.
            fd = second.stderr.fileno()
            import select

            ready, _, _ = select.select([fd], [], [], 0.5)
            if ready:
                extra = os.read(fd, 4096)
                second._wave23_err += extra
                err = second._wave23_err.decode()
        if "without IPC" not in err:
            fail(f"second server did not warn that it runs without IPC: {err[:400]!r}")
        still = rpc(path, "document_get", {})
        if still.get("title") != "OWNER":
            fail(f"second server stole the socket; title is {still.get('title')!r}")
        print("ok   second server does not remove a live socket")

        # Orphan: the socket file survives SIGKILL. The next server may remove it.
        os.kill(server.pid, signal.SIGKILL)
        server.wait(timeout=3)
        server = None
        w23.stop_proc(second)
        second = None
        deadline = time.monotonic() + 2
        while not os.path.exists(path) and time.monotonic() < deadline:
            time.sleep(0.05)
        if not os.path.exists(path):
            # Some kernels drop the file with the process. Create a stale node
            # the way a crash leaves a refused socket, then let the server reclaim it.
            stale = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            stale.bind(path)
            stale.listen(1)
            stale.close()
        third = w23.start_serve(cli, env)
        try:
            fresh = rpc(path, "document_get", {})
            if fresh.get("ok") is not True:
                fail(f"orphan socket was not reclaimed: {fresh}")
            if fresh.get("title") == "OWNER":
                fail("orphan reclaim attached to the dead server's document")
        finally:
            w23.stop_proc(third)
        print("ok   orphan socket can be removed")
    except w23.ProtocolError as exc:
        fail(str(exc))
    finally:
        w23.stop_proc(server)
        w23.stop_proc(second)
        runtime_cm.cleanup()
    print("ok   per-user IPC socket")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
