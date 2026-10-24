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
    winmm = "libremidi/backends/winmm/midi_in.hpp"
    loss = "tcxMidiSysex.h"
    original = {p: (source / "include" / p).read_text(encoding="utf-8") for p in (decoder, alsa, winmm)}
    original[loss] = (root / "addons/tcxMidi/src" / loss).read_text(encoding="utf-8")
    initial = "if (message.empty() && bytes.size() > sysex_max_bytes)"
    continuation = "if (!message.empty() && bytes.size() > sysex_max_bytes - message.size())"
    # The five event-admission/scratch mutations from the review now exercise
    # borrowed routing: resize, reserve, truncation, omitted feed, false rejection.
    # Initial/cumulative logical admission and reservation remain independent.
    borrowed = "          m_processing.on_bytes("
    event_span = "{static_cast<const uint8_t*>(ev.data.ext.ptr), ev.data.ext.len}"
    variants = {
        "baseline": None,
        "winmm_silent_longerror": (winmm,
            "        if (self.configuration.on_sysex_overflow)\n"
            "          self.configuration.on_sysex_overflow(0);", ""),
        "winmm_silent_requeue": (winmm,
            "          if (self.configuration.on_sysex_overflow)\n"
            "            self.configuration.on_sysex_overflow(0);", ""),
        "stray_eox_delivery": (decoder, "short_bytes[0] != 0xF7\n            && ", ""),
        "global_limit_loosen": (decoder, "sysex_max_bytes = 1'048'576;", "sysex_max_bytes = 1'048'577;"),
        "global_limit_stricter": (decoder, "sysex_max_bytes = 1'048'576;", "sysex_max_bytes = 1'048'575;"),
        "initial_remove": (decoder, initial, "if (false)"),
        "initial_loosen": (decoder, initial, initial.replace("sysex_max_bytes)", "sysex_max_bytes + 1)")),
        "initial_stricter": (decoder, initial, initial.replace(" > ", " >= ")),
        "continuation_remove": (decoder, continuation, "if (false)"),
        "continuation_loosen": (decoder, continuation, continuation.replace("sysex_max_bytes -", "sysex_max_bytes + 1 -")),
        "continuation_stricter": (decoder, continuation, continuation.replace(" > ", " >= ")),
        "capacity_remove": (decoder, "if (needed > message.bytes.capacity())", "if (false)"),
        "capacity_no_clamp": (decoder, "std::min(sysex_max_bytes,\n          std::max(needed, message.bytes.capacity() * 2))", "std::max(needed, message.bytes.capacity() * 2)"),
        "capacity_loosen": (decoder, "std::min(sysex_max_bytes,", "std::min(sysex_max_bytes + 1,"),
        "capacity_stricter": (decoder, "std::min(sysex_max_bytes,", "std::min(sysex_max_bytes - 1,"),
        "alsa_scratch_resize": (alsa, borrowed, "          decoding_buffer.resize(ev.data.ext.len);\n" + borrowed),
        "alsa_scratch_reserve": (alsa, borrowed, "          decoding_buffer.reserve(ev.data.ext.len);\n" + borrowed),
        "alsa_span_truncate": (alsa, event_span, "{static_cast<const uint8_t*>(ev.data.ext.ptr), std::min<std::size_t>(ev.data.ext.len, midi1::input_state_machine::sysex_max_bytes)}"),
        "alsa_feed_remove": (alsa, borrowed, "          if (false) m_processing.on_bytes("),
        "alsa_event_rejection": (alsa, borrowed,
                                 "          if (ev.data.ext.len > midi1::input_state_machine::sysex_max_bytes) {\n"
                                 "            if (configuration.on_sysex_overflow) configuration.on_sysex_overflow(ev.data.ext.len);\n"
                                 "            return 0;\n          }\n" + borrowed),
        "discard_resync": (decoder, "m_state = discard_sysex;", "m_state = main;"),
        "partial_channel": (decoder, "    }\n  }\n\n  void reject_sysex", "    }\n    short_size = 0;\n  }\n\n  void reject_sysex"),
        # Remove noexcept only in this mutation so the allocation test can catch.
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
        subprocess.run(["cmake", f"-DSOURCE_FILE={directory / 'include' / winmm}",
                        f"-DOUTPUT_FILE={directory / 'include/winmm_callback.inc'}", "-P",
                        str(root / "addons/tcxMidi/tests-sysex/ExtractWinmmCallback.cmake")], check=True)
        binary = directory / "test"
        command = ["/usr/bin/c++", "-std=c++20", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror",
                   "-Wno-error=stringop-overflow", "-DLIBREMIDI_ALSA", "-DLIBREMIDI_NO_BOOST",
                   "-DSOKOL_GLCORE", "-DTC_VERSION_LINKED", "-I" + str(directory / "include"),
                   *includes, *[str(root / "addons/tcxMidi/tests-sysex/src" / file)
                                for file in ("main.cpp", "device.cpp", "allocation.cpp", "winmm.cpp")],
                   "-o", str(binary), *libraries]
        (directory / "command.txt").write_text(shlex.join(command), encoding="utf-8")
        result = subprocess.run(command, cwd=build, capture_output=True, text=True, encoding="utf-8")
        (directory / "build.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        result.check_returncode()
        result = subprocess.run([str(binary)], capture_output=True, text=True, encoding="utf-8")
        (directory / "result.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        print(f"{name}: exit={result.returncode} {result.stdout.strip()}", flush=True)
        if result.returncode != (0 if change is None else 1):
            raise RuntimeError(name + " failed mutation check")
        expected_failure = {
            "winmm_silent_longerror": "WinMM LONGERROR counts one loss per event",
            "winmm_silent_requeue": "WinMM failed requeue counts one loss per event",
        }.get(name)
        if expected_failure and "FAIL: " + expected_failure not in result.stderr:
            raise RuntimeError(name + " failed for an unrelated reason")


if __name__ == "__main__":
    main()
