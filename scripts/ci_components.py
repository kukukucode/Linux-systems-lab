#!/usr/bin/env python3
"""Emit a JSON list of affected CI components from changed paths."""
import json
import sys

components = [
    "core", "process", "memory", "io", "ipc", "mini-shell",
    "threads", "network", "storage", "indexing", "security",
]

def affected(paths):
    chosen = set()
    for path in paths:
        if path == "Makefile" or path.startswith(".github/") or path.startswith("scripts/"):
            return components
        if path == "src/hello.c":
            chosen.add("core")
        elif path.startswith("src/mini_shell/") or path == "tests/test_mini_shell.py":
            chosen.add("mini-shell")
        elif path.startswith("src/"):
            name = path.split("/", 2)[1]
            if name in components:
                chosen.add(name)
            else:
                return components
        elif path.startswith("tests/test_") and path.endswith(".py"):
            name = path[len("tests/test_"):-len(".py")].replace("_", "-")
            if name in components:
                chosen.add(name)
            else:
                return components
        elif path.startswith(("docs/", "README", ".gitignore", "LICENSE")):
            continue
        else:
            return components
    return [name for name in components if name in chosen]

if __name__ == "__main__":
    paths = [line.strip() for line in sys.stdin if line.strip()]
    print(json.dumps(affected(paths)))
