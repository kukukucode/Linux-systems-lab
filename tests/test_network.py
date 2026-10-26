#!/usr/bin/env python3
"""Short-lived local-only socket tests; never run infinite-loop servers directly."""
import re
import select
import socket
import subprocess
import sys
from pathlib import Path

build = Path(sys.argv[1] if len(sys.argv) > 1 else "build")


def binary(name):
    return str(build / name)


def run(name):
    return subprocess.run(
        [binary(name)],
        capture_output=True,
        text=True,
        timeout=10,
        check=True,
    ).stdout


lt_et = run("epoll_trigger_modes")
assert "LT: second ready=1" in lt_et, lt_et
assert "ET: second ready=0" in lt_et, lt_et
assert lt_et.count("remaining readable=5 bytes") == 2, lt_et

drain = run("epoll_et_drain")
assert "read: EAGAIN (drained)" in drain, drain
assert "total read=8 bytes" in drain, drain
assert "third epoll_wait=1" in drain, drain
assert "new data: XYZ (3 bytes)" in drain, drain

server = subprocess.Popen(
    [binary("tcp_echo_server")],
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    bufsize=1,
)
try:
    readable, _, _ = select.select([server.stdout], [], [], 5)
    if not readable:
        raise AssertionError("TCP echo server did not announce a port")
    line = server.stdout.readline()
    match = re.fullmatch(r"listening on 127\.0\.0\.1:(\d+)\n", line)
    if not match:
        raise AssertionError(f"unexpected TCP server output: {line!r}")
    port = int(match.group(1))
    with socket.create_connection(("127.0.0.1", port), timeout=5) as client:
        client.settimeout(5)
        client.sendall(b"hello ci\n")
        data = client.recv(128)
        assert data == b"hello ci\n", data
    remaining, _ = server.communicate(timeout=5)
    assert server.returncode == 0, remaining
finally:
    if server.poll() is None:
        server.kill()
        server.communicate(timeout=5)

print("Network smoke tests passed.")
