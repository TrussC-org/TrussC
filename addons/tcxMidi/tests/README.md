# MIDI send result unit tests

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

The double does not verify native backend error generation or physical delivery.
Compile the production header against the pinned libremidi as well, and check
actual devices separately. Port removal/reconnection belongs to #415.
