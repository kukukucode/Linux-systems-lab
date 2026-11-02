#!/usr/bin/env python3
"""Short-lived local-only socket tests; never run infinite-loop servers directly."""
import re
import select
import socket
import struct
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

partial = run("partial_write")
assert "total sent=65536 bytes" in partial, partial
assert "total received=65536 bytes" in partial, partial
assert "data integrity=OK" in partial, partial

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


# Check buffered epoll echo with a slow reader.
import threading
import time

epoll_server = subprocess.Popen(
    [binary("tcp_echo_epoll")],
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    bufsize=1,
)

try:
    readable, _, _ = select.select([epoll_server.stdout], [], [], 5)
    if not readable:
        raise AssertionError("epoll server startup timeout")

    line = epoll_server.stdout.readline()
    match = re.fullmatch(r"listening on 127\.0\.0\.1:(\d+)\n", line)
    if not match:
        raise AssertionError(f"unexpected epoll output: {line!r}")

    # Keep draining logs so the server cannot block on stdout.
    def drain_logs():
        for _ in epoll_server.stdout:
            pass

    threading.Thread(target=drain_logs, daemon=True).start()

    payload = bytes(range(256)) * 2048
    errors = []

    with socket.create_connection(
        ("127.0.0.1", int(match.group(1))), timeout=5
    ) as client:
        client.settimeout(30)

        def send_payload():
            try:
                client.sendall(payload)
                client.shutdown(socket.SHUT_WR)
            except OSError as error:
                errors.append(error)

        sender = threading.Thread(target=send_payload)
        sender.start()

        time.sleep(0.25)

        received = bytearray()

        while True:
            chunk = client.recv(4096)
            if not chunk:
                break
            received.extend(chunk)
            time.sleep(0.002)

        sender.join(timeout=5)

        if sender.is_alive():
            raise AssertionError("sender did not finish")
        if errors:
            raise errors[0]

        assert received == payload, "epoll echo data mismatch"


    # An aborted connection must not affect other clients.
    def check_echo(sock, message):
        sock.sendall(message)
        received = bytearray()

        while len(received) < len(message):
            chunk = sock.recv(len(message) - len(received))

            if not chunk:
                raise AssertionError("unexpected EOF")

            received.extend(chunk)

        assert received == message, (received, message)

    address = ("127.0.0.1", int(match.group(1)))

    with socket.create_connection(address, timeout=5) as survivor:
        survivor.settimeout(5)
        check_echo(survivor, b"before reset\n")

        with socket.create_connection(address, timeout=5) as resetter:
            resetter.settimeout(5)
            check_echo(resetter, b"reset client\n")

            # Closing with SO_LINGER(1, 0) sends TCP RST.
            resetter.setsockopt(
                socket.SOL_SOCKET,
                socket.SO_LINGER,
                struct.pack("ii", 1, 0),
            )

        time.sleep(0.2)

        check_echo(survivor, b"after reset\n")

        with socket.create_connection(address, timeout=5) as newcomer:
            newcomer.settimeout(5)
            check_echo(newcomer, b"new connection\n")

    assert epoll_server.poll() is None
    print("TCP RST resilience passed.")

finally:
    if epoll_server.poll() is None:
        epoll_server.terminate()
        try:
            epoll_server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            epoll_server.kill()
            epoll_server.wait(timeout=5)

print("Buffered epoll echo integrity passed.")

print("Network smoke tests passed.")
