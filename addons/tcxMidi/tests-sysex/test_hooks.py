#!/usr/bin/env python3
"""Remove one hook per scratch include overlay and require behavioral failures.

Run via tcbuild.sh after building tests-sysex:
  test_hooks.py PATCHED_LIBREMIDI BUILD_DIR NEW_SCRATCH_DIR
Uses the real test target's include/link settings, not the wrapper mock.
"""
from pathlib import Path
import shlex
import subprocess
import sys


def main():
    source, build, work = (Path(arg).resolve() for arg in sys.argv[1:])
    work.mkdir()
    root = Path(__file__).resolve().parents[3]
    flags = (build / "CMakeFiles/tests-sysex.dir/flags.make").read_text(encoding="utf-8")
    includes = shlex.split(next(line.split(" = ", 1)[1] for line in flags.splitlines()
                                if line.startswith("CXX_INCLUDES = ")))
    link = shlex.split((build / "CMakeFiles/tests-sysex.dir/link.txt").read_text(encoding="utf-8"))
    libraries = link[link.index("-o") + 2:]
    decoder = "libremidi/detail/midi_stream_decoder.hpp"
    alsa = "libremidi/backends/alsa_seq/midi_in.hpp"
    loss = "tcxMidiSysex.h"
    original = {p: (source / "include" / p).read_text(encoding="utf-8") for p in (decoder, alsa)}
    original[loss] = (root / "addons/tcxMidi/src" / loss).read_text(encoding="utf-8")
    variants = {
        "baseline": None,
        "initial_assign": (decoder, "if (message.empty() && bytes.size() > sysex_max_bytes)", "if (false)"),
        "continuation_insert": (decoder, "if (!message.empty() && bytes.size() > sysex_max_bytes - message.size())", "if (false)"),
        "capacity": (decoder, "if (needed > message.bytes.capacity())", "if (false)"),
        "alsa_scratch": (alsa, "if (ev.data.ext.len > midi1::input_state_machine::sysex_max_bytes)", "if (false)"),
        "alsa_capacity": (alsa, "decoding_buffer.reserve(ev.data.ext.len);", ""),
        "discard_resync": (decoder, "m_state = discard_sysex;", "m_state = main;"),
        "alsa_rejection": (alsa, "m_processing.on_oversized_sysex(", "m_processing.on_bytes("),
        "partial_channel": (decoder, "    }\n  }\n\n  void reject_sysex", "    }\n    short_size = 0;\n  }\n\n  void reject_sysex"),
        # Reintroduce scheduling in the loss callback; fault injection must detect
        # allocation before the pending flag can be set (remove noexcept so the
        # test can catch bad_alloc and check the lost warning).
        "backend_scheduling": (loss, "        if (discarded.fetch_add", "        tc::internal::mainThreadQueue().send([self = std::shared_ptr<size_t>{}, needed] { (void)self; (void)needed; });\n        if (discarded.fetch_add"),
        "report_post": (loss, "            post(weak_from_this().lock());", ""),
        "report_once": (loss, "if (discarded.fetch_add(1, std::memory_order_relaxed) == 0)",
                        "if ((discarded.fetch_add(1, std::memory_order_relaxed), true))"),
    }
    for name, change in variants.items():
        directory = work / name
        directory.mkdir()
        for path, contents in original.items():
            if change and path == change[0]:
                assert contents.count(change[1]) == 1, name
                contents = contents.replace(change[1], change[2])
                if name == "backend_scheduling":
                    contents = contents.replace("void count(size_t needed) noexcept", "void count(size_t needed)")
            target = directory / "include" / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(contents, encoding="utf-8")
        # Quote-includes inside tcxMidiIn.h must see the loss overlay too.
        (directory / "include/tcxMidiIn.h").write_text(
            (root / "addons/tcxMidi/src/tcxMidiIn.h").read_text(encoding="utf-8"), encoding="utf-8")
        binary = directory / "test"
        command = ["/usr/bin/c++", "-std=c++20", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror",
                   "-Wno-error=stringop-overflow", "-DLIBREMIDI_ALSA", "-DLIBREMIDI_NO_BOOST",
                   "-DSOKOL_GLCORE", "-DTC_VERSION_LINKED", "-I" + str(directory / "include"),
                   *includes, *[str(root / "addons/tcxMidi/tests-sysex/src" / file)
                                for file in ("main.cpp", "device.cpp", "allocation.cpp")],
                   "-o", str(binary), *libraries]
        (directory / "command.txt").write_text(shlex.join(command), encoding="utf-8")
        subprocess.run(command, cwd=build, check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, encoding="utf-8")
        (directory / "result.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        print(f"{name}: exit={result.returncode} {result.stdout.strip()}", flush=True)
        if (result.returncode == 0) != (change is None):
            raise RuntimeError(name + " failed mutation check")


if __name__ == "__main__":
    main()
