#!/usr/bin/env python3
"""Short-lived local-only socket tests; never run infinite-loop servers directly."""
import re
import select
import socket
import struct
import subprocess
import sys
import time
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

    # Test RST while the peer is not reading echoed data.
    survivors = []
    resetter = None

    try:
        for i in range(3):
            sock = socket.create_connection(address, timeout=5)
            sock.settimeout(5)
            check_echo(sock, f"survivor {i}\\n".encode())
            survivors.append(sock)

        resetter = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        resetter.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        resetter.settimeout(5)
        resetter.connect(address)
        resetter.setblocking(False)

        payload = b"X" * 65536
        sent = 0
        deadline = time.monotonic() + 3

        while sent < 2097152 and time.monotonic() < deadline:
            try:
                n = resetter.send(payload)
                if n == 0:
                    break
                sent += n
            except BlockingIOError:
                time.sleep(0.01)

        assert sent > 0, "no data sent before RST"

        time.sleep(0.25)

        resetter.setsockopt(
            socket.SOL_SOCKET,
            socket.SO_LINGER,
            struct.pack("ii", 1, 0),
        )
        resetter.close()
        resetter = None

        for i, sock in enumerate(survivors):
            check_echo(sock, f"still alive {i}\\n".encode())

        # RST processing is asynchronous; allow time to free the slot.
        deadline = time.monotonic() + 5

        while True:
            try:
                with socket.create_connection(address, timeout=1) as newcomer:
                    newcomer.settimeout(1)
                    check_echo(newcomer, b"replacement\\n")
                break
            except (OSError, AssertionError):
                if time.monotonic() >= deadline:
                    raise AssertionError("client slot not reused after RST")
                time.sleep(0.1)

        assert epoll_server.poll() is None
        print("RST under backpressure passed.")

    finally:
        if resetter is not None:
            resetter.close()
        for sock in survivors:
            sock.close()


    # Idle clients should time out without affecting active clients.
    with (
        socket.create_connection(address, timeout=5) as idle,
        socket.create_connection(address, timeout=5) as active,
    ):

        idle.settimeout(3)
        active.settimeout(3)

        for i in range(7):
            check_echo(active, f"keepalive {i}\n".encode())
            time.sleep(1)

        assert idle.recv(1) == b"", "idle client was not disconnected"
        print("Idle timeout passed.")

        check_echo(active, b"still active\n")

        with socket.create_connection(address, timeout=5) as newcomer:
            newcomer.settimeout(3)
            check_echo(newcomer, b"new client\n")

        print("Active client and slot reuse passed.")

finally:
    if epoll_server.poll() is None:
        epoll_server.terminate()
        try:
            epoll_server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            epoll_server.kill()
            epoll_server.wait(timeout=5)

print("Buffered epoll echo integrity passed.")


# Verify orderly shutdown for both termination signals.
import signal


def check_server_shutdown(stop_signal):
    process = subprocess.Popen(
        [binary("tcp_echo_epoll")],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )

    try:
        readable, _, _ = select.select([process.stdout], [], [], 5)
        if not readable:
            raise AssertionError("shutdown test: startup timeout")

        line = process.stdout.readline()
        match = re.fullmatch(
            r"listening on 127\.0\.0\.1:(\d+)\n", line
        )
        assert match, f"unexpected startup output: {line!r}"

        with socket.create_connection(
            ("127.0.0.1", int(match.group(1))), timeout=5
        ) as client:
            client.settimeout(5)
            client.sendall(b"shutdown probe\n")

            received = bytearray()
            while len(received) < len(b"shutdown probe\n"):
                chunk = client.recv(64)
                if not chunk:
                    raise AssertionError("unexpected EOF before shutdown")
                received.extend(chunk)

            assert received == b"shutdown probe\n"

            process.send_signal(stop_signal)
            output, _ = process.communicate(timeout=5)

            assert process.returncode == 0, output
            assert "shutdown requested" in output, output
            assert "closed fd=" in output, output
            assert "shutdown complete" in output, output

    finally:
        if process.poll() is None:
            process.kill()
            process.communicate(timeout=5)


check_server_shutdown(signal.SIGINT)
print("SIGINT shutdown passed.")

check_server_shutdown(signal.SIGTERM)
print("SIGTERM shutdown passed.")


# Check HTTP status, headers, and exact response body.
def check_http_route(
    path, expected_status, expected_body,
    method="GET", expected_allow=None, request_line=None,
    raw_request=None, shutdown_write=False,
    slow_chunks=0, max_elapsed=None, split_body=False,
    expected_body_received=None
):
    process = subprocess.Popen(
        [binary("http_routes")],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    try:
        readable, _, _ = select.select([process.stdout], [], [], 5)
        assert readable, "HTTP server startup timeout"

        line = process.stdout.readline()
        match = re.fullmatch(
            r"listening on 127\.0\.0\.1:(\d+)\n", line
        )
        assert match, f"unexpected startup output: {line!r}"

        with socket.create_connection(
            ("127.0.0.1", int(match.group(1))), timeout=5
        ) as client:
            client.settimeout(5)

            line = (
                request_line
                if request_line is not None
                else f"{method} {path} HTTP/1.1"
            )

            request = (
                f"{line}\r\n"
                "Host: localhost\r\n"
                "Connection: close\r\n"
                "\r\n"
            ).encode("ascii")

            if raw_request is not None:
                request = raw_request

            started = time.monotonic()
            client.sendall(request)

            if split_body:
                client.settimeout(0.25)
                try:
                    early = client.recv(1)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(
                        f"premature HTTP response: {early!r}"
                    )

                client.settimeout(5)
                client.sendall(b"llo")

            for _ in range(slow_chunks):
                time.sleep(0.4)
                client.sendall(b"a")

            if shutdown_write:
                client.shutdown(socket.SHUT_WR)

            chunks = []
            while True:
                chunk = client.recv(4096)
                if not chunk:
                    break
                chunks.append(chunk)

        output, _ = process.communicate(timeout=5)
        assert process.returncode == 0, output

        response = b"".join(chunks)
        headers, separator, body = response.partition(b"\r\n\r\n")

        assert separator, "missing HTTP header terminator"
        assert headers.split(b"\r\n")[0] == expected_status
        assert (
            b"Content-Length: " + str(len(expected_body)).encode()
        ) in headers.split(b"\r\n")
        if expected_allow is not None:
            assert (
                b"Allow: " + expected_allow
            ) in headers.split(b"\r\n")

        assert body == expected_body, (body, expected_body)

        if split_body:
            assert "body_received=5" in output, output

        if expected_body_received is not None:
            assert (
                f"body_received={expected_body_received}" in output
            ), output

        if max_elapsed is not None:
            elapsed = time.monotonic() - started
            assert elapsed < max_elapsed, (
                f"HTTP deadline exceeded: {elapsed:.2f}s"
            )

    finally:
        if process.poll() is None:
            process.kill()
            process.communicate(timeout=5)


check_http_route("/hello", b"HTTP/1.1 200 OK", b"hello")
check_http_route("/unknown", b"HTTP/1.1 404 Not Found", b"not found")

check_http_route(
    "/hello",
    b"HTTP/1.1 405 Method Not Allowed",
    b"method not allowed",
    method="POST",
    expected_allow=b"GET",
)

check_http_route(
    "/hello",
    b"HTTP/1.1 400 Bad Request",
    b"bad request",
    request_line="GET /hello HTTP/1.1 EXTRA",
)

check_http_route(
    "/hello",
    b"HTTP/1.1 400 Bad Request",
    b"bad request",
    request_line="GET /hello",
)

check_http_route(
    "/hello",
    b"HTTP/1.1 400 Bad Request",
    b"bad request",
    request_line="GET /hello HTTP/1.1\x00EXTRA",
)

prefix = b"GET /hello HTTP/1.1\r\nX-Long: "
oversized_request = prefix + b"a" * (4095 - len(prefix))

assert len(oversized_request) == 4095

check_http_route(
    "/hello",
    b"HTTP/1.1 431 Request Header Fields Too Large",
    b"headers too large",
    raw_request=oversized_request,
)

check_http_route(
    "/hello",
    b"HTTP/1.1 400 Bad Request",
    b"bad request",
    raw_request=b"GET /hello HTTP/1.1\r\nHost: localhost\r\n",
    shutdown_write=True,
)

check_http_route(
    "/hello",
    b"HTTP/1.1 408 Request Timeout",
    b"request timeout",
    raw_request=b"GET /hello HTTP/1.1\r\n",
)

check_http_route(
    "/hello",
    b"HTTP/1.1 408 Request Timeout",
    b"request timeout",
    raw_request=b"GET /hello HTTP/1.1\r\nX-Slow: ",
    slow_chunks=4,
    max_elapsed=3.0,
)

# HTTP/1.1 requires exactly one non-empty Host header.
check_http_route(
    "/hello",
    b"HTTP/1.1 400 Bad Request",
    b"bad request",
    raw_request=(
        b"GET /hello HTTP/1.1\r\n"
        b"Connection: close\r\n"
        b"\r\n"
    ),
)

check_http_route(
    "/hello",
    b"HTTP/1.1 400 Bad Request",
    b"bad request",
    raw_request=(
        b"GET /hello HTTP/1.1\r\n"
        b"Host: localhost\r\n"
        b"Host: example.com\r\n"
        b"\r\n"
    ),
)

check_http_route(
    "/hello",
    b"HTTP/1.1 400 Bad Request",
    b"bad request",
    raw_request=(
        b"GET /hello HTTP/1.1\r\n"
        b"Host:\r\n"
        b"\r\n"
    ),
)

check_http_route(
    "/hello",
    b"HTTP/1.1 200 OK",
    b"hello",
    raw_request=(
        b"GET /hello HTTP/1.1\r\n"
        b"host: localhost\r\n"
        b"\r\n"
    ),
)

# Validate HTTP header values and reject obsolete line folding.
header_prefix = b"GET /hello HTTP/1.1\r\nHost: localhost\r\n"

check_http_route(
    "/hello",
    b"HTTP/1.1 400 Bad Request",
    b"bad request",
    raw_request=header_prefix + b"X-Test: a\x01b\r\n\r\n",
)

check_http_route(
    "/hello",
    b"HTTP/1.1 400 Bad Request",
    b"bad request",
    raw_request=header_prefix + b"X-Test: a\x7fb\r\n\r\n",
)

check_http_route(
    "/hello",
    b"HTTP/1.1 400 Bad Request",
    b"bad request",
    raw_request=header_prefix + b"X-Test: a\r\n b\r\n\r\n",
)

check_http_route(
    "/hello",
    b"HTTP/1.1 200 OK",
    b"hello",
    raw_request=header_prefix + b"X-Test: a\tb\r\n\r\n",
)

print("HTTP header syntax and routing tests passed.")

check_http_route(
    "/hello",
    b"HTTP/1.1 405 Method Not Allowed",
    b"method not allowed",
    raw_request=(
        b"POST /hello HTTP/1.1\r\n"
        b"Host: localhost\r\n"
        b"Content-Length: 5\r\n"
        b"\r\n"
        b"he"
    ),
    split_body=True,
)

partial_body_request = (
    b"POST /hello HTTP/1.1\r\n"
    b"Host: localhost\r\n"
    b"Content-Length: 5\r\n"
    b"\r\n"
    b"he"
)

# Body is shorter than Content-Length, then the sender closes.
check_http_route(
    "/hello",
    b"HTTP/1.1 400 Bad Request",
    b"bad request",
    raw_request=partial_body_request,
    shutdown_write=True,
)

print("Incomplete HTTP body EOF passed.")

# Body is incomplete and the sender keeps the connection open.
check_http_route(
    "/hello",
    b"HTTP/1.1 408 Request Timeout",
    b"request timeout",
    raw_request=partial_body_request,
)

print("Incomplete HTTP body timeout passed.")
print("Split HTTP body receiving passed.")

# Content-Length boundary and framing validation.
cl_prefix = b"GET /hello HTTP/1.1\r\nHost: localhost\r\n"

check_http_route(
    "/hello",
    b"HTTP/1.1 200 OK",
    b"hello",
    raw_request=cl_prefix + b"Content-Length: 0\r\n\r\n",
)

invalid_lengths = [
    ("non-numeric", b"Content-Length: abc\r\n"),
    ("negative", b"Content-Length: -1\r\n"),
    (
        "duplicate",
        b"Content-Length: 2\r\nContent-Length: 2\r\n",
    ),
    (
        "overflow",
        b"Content-Length: " + b"9" * 64 + b"\r\n",
    ),
    (
        "conflicting framing",
        b"Content-Length: 0\r\nTransfer-Encoding: chunked\r\n",
    ),
]

for name, fields in invalid_lengths:
    check_http_route(
        "/hello",
        b"HTTP/1.1 400 Bad Request",
        b"bad request",
        raw_request=cl_prefix + fields + b"\r\n",
    )
    print(f"Content-Length {name}: OK")

print("Content-Length boundary tests passed.")

check_http_route(
    "/hello",
    b"HTTP/1.1 405 Method Not Allowed",
    b"method not allowed",
    raw_request=(
        b"POST /hello HTTP/1.1\r\n"
        b"Host: localhost\r\n"
        b"Content-Length: 5\r\n"
        b"\r\n"
        b"helloEXTRA"
    ),
    expected_body_received=5,
)

print("Content-Length extra bytes test passed.")

# Two pipelined HTTP requests on one TCP connection.
def check_http_keepalive():
    process = subprocess.Popen(
        [binary("http_keepalive")],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    try:
        ready, _, _ = select.select([process.stdout], [], [], 5)
        assert ready, "Keep-Alive startup timeout"

        line = process.stdout.readline()
        match = re.fullmatch(
            r"listening on 127\.0\.0\.1:(\d+)\n", line
        )
        assert match, line

        request = (
            b"GET /hello HTTP/1.1\r\n"
            b"Host: localhost\r\n\r\n"
            b"GET /unknown HTTP/1.1\r\n"
            b"Host: localhost\r\n\r\n"
        )

        with socket.create_connection(
            ("127.0.0.1", int(match.group(1))), timeout=5
        ) as client:
            client.settimeout(5)
            client.sendall(request)

            chunks = []
            while True:
                data = client.recv(4096)
                if not data:
                    break
                chunks.append(data)

        log, _ = process.communicate(timeout=5)
        assert process.returncode == 0, log

        response = b"".join(chunks)

        headers1, sep1, rest = response.partition(b"\r\n\r\n")
        assert sep1
        assert headers1.startswith(b"HTTP/1.1 200 OK")
        assert b"Connection: keep-alive" in headers1
        assert rest.startswith(b"hello")

        headers2, sep2, body2 = rest[5:].partition(b"\r\n\r\n")
        assert sep2
        assert headers2.startswith(b"HTTP/1.1 404 Not Found")
        assert b"Connection: close" in headers2
        assert body2 == b"not found"

        assert "path=/hello" in log
        assert "path=/unknown" in log

    finally:
        if process.poll() is None:
            process.kill()
            process.communicate(timeout=5)


check_http_keepalive()
print("Two-request HTTP Keep-Alive passed.")


# Client-requested HTTP connection closure.
def check_http_client_close(connection_value):
    process = subprocess.Popen(
        [binary("http_keepalive")],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    try:
        ready, _, _ = select.select([process.stdout], [], [], 5)
        assert ready, "HTTP server startup timeout"

        line = process.stdout.readline()
        match = re.fullmatch(
            r"listening on 127\.0\.0\.1:(\d+)\n", line
        )
        assert match, line

        request = (
            b"GET /hello HTTP/1.1\r\n"
            b"Host: localhost\r\n"
            b"Connection: " + connection_value + b"\r\n"
            b"\r\n"
        )

        with socket.create_connection(
            ("127.0.0.1", int(match.group(1))), timeout=5
        ) as client:
            client.settimeout(5)
            client.sendall(request)

            chunks = []
            while True:
                chunk = client.recv(4096)
                if not chunk:
                    break
                chunks.append(chunk)

        output, _ = process.communicate(timeout=5)
        assert process.returncode == 0, output

        response = b"".join(chunks)
        headers, separator, body = response.partition(b"\r\n\r\n")

        assert separator
        assert headers.startswith(b"HTTP/1.1 200 OK")
        assert b"Connection: close" in headers.split(b"\r\n")
        assert body == b"hello"
        assert "path=/hello" in output

    finally:
        if process.poll() is None:
            process.kill()
            process.communicate(timeout=5)


check_http_client_close(b"close")
print("Client Connection: close passed.")

check_http_client_close(b"keep-alive, CLOSE")
print("Connection token parsing passed.")


# Send the second request only after receiving the first response.
def check_sequential_http_keepalive():
    process = subprocess.Popen(
        [binary("http_keepalive")],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    def read_response(reader):
        status = reader.readline().rstrip(b"\r\n")
        assert status.startswith(b"HTTP/1.1 "), status

        headers = {}
        while True:
            line = reader.readline()
            assert line, "Unexpected EOF in response headers"

            if line == b"\r\n":
                break

            name, value = line.split(b":", 1)
            headers[name.lower()] = value.strip()

        length = int(headers[b"content-length"])
        body = reader.read(length)
        assert len(body) == length

        return status, headers, body

    try:
        ready, _, _ = select.select([process.stdout], [], [], 5)
        assert ready, "HTTP server startup timeout"

        line = process.stdout.readline()
        match = re.fullmatch(
            r"listening on 127\.0\.0\.1:(\d+)\n", line
        )
        assert match, line

        with socket.create_connection(
            ("127.0.0.1", int(match.group(1))), timeout=5
        ) as client:
            client.settimeout(5)

            with client.makefile("rb") as reader:
                client.sendall(
                    b"GET /hello HTTP/1.1\r\n"
                    b"Host: localhost\r\n\r\n"
                )

                status1, headers1, body1 = read_response(reader)

                assert status1 == b"HTTP/1.1 200 OK"
                assert headers1[b"connection"] == b"keep-alive"
                assert body1 == b"hello"

                # First response received; now send second request.
                client.sendall(
                    b"GET /unknown HTTP/1.1\r\n"
                    b"Host: localhost\r\n\r\n"
                )

                status2, headers2, body2 = read_response(reader)

                assert status2 == b"HTTP/1.1 404 Not Found"
                assert headers2[b"connection"] == b"close"
                assert body2 == b"not found"

                assert reader.read(1) == b""

        output, _ = process.communicate(timeout=5)
        assert process.returncode == 0, output
        assert "path=/hello" in output
        assert "path=/unknown" in output

    finally:
        if process.poll() is None:
            process.kill()
            process.communicate(timeout=5)


check_sequential_http_keepalive()
print("Sequential HTTP Keep-Alive passed.")


# An idle Keep-Alive connection should close without another response.
def check_http_keepalive_idle_timeout():
    import time

    process = subprocess.Popen(
        [binary("http_keepalive")],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    try:
        ready, _, _ = select.select([process.stdout], [], [], 5)
        assert ready, "HTTP server startup timeout"

        line = process.stdout.readline()
        match = re.fullmatch(
            r"listening on 127\.0\.0\.1:(\d+)\n", line
        )
        assert match, line

        with socket.create_connection(
            ("127.0.0.1", int(match.group(1))), timeout=5
        ) as client:
            client.settimeout(6)

            with client.makefile("rb") as reader:
                client.sendall(
                    b"GET /hello HTTP/1.1\r\n"
                    b"Host: localhost\r\n\r\n"
                )

                status = reader.readline().strip()
                assert status == b"HTTP/1.1 200 OK"

                headers = {}
                while True:
                    line = reader.readline()
                    assert line, "Unexpected EOF in headers"
                    if line == b"\r\n":
                        break

                    name, value = line.split(b":", 1)
                    headers[name.lower()] = value.strip()

                assert headers[b"connection"] == b"keep-alive"
                length = int(headers[b"content-length"])
                assert reader.read(length) == b"hello"

                start = time.monotonic()

                # Do not send a second request.
                assert reader.read(1) == b""

                elapsed = time.monotonic() - start
                assert elapsed < 5.5, elapsed

        output, _ = process.communicate(timeout=5)
        assert process.returncode == 0, output

    finally:
        if process.poll() is None:
            process.kill()
            process.communicate(timeout=5)


check_http_keepalive_idle_timeout()
print("HTTP Keep-Alive idle timeout passed.")

print("Network smoke tests passed.")
