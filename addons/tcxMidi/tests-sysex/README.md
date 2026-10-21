# Bounded SysEx tests

This console harness uses the addon's real pinned libremidi headers and library.
It is discovered by `examples/build_all.py --addon-tests-only`. Running without
arguments needs no MIDI device or display. On Linux with ALSA headers, it also
runs the real ALSA sequencer event codec without opening a client or port.

From the repository root (use the build resource wrapper when required):

```sh
tools/bin/trusscli update -p addons/tcxMidi/tests-sysex --tc-root "$PWD" --ide cmake
cmake -S addons/tcxMidi/tests-sysex -B addons/tcxMidi/tests-sysex/build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build addons/tcxMidi/tests-sysex/build-linux --config Release
addons/tcxMidi/tests-sysex/bin/tests-sysex
```

For an offline source override, copy pristine libremidi 5.4.3 into a private
writable directory, apply `cmake/PatchLibremidi.cmake` as described in the ledger,
and configure with `-DFETCHCONTENT_SOURCE_DIR_LIBREMIDI=path/to/private/copy`.
Unpatched overrides fail configuration deliberately.

The tests cover both stream entry points, initial and continuation admission,
exact-limit complete/fragmented messages, bounded capacity growth, real-time
filtering, discard/resynchronization, start timestamps, ignored SysEx, reset,
successive messages without stack growth, ALSA scratch and safe-thread reporting.
The reporting callback is also checked with backend allocations disabled, empty
idle queues, one posted slot on first loss and slot lifetime through teardown.
The wrapper's `tests/` harness additionally checks idle input creation/destruction,
automatic reporting across close/reopen and destruction without polling, and
separation from the polling drop count.

The offline configure checks use a pristine source directory and a new scratch
directory. They reconstruct a local archive to exercise FetchContent's actual
patch command, reconfiguration, unpatched/patched/edited source overrides and
reused extracted caches; they do not replace the production archive hash.

```sh
python3 addons/tcxMidi/tests-sysex/test_patch.py path/to/pristine/libremidi tmp/patch-checks
```

After a native Linux Makefiles build, independently disable each protection in
scratch include overlays and confirm its tests fail (the baseline must pass):

```sh
python3 addons/tcxMidi/tests-sysex/test_hooks.py path/to/patched/libremidi addons/tcxMidi/tests-sysex/build-linux tmp/hook-checks
```

## Native port checks

A SysEx above 1,048,576 bytes including F0/F7 is now discarded whole; this is a
receiver memory policy, not the MIDI protocol maximum. Device/driver delivery
and representative controller or firmware dumps need separate verification.

The same executable accepts `--receive-virtual NAME`, `--receive NAME`, and
`--send NAME`. The sender sends an over-limit SysEx, an over-limit SysEx with an
interleaved F8, then a valid small SysEx and Note On. Start the receiver first;
it waits for that note with a 60 second failure timeout. It does not enable
polling. Expected receiver result (exit 0):

```text
oversize=0 clocks=1 valid_sysex=1 notes=1 unexpected=0 warnings=1
```

Expect one `SysEx assembly` Warning naming limit 1048576 and needed bytes;
neither rejected SysEx is delivered. The sender exits 0 after all sends succeed.
A failed send or a driver that filters/rejects the test traffic is a failed port
check, even if the unit tests pass.

macOS CoreMIDI provides virtual ports without installing a driver. Build once:

```sh
tools/bin/trusscli update -p addons/tcxMidi/tests-sysex --tc-root "$PWD" --ide cmake
cmake -S addons/tcxMidi/tests-sysex -B addons/tcxMidi/tests-sysex/build-macos -DCMAKE_BUILD_TYPE=Release
cmake --build addons/tcxMidi/tests-sysex/build-macos --config Release
```

In terminal 1:

```sh
addons/tcxMidi/tests-sysex/bin/tests-sysex.app/Contents/MacOS/tests-sysex --receive-virtual TrussC-SysEx-Check
```

In terminal 2:

```sh
addons/tcxMidi/tests-sysex/bin/tests-sysex.app/Contents/MacOS/tests-sysex --send TrussC-SysEx-Check
```

No IAC setup is needed for this virtual input. Alternatively enable IAC Driver
in Audio MIDI Setup and use `--receive` / `--send` with its bus name.

Windows 10/11 WinMM has no generic built-in MIDI loopback and does not support
libremidi virtual ports. Use an existing MIDI loopback device or install a small
loopback driver such as loopMIDI and create `TrussC-SysEx-Check`. The test uses two large messages, so prefer a virtual loopback over a
31.25 kbit/s hardware cable, which exceeds the receiver timeout. Without an
existing loopback device or an installed loopback driver, this WinMM check
cannot run.

In a Visual Studio developer PowerShell, build once:

```powershell
.\tools\bin\trusscli.exe update -p addons/tcxMidi/tests-sysex --tc-root "$PWD" --ide cmake
cmake -S addons/tcxMidi/tests-sysex -B addons/tcxMidi/tests-sysex/build-windows
cmake --build addons/tcxMidi/tests-sysex/build-windows --config Release
```

In terminal 1:

```powershell
.\addons\tcxMidi\tests-sysex\bin\tests-sysex.exe --receive TrussC-SysEx-Check
```

In terminal 2:

```powershell
.\addons\tcxMidi\tests-sysex\bin\tests-sysex.exe --send TrussC-SysEx-Check
```
