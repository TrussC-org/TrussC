# TrussC modifications to libremidi

**Upstream base:** libremidi v5.4.3, SHA256-pinned release archive in
`CMakeLists.txt`. The upstream BSD 2-Clause / RtMidi MIT notices are unchanged.

`cmake/PatchLibremidi.cmake` applies exact replacements with CMake alone (no git,
patch utility or Python required). It checks the four upstream input SHA256
hashes and the ALSA output original/patched hash before writing. Unknown input or output fails configuration. A second,
read-only check after FetchContent covers source overrides, previously populated
sources and shared FetchContent caches. Output is explicitly LF on every host,
including CRLF script checkouts, while CRLF source files remain unknown input
and are rejected before any write. For an offline override, patch a private
copy explicitly before configuring:

```sh
cmake -DLIBREMIDI_SOURCE_DIR=path/to/private/libremidi -P addons/tcxMidi/cmake/PatchLibremidi.cmake
```

## Changes

- `backends/alsa_seq/midi_out.hpp` (#992): keep the encoder at 256 bytes and
  send consecutive partial SysEx events without a logical output size limit.
  Drain each event before encoding the next. Nonblocking drain retries are
  limited to 1000, with 1 ms between attempts; exhaustion discards pending
  output and returns `timed_out`. Other driver errors fail immediately and
  discard pending bytes. A prefix may have arrived when a send fails.
  Pace partial SysEx events by 1 ms to allow readers to release pool cells.
  Both pacing and draining run synchronously on the calling thread: nominally
  about 256 KB/s (roughly 80 times DIN MIDI), so a 1 MB dump takes about 4 s.
  Calling `sendSysex` from `update()` or `draw()` stalls that thread for the
  entire send; drain retries can add up to 1000 waits per event, and scheduling
  can delay each wait. With immediate mock drains and the real ALSA codec,
  the 1,048,577-byte host send measured **4,369 ms** (4,097 events); real-device
  timing can vary.
  Success means the sequencer accepted the send; it does not acknowledge
  reception. A receiver whose input pool overflows loses data on its side,
  as with other MIDI backends. The output path never reads the sequencer handle.
  Private clients are opened nonblocking; supplied contexts retain their mode.
  A blocking supplied handle can block in the kernel drain beyond the retry
  bound. Supplied contexts concern direct libremidi configuration only; TrussC's
  own MidiOut never supplies a context.
  Other native/Web output backends use upstream behavior.

Finding only (send side unchanged): WinMM `send_message` waits without a bound
in `while (midiOutUnprepareHeader(...) == MIDIERR_STILLPLAYING) Sleep(1);` and
returns on `midiOutLongMsg` failure without unpreparing the header.

## Upstream input restored (#1098)

R1–R3 have been removed: the shared MIDI 1 decoder, input configuration,
ALSA input and WinMM input are byte-for-byte libremidi v5.4.3. Native input no
longer has TrussC's 1 MiB assembly cap, overflow callback, assembly-loss Warning,
or modified parsing/resynchronization and backend loss handling. Polling queue
limits remain independent. WebMIDI still requests access without SysEx.
Only R4 (ALSA chunked output, #992) is patched. FetchContent uses a new R4 source
location so ordinary reconfiguration extracts fresh sources when upgrading from
R1–R3. Stale input in private overrides or reused caches fails verification with
the differing file and instructions to use clean v5.4.3 sources; the output-only
patch does not undo old input edits.
The release archive SHA256 remains
`a1c369c6aab01c80f1506345d1c099a167b330d1d59159aa7583f7b0b9f966b9`.

## Updating or dropping the patch

1. Check whether a new upstream release provides the retained chunked ALSA
   output behavior, including draining, pacing and supplied-context handling.
2. Rebase only the output replacement when needed and update its hashes;
   unknown ALSA output content must still fail configuration.
3. Run `tests-sysex`, `tests`, `test_patch.py`, `test_output.py` and the core
   sweep. The patch tests also check that every other upstream file is unchanged.
   Use `tests-sysex/README.md` for the optional ALSA roundtrip check.
4. Update this ledger and `docs/LICENSE.md`. No upstream PR is planned for #1098.
