#!/usr/bin/env python3
"""Execute the actual Metal upload functions against GNU Objective-C mocks.

Linux only, no Apple SDK/GPU. Run through the build wrapper. Function bodies
are extracted unchanged from the selected header, never reimplemented here.
Optional header argument allows the same regression to run on the base.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from check_build_warnings import run_build

HERE = Path(__file__).resolve().parent
HEADER = HERE.parents[1] / "include/sokol/sokol_gfx.h"


def extract(source, name):
    start = source.find("_SOKOL_PRIVATE ", source.find(name + "(") - 32)
    if start < 0 or name not in source[start:source.find("{", start)]:
        return ""
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def main():
    source = (Path(sys.argv[1]) if len(sys.argv) > 1 else HEADER).read_text(encoding="utf-8")
    names = ["_sg_mtl_bind_uniform_buffers", "_sg_mtl_resize_uniform_buffer",
             "_sg_mtl_grow_uniform_buffer", "_sg_mtl_apply_uniforms"]
    functions = "\n".join(extract(source, name) for name in names)
    if extract(source, names[0]):
        functions += "\n#define HAS_PASS_BIND 1\n"
    fixture = (HERE / "backend_mock.mm").read_text(encoding="utf-8")
    scratch_root = Path(os.environ.get("TMPDIR", str(HERE / "build-mock")))
    scratch_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="metal-uniform-", dir=scratch_root) as scratch:
        path = Path(scratch)
        (path / "functions.inc").write_text(functions, encoding="utf-8")
        (path / "mock.mm").write_text(fixture, encoding="utf-8")
        run_build(["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-function", "-x", "objective-c++", str(path / "mock.mm"),
                        "-l:libobjc.so.4", "-o", str(path / "mock")], check=True)
        return subprocess.run([str(path / "mock")]).returncode


if __name__ == "__main__":
    sys.exit(main())
