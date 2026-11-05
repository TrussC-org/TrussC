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
    import re
    match = re.search(r"(?:_SOKOL_PRIVATE|SOKOL_API_IMPL) [^;{\n]*\b" + re.escape(name) + r"\([^;{]*\)\s*\{", source)
    if not match:
        return ""
    start = match.start()
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def main():
    source = (Path(sys.argv[1]) if len(sys.argv) > 1 else HEADER).read_text(encoding="utf-8")
    names = ["_sg_mtl_bind_uniform_buffers", "_sg_roundup", "_sg_roundup_pow2", "_sg_mtl_resize_uniform_buffer",
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
        result = subprocess.run([str(path / "mock")]).returncode
        # Exercise the real swapchain depth/stencil adapter, including its defaults.
        begin_pass = extract(source, "_sg_mtl_begin_render_pass")
        start = begin_pass.index("        // optional depth-stencil texture")
        opening = begin_pass.index("{", start)
        end, depth = opening + 1, 1
        while depth:
            depth += (begin_pass[end] == "{") - (begin_pass[end] == "}")
            end += 1
        adapter = begin_pass[start:end]
        (path / "store.inc").write_text(
            extract(source, "_sg_mtl_load_action") + extract(source, "_sg_mtl_store_action") +
            "\nstatic void apply(const Swapchain* swapchain, const Action* action, Descriptor& pass_desc) {\n" +
            adapter + "\n}\n", encoding="utf-8")
        (path / "store.mm").write_text((HERE / "store_mock.mm").read_text(encoding="utf-8"), encoding="utf-8")
        run_build(["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                   "-Wno-arc-bridge-casts-disallowed-in-nonarc", "-x", "objective-c++",
                   str(path / "store.mm"), "-l:libobjc.so.4", "-o", str(path / "store")], check=True)
        result |= subprocess.run([str(path / "store")]).returncode
        seams = ["_sg_mtl_finish_blit_cmd_encoder", "sg_tc_mtl_flush", "sg_tc_submit_prefix", "sg_tc_mtl_command_buffer"]
        # The old base predates the persistent encoder; uniform coverage still runs.
        if extract(source, seams[0]):
            (path / "seams.inc").write_text("\n".join(extract(source, name) for name in seams), encoding="utf-8")
            (path / "seam.mm").write_text((HERE / "seam_mock.mm").read_text(encoding="utf-8"), encoding="utf-8")
            run_build(["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unused-function", "-Wno-arc-bridge-casts-disallowed-in-nonarc", "-x", "objective-c++",
                       str(path / "seam.mm"), "-l:libobjc.so.4", "-o", str(path / "seam")], check=True)
            result |= subprocess.run([str(path / "seam")]).returncode
        return result


if __name__ == "__main__":
    sys.exit(main())
