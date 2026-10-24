# TrussC modifications to libremidi

**Upstream base:** libremidi v5.4.3, SHA256-pinned release archive in
`CMakeLists.txt`. The upstream BSD 2-Clause / RtMidi MIT notices are unchanged.

`cmake/PatchLibremidi.cmake` applies exact replacements with CMake alone (no git,
patch utility or Python required). It checks all five original/patched SHA256
hashes before writing. Unknown input or output fails configuration. A second,
read-only check after FetchContent covers source overrides, previously populated
sources and shared FetchContent caches. Output is explicitly LF on every host,
including CRLF script checkouts, while CRLF source files remain unknown input
and are rejected before any write. For an offline override, patch a private
copy explicitly before configuring:

```sh
cmake -DLIBREMIDI_SOURCE_DIR=path/to/private/libremidi -P addons/tcxMidi/cmake/PatchLibremidi.cmake
```

## Changes

- `detail/midi_stream_decoder.hpp`: cap MIDI 1 SysEx assembly at 1,048,576 bytes
  including F0/F7; check initial assignment and continuation admission before
  storage, and reserve bounded capacity before vector growth. Storage grows on
  demand. Drop an over-limit message whole, retain discard state through
  real-time messages, and resume at F7 or a new non-real-time status. Forward
  real-time messages using existing timing/sensing filters. Process a remainder
  in the same input span without recursive stack growth. Keep partial short
  messages and channel running status across input reads through both entries.
  Ignore EOX (F7) outside SysEx without inventing a loss or Warning; preserve
  its existing system-common cancellation of running status.
- `input_configuration.hpp`: internal `on_sysex_overflow(needed_bytes)` callback,
  invoked once per cap-discarded message on the backend thread (WinMM also uses
  zero for the receive loss events below). TrussC uses it only
  to record a counter and pending flag and post a preallocated main-thread slot
  without allocation or logging. Only the first loss posts the slot, which keeps
  the diagnostics alive until the owner's sole Warning is delivered.
- `backends/alsa_seq/midi_in.hpp`: feed borrowed SysEx event bytes directly to
  the bounded shared decoder. Admission and overflow accounting use each parsed
  logical message. SysEx leaves the fixed codec scratch size/capacity unchanged;
  ordinary ALSA events continue through the byte-event codec.
- `backends/winmm/midi_in.hpp`: retain decoder reset on `MIM_LONGERROR` and
  count it and each failed `midiInAddBuffer` re-queue via `on_sysex_overflow(0)`
  (unknown-size receive loss), sharing the owner's preallocated, truthful Warning
  slot without backend allocation or logging.
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
  Other native/Web output backends and the input bounds are unchanged.

Finding only (send side unchanged): WinMM `send_message` waits without a bound
in `while (midiOutUnprepareHeader(...) == MIDIERR_STILLPLAYING) Sleep(1);` and
returns on `midiOutLongMsg` failure without unpreparing the header.

The cap is a TrussC external-input memory policy, not a MIDI protocol maximum.
A legitimate single SysEx above it is discarded. CoreMIDI, WinMM, WinUWP, ALSA
seq/raw and Android use the shared decoder. JACK is disabled by the addon;
WebMIDI continues requesting access without SysEx. TrussC uses decoded-message
callbacks; the receive policy does not add bounds to application raw-data storage.

## Updating or dropping the patch

1. Check whether the new upstream release provides equivalent bounded assembly,
   bounded ALSA scratch, discard/resynchronization behavior and chunked ALSA
   output with bounded drain retries and pacing, plus WinMM receive-loss
   reporting. If so, remove the patch and verification calls, and adapt the
   internal overflow callback.
2. Otherwise rebase the replacements on the new pinned release, update all
   original/patched hashes, and keep failure for unknown source content.
3. Run `tests-sysex`, `tests`, patch integration checks, `test_output.py`,
   `test_hooks.py` and the core sweep.
   Independently remove each admission/capacity/scratch/discard hook in a private
   source copy and confirm its corresponding test fails. Run CoreMIDI and WinMM
   port checks described in `tests-sysex/README.md`.
4. Update this ledger and `docs/LICENSE.md`.
