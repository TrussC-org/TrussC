# MIDI unit tests

Console TrussC project using the production `MidiOut` and linked logger/event
implementation with a small libremidi output backend double. The existing
`examples/build_all.py --addon-tests-only` CI lane discovers `tests/src/`, builds
this project and runs its `tests` executable (nonzero exit means failure).

Builds fetch the addon's pinned libremidi and require the usual TrussC platform
dependencies. Running the test needs no MIDI device, ALSA sequencer or display.
`local.cmake` puts `mocks/` first only for the test executable; libremidi itself
still builds against its real headers.

```sh
tools/bin/trusscli update -p addons/tcxMidi/tests --tc-root "$PWD" --ide cmake
cmake -S addons/tcxMidi/tests -B addons/tcxMidi/tests/build-linux
cmake --build addons/tcxMidi/tests/build-linux --config Release -j 4
addons/tcxMidi/tests/bin/tests
```

Run from the repository root and use your build resource wrapper when required
by your environment. `build_all.py` selects the platform's build directory and
binary name (an app bundle on macOS or `.exe` on Windows). Generated CMake files,
presets, build directories and binaries are ignored by the project `.gitignore`.

Checks every public send helper (including both pitch bend overloads) for
success, backend failure and a closed port; verifies warning severity and error
text, unchanged MIDI bytes/clamping, empty messages, and recovery after an error
or explicit close/reopen. For each failure reason, 100 consecutive failures
return false with exactly one warning; reopening by index, name or virtual port
allows one more warning per reason. Closing and failed open attempts reset the
gates too, and separate MidiOut objects have independent gates.
Existing calls ignoring the result also compile.
The harness uses a plain `main()` without starting the TrussC app loop.

Virtual-port enumeration tests retain the hardware entries and expose software
entries only when `track_virtual` is set. They cover all six observer paths:
input/output listing, opening by name and opening by index. Removing the flag
fails these checks even on a host without a MIDI subsystem. The real ALSA and
CoreMIDI integration test is in `../tests-virtual`.

The double does not verify native backend error generation or physical delivery.
Compile the production header against the pinned libremidi as well, and check
actual devices separately. Removal of a real port is checked by `../tests-virtual`.

Input storage tests also drive the production callback with the existing mock.
They check copied port-name accounting, exact oldest-message eviction, shrink
and zero-limit behavior, a whole over-budget message, 1024 64,000-byte messages,
and one automatic main-thread Warning without polling. Counters and gates
survive close/reopen; inline events remain independent of polling losses.

Device lifecycle tests (`deviceLifecycle.cpp`, #415) run the same checks for
`MidiIn` and `MidiOut`. The double's observer keeps the removal callbacks of
every live watch; the test marks a port unplugged and calls them from a worker
thread, as a backend would. They cover the five rules: `isConnected()` only
while open; `setup()` reopening the last port by name (also when indices
shifted, for a virtual port, after a failed open, and failing with one error
when there is nothing to reopen or the port is gone); `close()`, reopening and
`setup()` never firing; a removal firing once on the main thread, only at the
next queue drain, with `isConnected()` already false and one warning, a
removal storm being one event, another port's removal and virtual ports being
ignored, and a listener reconnecting with `setup()`; and the destructor firing
nothing, also with a delivery already queued. Every watch is released with its
port.
