#!/usr/bin/env python3
"""Offline checks. Usage: test_patch.py PRISTINE_LIBREMIDI WORK_DIR [--script-only].

Run through the build resource wrapper. WORK_DIR must be a new scratch directory.
The tarball is reconstructed from local source, not a replacement upstream pin.
--script-only tests the patch with CMake 3.20 (libremidi's build needs 3.22).
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tarfile


PATCHED_HASHES = {
    "include/libremidi/detail/midi_stream_decoder.hpp":
        "b919e33aba675e7286bde4e437831628fce9f062c7d8eec362214b2efff041c5",
    "include/libremidi/input_configuration.hpp":
        "91891013773211537e9397e4cd4ac994fe3cb400b0eea80f19dc8f5575d426ba",
    "include/libremidi/backends/alsa_seq/midi_in.hpp":
        "316f03a84f76c59a2629b863d656789b0af9821500fe4ce852e038417d6e895d",
}


def snapshot(root):
    return {str(path.relative_to(root)): (path.read_bytes(), path.stat().st_mtime_ns)
            for path in root.rglob("*") if path.is_file()}


def patched_bytes(root):
    result = {path: (root / path).read_bytes() for path in PATCHED_HASHES}
    for path, data in result.items():
        assert b"\r" not in data, path
        assert hashlib.sha256(data).hexdigest() == PATCHED_HASHES[path], path
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pristine", type=Path)
    parser.add_argument("work", type=Path)
    parser.add_argument("--script-only", action="store_true")
    args = parser.parse_args()
    pristine, work = args.pristine.resolve(), args.work.resolve()
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

    if not args.script_only:
        configure("archive_patch_command")
        run("archive_reconfigure", ["cmake", "-S", str(work), "-B", str(work / "archive_patch_command")])
    override = work / "override"
    shutil.copytree(pristine, override)
    if not args.script_only:
        configure("unpatched_override", f"-DFETCHCONTENT_SOURCE_DIR_LIBREMIDI={override}", succeeds=False)
    script = private_addon / "cmake/PatchLibremidi.cmake"
    # Bytes APIs avoid Python's native newline conversion on Windows.
    script_lf = script.read_bytes().replace(b"\r\n", b"\n")
    script.write_bytes(script_lf)
    script_crlf = work / "PatchLibremidi-crlf.cmake"
    script_crlf.write_bytes(script_lf.replace(b"\n", b"\r\n"))

    def patch(label, root, patch_script=script, verify=False, succeeds=True):
        run(label, ["cmake", f"-DLIBREMIDI_SOURCE_DIR={root}",
                    f"-DVERIFY_ONLY={'ON' if verify else 'OFF'}", "-P", str(patch_script)], succeeds)

    run("explicit_private_patch", ["cmake", f"-DLIBREMIDI_SOURCE_DIR={override}", "-P", str(script)])
    expected = patched_bytes(override)
    run("idempotent_patch", ["cmake", f"-DLIBREMIDI_SOURCE_DIR={override}", "-P", str(script)])
    assert patched_bytes(override) == expected
    crlf_output = work / "crlf_script_output"
    shutil.copytree(pristine, crlf_output)
    patch("crlf_script_patch", crlf_output, script_crlf)
    assert patched_bytes(crlf_output) == expected
    before = snapshot(crlf_output)
    patch("crlf_script_verify_only", crlf_output, script_crlf, verify=True)
    assert snapshot(crlf_output) == before, "VERIFY_ONLY wrote patched sources"
    patch("crlf_script_idempotent", crlf_output, script_crlf)
    assert snapshot(crlf_output) == before, "idempotent patch wrote sources"

    # Reject CRLF as unknown bytes, even if only the last checked file differs.
    # Test both entry points and both script encodings, including pristine input.
    for index, changed_path in enumerate([None, *PATCHED_HASHES]):
        rejected = work / f"rejected_input_{index}"
        shutil.copytree(pristine, rejected)
        if changed_path:
            path = rejected / changed_path
            data = path.read_bytes()
            assert b"\r" not in data and b"\n" in data, changed_path
            path.write_bytes(data.replace(b"\n", b"\r\n"))
        before = snapshot(rejected)
        for encoding, candidate in (("lf", script), ("crlf", script_crlf)):
            for verify in ((True,) if changed_path is None else (False, True)):
                patch(f"reject_{index}_{encoding}_verify_{verify}", rejected, candidate,
                      verify=verify, succeeds=False)
                assert snapshot(rejected) == before, "rejected input was modified"

    # Remove the LF writer in a scratch CRLF script. On Windows this is the
    # actual old implementation. Elsewhere emulate only its CRT write-time
    # LF -> CRLF conversion, which a POSIX text stream cannot reproduce itself.
    mutant = script_lf.decode("utf-8")
    for index in range(3):
        fixed_write = (f'file(CONFIGURE OUTPUT "${{path{index}}}" CONTENT "${{contents}}" '
                       '@ONLY NEWLINE_STYLE UNIX)')
        assert mutant.count(fixed_write) == 1
        old_write = f'file(WRITE "${{path{index}}}" "${{contents}}")'
        if os.name != "nt":
            old_write = 'string(REPLACE "\\n" "\\r\\n" contents "${contents}")\n    ' + old_write
        mutant = mutant.replace(fixed_write, old_write)
    mutant_script = work / "PatchLibremidi-mutant-crlf.cmake"
    mutant_script.write_bytes(mutant.encode("utf-8").replace(b"\n", b"\r\n"))
    mutant_output = work / "mutant_output"
    shutil.copytree(pristine, mutant_output)
    patch("mutant_crlf_script_patch", mutant_output, mutant_script)
    for path, data in expected.items():
        actual = (mutant_output / path).read_bytes()
        assert actual == data.replace(b"\n", b"\r\n"), path
        assert hashlib.sha256(actual).hexdigest() != PATCHED_HASHES[path], path
    before = snapshot(mutant_output)
    patch("mutant_crlf_script_verify_rejected", mutant_output, verify=True, succeeds=False)
    assert snapshot(mutant_output) == before
    print("PASS: writer mutation detected (" +
          ("native Windows" if os.name == "nt" else "Windows text-mode emulation") + ")", flush=True)

    if args.script_only:
        return
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
