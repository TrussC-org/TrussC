# Virtual MIDI port integration test

Uses the real, pinned libremidi through the production `MidiIn` and `MidiOut`.
No window or physical MIDI device is needed. On Linux the backend must be ALSA
sequencer; on macOS it is CoreMIDI. Separate backend clients create a virtual
source and destination in the same process. The test lists both, opens each by
name, sends a Note On in both directions and checks the received `90 3c 64`
bytes. A default observer is a negative control: it must hide both virtual ports.
Removing `track_virtual = true` from the shared configuration must fail the test.
The test also checks that the application's own virtual ports remain visible.

From the repository root (use your environment's build resource wrapper):

```sh
tools/bin/trusscli update -p addons/tcxMidi/tests-virtual --tc-root "$PWD" --ide cmake
cmake -S addons/tcxMidi/tests-virtual -B addons/tcxMidi/tests-virtual/build-linux
cmake --build addons/tcxMidi/tests-virtual/build-linux --config Release -j 4
addons/tcxMidi/tests-virtual/bin/tests-virtual --require-device
```

Without `--require-device`, an unavailable ALSA sequencer prints `SKIP` with
the reason and exits successfully. With it, an unavailable sequencer is an
error, so device checks cannot accidentally pass by skipping. Missing
`/dev/snd/seq` or insufficient access requires a host with `snd-seq` enabled and
appropriate permissions. A message wait has a five-second failure timeout;
there is no elapsed-time assertion. The addon test lane discovers this project.

On the trussc-mac Air, build in `build-macos`, then run
`addons/tcxMidi/tests-virtual/bin/tests-virtual.app/Contents/MacOS/tests-virtual --require-device`.
Expect `PASS` for both virtual clients, the negative control, both lists and
name-based opens, and both `received 90 3c 64` checks; there must be no `SKIP`.
This automatic CoreMIDI check is necessary: IAC endpoints can have an entity
and already pass libremidi's hardware classification without the flag.

For a separate application's loopback port, use:

```sh
tests-virtual --require-device --loopback TrussC982
```

On Windows create `TrussC982` in loopMIDI and keep loopMIDI running; run the
built `tests-virtual.exe` with those arguments. WinMM cannot create virtual
ports itself, so an invocation without `--loopback` skips. On macOS enable
IAC Driver in Audio MIDI Setup, create an enabled bus named `TrussC982`, and
run the same arguments with the app-bundle executable above. Expect input and
output listings containing the name, both name-based opens, a successful send,
and `PASS: external loopback received 90 3c 64`. Choose a distinctive substring
of the actual displayed name; WinMM limits names to 31 characters.
