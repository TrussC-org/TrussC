# SysEx tests

This console addon harness uses the pinned upstream libremidi decoder and the
retained ALSA output patch (#992). The default run needs no MIDI device or display.
It checks complete and fragmented SysEx above 1 MiB through both decoder entry
points, byte-for-byte delivery, start timestamps and subsequent normal messages.
Input cap, overflow-callback and patched WinMM/ALSA input tests were removed with
R1–R3 in #1098.

Build through the addon path from the repository root:

```sh
tools/bin/trusscli update -p addons/tcxMidi/tests-sysex --tc-root "$PWD" --ide cmake
/path/to/tcbuild.sh cmake -S addons/tcxMidi/tests-sysex -B addons/tcxMidi/tests-sysex/build-linux -DCMAKE_BUILD_TYPE=Release
/path/to/tcbuild.sh cmake --build addons/tcxMidi/tests-sysex/build-linux --config Release
addons/tcxMidi/tests-sysex/bin/tests-sysex
```

For offline builds, copy libremidi v5.4.3 into a private directory, apply
`cmake/PatchLibremidi.cmake`, and configure with
`-DFETCHCONTENT_SOURCE_DIR_LIBREMIDI=path/to/private/copy`.
Only ALSA output is patched; verification also requires the four restored input
files to match upstream. Normal reconfiguration uses a fresh R4 source location.
Stale overrides/caches fail with the differing file and clean-source instructions.

```sh
/path/to/tcbuild.sh python3 addons/tcxMidi/tests-sysex/test_patch.py path/to/pristine/libremidi tmp/patch-checks
```

The offline patch tests exercise FetchContent, source overrides, reused caches,
LF/CRLF scripts, stale input and unknown output sources, read-only verification and idempotence.
They compare all other files byte-for-byte with the pristine source.

## ALSA large-output regression (#992)

After building the harness on Linux, run:

```sh
addons/tcxMidi/tests-sysex/bin/tests-sysex --alsa-roundtrip
addons/tcxMidi/tests-sysex/bin/tests-sysex --alsa-roundtrip --require-device
```

This opens two real ALSA clients and sends patterned 1,048,577-byte and
1,048,576-byte SysEx through `MidiOut::sendSysex`. A raw sequencer receiver
checks all 2,097,153 bytes in order, with a 200-cell input pool. The raw
subscription avoids depending on observer virtual-port enumeration (#982). No external MIDI
hardware, MIDI Through device, display, or root privileges are needed, but the
user must be able to open `/dev/snd/seq`. A missing/inaccessible sequencer prints
`SKIP: ALSA sequencer cannot be opened: ...` and exits 0. `--require-device`
changes that result to `FAIL` / exit 1; all failures after opening fail normally.
On a successful Pi run expect (exit 0):

```text
PASS: ALSA SysEx roundtrip sizes=1048577,1048576 sent=1 received=2097153 byte_match=1
```

Native MIDI delivery on other backends requires a separate device or loopback check.

The output host test uses the production patched ALSA backend and the real
`snd_midi_event` encoder, replacing only the dynamic-library symbol boundary
with fake sequencer calls and counting sleep requests. It needs ALSA development
headers, a C++20 compiler
and libasound, but no device or display. Run through the build wrapper:

```sh
python3 addons/tcxMidi/tests-sysex/test_output.py path/to/patched/libremidi /tmp/new-output-check
```

It checks byte-for-byte large-message encoding, draining every event, temporary
EAGAIN/EINTR recovery without duplicate chunks, fatal drain failure, exact retry
exhaustion, pending-buffer cleanup, recovery with a fresh F0, and calling-thread
pacing requests without wall-clock assertions. The output must make no input
calls on supplied shared duplex or
output-only contexts, including when an attempted read would return EBADF.
The driver overlay counts both event-input and input-pending calls.
The baseline must pass; mutations must fail at their intended assertions:
remove the entire output replacement, enlarge the encoder chunk, ignore fatal
drain errors, change the retry bound, remove pacing, add an event-input read,
and add an input-pending read. Mutation sources and binaries stay in the
supplied scratch directory. Sleep requests are counted at the sleep boundary;
the test does not measure
kernel delivery, elapsed time or receiver scheduling.
