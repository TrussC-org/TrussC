#!/usr/bin/env python3
"""Check unchanged Metal backend bodies using GNU Objective-C mocks, no Apple SDK.
Run through tcbuild.sh. This does not check Metal driver behavior.
"""
from pathlib import Path
import importlib.util
import os
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
spec = importlib.util.spec_from_file_location("metal_extract", ROOT / "core/tests/metalUniformGrowth/test_backend.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
source = (ROOT / "core/include/sokol/sokol_gfx.h").read_text(encoding="utf-8")
scratch = Path(os.environ["TMPDIR"]) / "stencil-ref-metal"
scratch.mkdir(parents=True, exist_ok=True)
(scratch / "functions.inc").write_text("\n".join(module.extract(source, name) for name in
    ("_sg_mtl_apply_stencil_ref", "_sg_mtl_apply_pipeline")), encoding="utf-8")
subprocess.run(["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-x", "objective-c++",
    "-I", str(scratch), str(HERE / "metal_mock.mm"), "-l:libobjc.so.4", "-o", str(scratch / "mock")], check=True)
subprocess.run([str(scratch / "mock")], check=True)
