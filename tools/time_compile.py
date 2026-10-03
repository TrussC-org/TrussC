#!/usr/bin/env python3
"""Compiler launcher: report per-TU wall time without changing cache keys."""

import pathlib
import subprocess
import sys
import time


def main():
    command = sys.argv[1:]
    source = next((arg for arg in command
                   if pathlib.Path(arg).suffix in (".c", ".cpp", ".cc", ".cxx", ".m", ".mm")),
                  "unknown source")
    start = time.monotonic()
    result = subprocess.run(command)
    print(f"Compile time: {time.monotonic() - start:.2f}s {source}",
          file=sys.stderr, flush=True)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
