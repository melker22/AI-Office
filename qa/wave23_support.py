"""Shared subprocess helpers for the wave 23 MCP/CLI checks.

Every external process is started with check=False. A missing binary is a
missing file, not a substring of stderr.
"""

from __future__ import annotations

import json
import os
import select
import socket
import subprocess
import tempfile
import threading
import time
from pathlib import Path


class ProtocolError(Exception):
    pass


def child_env(runtime: Path, *, headless_zero: bool = False, extra: dict | None = None) -> dict:
    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"
    # The parent session often has DISPLAY=:0 while this process cannot open
    # it. Qt's GTK platform theme then aborts before main() reaches the tool.
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
    env["QT_QPA_PLATFORMTHEME"] = "generic"
    env["XDG_RUNTIME_DIR"] = str(runtime)
    env.pop("AIOFFICE_IPC_READ_MS", None)
    env.pop("AIOFFICE_SOFFICE", None)
    env.pop("AIOFFICE_IPC_SERVE", None)
    env.pop("AIOFFICE_PRINT_IPC_PATH", None)
    if headless_zero:
        env["AIOFFICE_HEADLESS"] = "0"
    else:
        env["AIOFFICE_HEADLESS"] = "1"
    if extra:
        env.update(extra)
    return env


def spawn(cli: str, args: list[str], env: dict, *, stdin=None) -> subprocess.Popen:
    return subprocess.Popen(
        [cli, *args],
        stdin=subprocess.PIPE if stdin is None else stdin,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    )


def run_cli(cli: str, args: list[str], env: dict, *, input_bytes: bytes | None = None, timeout: float = 20) -> subprocess.CompletedProcess:
    return subprocess.run(
        [cli, *args],
        input=input_bytes,
        capture_output=True,
        env=env,
        timeout=timeout,
        check=False,
    )


def read_exact_line(proc: subprocess.Popen, timeout: float) -> bytes:
    """Read one stdout line from a live process. The line must not contain CR or LF."""
    fd = proc.stdout.fileno()
    buf = getattr(proc, "_wave23_buf", b"")
    deadline = time.monotonic() + timeout
    while b"\n" not in buf:
        remain = deadline - time.monotonic()
        if remain <= 0:
            raise ProtocolError(f"timeout waiting for newline; buffered {buf[:120]!r}")
        ready, _, _ = select.select([fd], [], [], remain)
        if not ready:
            raise ProtocolError(f"timeout waiting for newline; buffered {buf[:120]!r}")
        chunk = os.read(fd, 65536)
        if chunk == b"":
            raise ProtocolError(f"stdout closed before newline; buffered {buf[:120]!r}")
        if b"\n" in chunk and b"\n" in buf + chunk:
            pass
        buf += chunk
    line, rest = buf.split(b"\n", 1)
    proc._wave23_buf = rest
    if b"\n" in line or b"\r" in line:
        raise ProtocolError(f"framing is not a single LF-terminated line: {line[:120]!r}")
    return line


def parse_json_line(line: bytes) -> dict:
    if line.startswith(b"Content-Length") or line.startswith(b"content-length"):
        raise ProtocolError(f"Content-Length header on stdout: {line[:80]!r}")
    try:
        text = line.decode("utf-8")
        obj = json.loads(text)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ProtocolError(f"stdout line is not JSON: {line[:120]!r} ({exc})") from exc
    if not isinstance(obj, dict):
        raise ProtocolError(f"stdout line is not a JSON object: {line[:120]!r}")
    # Compact: the wire line has no raw newline, and a re-encode of the same
    # object must not need a longer line than Qt's compact form. Reject a line
    # that is pretty-printed (space-newline already stripped) by requiring the
    # parsed object to round-trip without introducing whitespace differences
    # that only a non-compact encoder would... Qt key order differs from
    # json.dumps, so compare the raw line for embedded whitespace newlines only.
    if b"\n" in line or b"\r" in line:
        raise ProtocolError("embedded newline in a JSON line")
    return obj


def read_json_line(proc: subprocess.Popen, timeout: float) -> dict:
    return parse_json_line(read_exact_line(proc, timeout))


def send_line(proc: subprocess.Popen, obj: dict) -> None:
    payload = json.dumps(obj, separators=(",", ":"), ensure_ascii=False).encode("utf-8") + b"\n"
    proc.stdin.write(payload)
    proc.stdin.flush()


def send_content_length(proc: subprocess.Popen, obj: dict) -> None:
    body = json.dumps(obj, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    header = f"Content-Length: {len(body)}\r\n\r\n".encode("ascii")
    proc.stdin.write(header + body)
    proc.stdin.flush()


def line_waiting(proc: subprocess.Popen, wait: float) -> bool:
    fd = proc.stdout.fileno()
    buf = getattr(proc, "_wave23_buf", b"")
    if buf:
        return True
    ready, _, _ = select.select([fd], [], [], wait)
    return bool(ready)


def tool_text(message: dict) -> dict:
    result = message.get("result")
    if not isinstance(result, dict):
        raise ProtocolError(f"missing result: {json.dumps(message)[:400]}")
    content = result.get("content")
    if not isinstance(content, list) or not content:
        raise ProtocolError(f"missing content: {json.dumps(message)[:400]}")
    text = content[0].get("text")
    if not isinstance(text, str):
        raise ProtocolError("tool text is not a string")
    try:
        inner = json.loads(text)
    except json.JSONDecodeError as exc:
        raise ProtocolError(f"tool text is not JSON: {text[:200]}") from exc
    if not isinstance(inner, dict):
        raise ProtocolError("tool text is not an object")
    return inner


class FakeIpc:
    """Minimal Unix-socket stand-in for the GUI IPC server."""

    def __init__(self, path: str):
        self.path = path
        self.calls: list[dict] = []
        self.mode = "ok"  # ok | invalid | partial | hang
        self.result = {"ok": True, "title": "FROM-GUI", "dirty": False, "path": ""}
        self._stop = threading.Event()
        if os.path.exists(path):
            os.remove(path)
        self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._sock.bind(path)
        self._sock.listen(8)
        self._sock.settimeout(0.2)
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def close(self) -> None:
        self._stop.set()
        try:
            self._sock.close()
        except OSError:
            pass
        self._thread.join(timeout=2)
        if os.path.exists(self.path):
            try:
                os.remove(self.path)
            except OSError:
                pass

    def _loop(self) -> None:
        while not self._stop.is_set():
            try:
                conn, _ = self._sock.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            threading.Thread(target=self._client, args=(conn,), daemon=True).start()

    def _client(self, conn: socket.socket) -> None:
        conn.settimeout(2)
        data = b""
        try:
            while b"\n" not in data:
                chunk = conn.recv(65536)
                if not chunk:
                    conn.close()
                    return
                data += chunk
                if len(data) > 8_000_000:
                    break
        except socket.timeout:
            conn.close()
            return
        line = data.split(b"\n", 1)[0]
        try:
            msg = json.loads(line.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            conn.close()
            return
        if isinstance(msg, dict):
            self.calls.append(msg)
        if self.mode == "hang":
            time.sleep(30)
            conn.close()
            return
        if self.mode == "invalid":
            try:
                conn.sendall(b"NOPE\n")
            except OSError:
                pass
            conn.close()
            return
        if self.mode == "partial":
            try:
                conn.sendall(b"{")
            except OSError:
                pass
            conn.close()
            return
        reply = {
            "jsonrpc": "2.0",
            "id": msg.get("id", 1) if isinstance(msg, dict) else 1,
            "result": self.result,
        }
        try:
            conn.sendall(json.dumps(reply, separators=(",", ":")).encode("utf-8") + b"\n")
        except OSError:
            pass
        conn.close()


def print_ipc_path(cli: str, env: dict) -> str:
    probe = dict(env)
    probe["AIOFFICE_PRINT_IPC_PATH"] = "1"
    proc = run_cli(cli, [], probe, timeout=15)
    if proc.returncode != 0:
        raise ProtocolError(f"print ipc path failed: {proc.stderr.decode()[:400]}")
    path = proc.stdout.decode().strip().splitlines()[-1].strip()
    if not path:
        raise ProtocolError("empty ipc path")
    return path


def start_serve(cli: str, env: dict) -> subprocess.Popen:
    serve_env = dict(env)
    serve_env["AIOFFICE_IPC_SERVE"] = "1"
    serve_env.pop("AIOFFICE_PRINT_IPC_PATH", None)
    proc = spawn(cli, [], serve_env)
    deadline = time.monotonic() + 10
    err = b""
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            rest = proc.stderr.read() if proc.stderr else b""
            raise ProtocolError(f"ipc serve exited {proc.returncode}: {(err + rest).decode()[:500]}")
        fd = proc.stderr.fileno()
        ready, _, _ = select.select([fd], [], [], 0.2)
        if ready:
            err += os.read(fd, 4096)
            proc._wave23_err = err
            if b"ipc serving" in err:
                return proc
        else:
            proc._wave23_err = err
    raise ProtocolError(f"ipc serve did not announce: {err.decode()[:500]}")


def stop_proc(proc: subprocess.Popen | None) -> None:
    if proc is None or proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=3)


def make_runtime() -> tempfile.TemporaryDirectory:
    return tempfile.TemporaryDirectory(prefix="aioffice-wave23-")
