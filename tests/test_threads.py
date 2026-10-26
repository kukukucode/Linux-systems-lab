#!/usr/bin/env python3
"""Bounded smoke tests for well-defined pthread examples."""
import re
import subprocess
import sys
from pathlib import Path

build = Path(sys.argv[1] if len(sys.argv) > 1 else "build")


def run(name, timeout=25):
    result = subprocess.run(
        [str(build / name)],
        capture_output=True,
        text=True,
        timeout=timeout,
        check=True,
    )
    return result.stdout


basic = run("thread_basic")
assert "worker: hello from thread" in basic
assert "main: after pthread_join" in basic

mutex = run("thread_mutex")
assert "expected=2000000" in mutex
assert "actual=2000000" in mutex

pool = run("thread_pool")
tasks = re.findall(r"task=(\d+) result=(\d+)", pool)
assert len(tasks) == 10, pool
assert sorted((int(n), int(value)) for n, value in tasks) == [
    (n, n * n) for n in range(1, 11)
], pool
assert len(re.findall(r"worker \d+: exiting", pool)) == 3, pool

print("Threads smoke tests passed.")
