#!/usr/bin/env python3
"""Offline configure checks. Usage: test_patch.py PRISTINE_LIBREMIDI WORK_DIR.

Run through the build resource wrapper. WORK_DIR must be a new scratch directory.
The tarball is reconstructed from local source, not a replacement upstream pin.
"""
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile


def main():
    pristine, work = (Path(arg).resolve() for arg in sys.argv[1:])
    work.mkdir()
    addon = Path(__file__).resolve().parents[1]
    private_addon = work / "addon"
    private_addon.mkdir()
    shutil.copytree(addon / "cmake", private_addon / "cmake")
    (private_addon / "src").mkdir()
    archive = work / "libremidi.tar.gz"
    with tarfile.open(archive, "w:gz") as output:
        output.add(pristine, arcname="libremidi-5.4.3")
    source = (addon / "CMakeLists.txt").read_text(encoding="utf-8")
    source = source.replace(
        "https://github.com/celtera/libremidi/archive/refs/tags/v5.4.3.tar.gz",
        archive.as_uri()).replace(
        "a1c369c6aab01c80f1506345d1c099a167b330d1d59159aa7583f7b0b9f966b9",
        hashlib.sha256(archive.read_bytes()).hexdigest())
    (private_addon / "CMakeLists.txt").write_text(source, encoding="utf-8")
    (work / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.20)\nproject(patch_checks LANGUAGES C CXX)\n'
        'add_library(TrussC INTERFACE)\nadd_subdirectory(addon)\n', encoding="utf-8")

    def run(label, command, succeeds=True):
        result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
        (work / (label + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        if (result.returncode == 0) != succeeds:
            raise RuntimeError(f"{label}: unexpected result {result.returncode}; see log")
        if not succeeds and "checked libremidi 5.4.3 SysEx patch" not in result.stdout + result.stderr:
            raise RuntimeError(f"{label}: failed for an unrelated reason")
        print(f"PASS: {label}", flush=True)

    def configure(label, *options, succeeds=True):
        run(label, ["cmake", "-S", str(work), "-B", str(work / label),
                    "-DFETCHCONTENT_UPDATES_DISCONNECTED=ON", *options], succeeds)

    configure("archive_patch_command")
    run("archive_reconfigure", ["cmake", "-S", str(work), "-B", str(work / "archive_patch_command")])
    override = work / "override"
    shutil.copytree(pristine, override)
    configure("unpatched_override", f"-DFETCHCONTENT_SOURCE_DIR_LIBREMIDI={override}", succeeds=False)
    script = private_addon / "cmake/PatchLibremidi.cmake"
    run("explicit_private_patch", ["cmake", f"-DLIBREMIDI_SOURCE_DIR={override}", "-P", str(script)])
    run("idempotent_patch", ["cmake", f"-DLIBREMIDI_SOURCE_DIR={override}", "-P", str(script)])
    configure("patched_override", f"-DFETCHCONTENT_SOURCE_DIR_LIBREMIDI={override}")
    decoder = override / "include/libremidi/detail/midi_stream_decoder.hpp"
    with decoder.open("a", encoding="utf-8") as output:
        output.write("\n// Unknown local edit.\n")
    configure("modified_override", f"-DFETCHCONTENT_SOURCE_DIR_LIBREMIDI={override}", succeeds=False)
    cache = work / "reused_cache"
    shutil.copytree(pristine, cache / "libremidi-src")
    configure("unpatched_reused_cache", f"-DFETCHCONTENT_BASE_DIR={cache}",
              "-DFETCHCONTENT_FULLY_DISCONNECTED=ON", succeeds=False)
    run("private_cache_patch", ["cmake", f"-DLIBREMIDI_SOURCE_DIR={cache / 'libremidi-src'}", "-P", str(script)])
    configure("patched_reused_cache", f"-DFETCHCONTENT_BASE_DIR={cache}",
              "-DFETCHCONTENT_FULLY_DISCONNECTED=ON")


if __name__ == "__main__":
    main()
