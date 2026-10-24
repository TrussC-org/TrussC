#!/usr/bin/env python3
"""Run the production ALSA output with a fake sequencer and real event encoder.

Usage (through tcbuild.sh): test_output.py PATCHED_LIBREMIDI NEW_SCRATCH_DIR
The only include overlay replaces dlsym at the driver boundary. No device needed.
"""
from pathlib import Path
import subprocess
import sys


def main():
    source, work = (Path(arg).resolve() for arg in sys.argv[1:])
    work.mkdir()
    loader = "libremidi/backends/linux/dylib_loader.hpp"
    overlay = work / "include" / loader
    overlay.parent.mkdir(parents=True)
    contents = (source / "include" / loader).read_text(encoding="utf-8")
    contents = contents.replace("return reinterpret_cast<T>(dlsym(impl, sym));",
                                "if (auto mock = alsaTestSymbol(sym)) return reinterpret_cast<T>(mock);\n"
                                "    return reinterpret_cast<T>(dlsym(impl, sym));")
    overlay.write_text(contents, encoding="utf-8")
    test = Path(__file__).parent / "host/alsa_output.cpp"

    def run(label, failure=None):
        binary = work / label
        command = ["c++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-DLIBREMIDI_NO_BOOST", "-I" + str(work / "include"),
                   "-I" + str(source / "include"), str(test), "-ldl", "-pthread", "-o", str(binary)]
        subprocess.run(command, check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, encoding="utf-8", timeout=45)
        (work / (label + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode != (1 if failure else 0):
            raise RuntimeError(f"{label}: unexpected exit {result.returncode}; see log")
        if failure and failure not in result.stderr:
            raise RuntimeError(f"{label}: mutation failed at an unrelated assertion; see log")
        print(f"PASS: {label}", flush=True)
        if not failure:
            print(result.stdout, end="", flush=True)

    run("baseline")
    output = source / "include/libremidi/backends/alsa_seq/midi_out.hpp"
    patched = output.read_text(encoding="utf-8")
    patch = (Path(__file__).parents[1] / "cmake/PatchLibremidi.cmake").read_text(encoding="utf-8")
    original = patch.split('if(hash4 STREQUAL original4)', 1)[1].split('string(REPLACE [==[', 1)[1].split(']==]', 1)[0]
    target = work / "include/libremidi/backends/alsa_seq/midi_out.hpp"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(original, encoding="utf-8")
    run("replacement_removed", failure="real encoder splits reported-size SysEx")
    for name, before, after, failure in [
        ("supplied_mode_changed", "    if (!drop_output)",
         '    snd.library.symbol<decltype(&::snd_seq_nonblock)>("snd_seq_nonblock")(this->seq, 1);\n'
         "    if (!drop_output)", "supplied blocking context mode unchanged after construction"),
        ("chunk_size_changed", "m_bufferSize{256}", "m_bufferSize{512}",
         "real encoder splits reported-size SysEx"),
        ("drain_error_ignored", "if (result != -EAGAIN && result != -EINTR)", "if (false)",
         "fatal drain discards pending bytes and stops"),
        ("drain_bound_changed", "if (attempt == 1000)", "if (attempt == 999)",
         "exhausts the exact retry bound"),
        ("pacing_removed", "if (ev.type == SND_SEQ_EVENT_SYSEX && offset < size)", "if (false)",
         "partial SysEx events are paced on the calling thread"),
        ("input_read_added", "    std::size_t offset = 0;",
         "    snd_seq_event_t* incoming{};\n"
         "    snd.seq.event_input(this->seq, &incoming);\n"
         "    std::size_t offset = 0;", "supplied output-only context never makes an input call"),
        ("input_pending_read_added", "    std::size_t offset = 0;",
         "    snd.seq.event_input_pending(this->seq, 1);\n"
         "    std::size_t offset = 0;", "shared duplex output never reads unrelated input"),
    ]:
        assert patched.count(before) == 1
        target.write_text(patched.replace(before, after), encoding="utf-8")
        run(name, failure=failure)


if __name__ == "__main__":
    main()
