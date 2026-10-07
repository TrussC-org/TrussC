# Stream seek latency

A standalone console measurement tool for #550, with no product changes or
timing assertions (#527). By default it opens the **real default output device**
through `AudioEngine`, streams a generated WAV through `Sound::loadStream()`
(`SoundStream`), and measures 300 seeks. It refuses silent fallback to Null.
The signal is a quiet DC ramp; seeks may make audible clicks. System output
volume does not affect detection, which happens before the OS mixer.

## Build against a checkout

Install the checkout's normal TrussC build dependencies and `trusscli`. Keep
this benchmark folder in a writable location; the selected checkout also needs
to be writable for TrussC's core build directory. From the directory containing
`bench/streamSeekLatency`:

```sh
trusscli update -p bench/streamSeekLatency --tc-root <checkout>
cmake -S bench/streamSeekLatency -B bench/streamSeekLatency/build-release -DCMAKE_BUILD_TYPE=Release -DTRUSSC_DIR=<checkout>/core
cmake --build bench/streamSeekLatency/build-release --config Release --parallel 4
```

Replace `<checkout>` with an absolute path; quote paths containing spaces.
`--tc-root` takes the repository root; `TRUSSC_DIR` takes its **core** directory.
The explicit CMake argument also works without using the generated presets.
Build separately for each revision and run/save results before rebuilding:
the generated projects place executables in the same `bin` directory. Use a
fresh build directory when changing checkouts. CMakeLists, presets, and build
outputs are generated and ignored, as in `core/tests/userDataPath`.

For the before/after comparison use `fb2cfad9` (before #547) and `fc4565c8`
(#547 merge), then optionally current main. Current main contains subsequent
changes, so comparing it alone with the parent does not isolate #547.
This source was built against `fb2cfad9` and current main; see
[REPORT.md](../../REPORT.md). Local run logs and CSVs are kept in the ignored
`results/` directory.

Windows, from a Visual Studio developer PowerShell (WASAPI expected):

```powershell
.\bench\streamSeekLatency\bin\streamSeekLatency.exe --count 300 --seed 550 --csv before-wasapi.csv
```

macOS, from Terminal (Core Audio expected):

```sh
./bench/streamSeekLatency/bin/streamSeekLatency.app/Contents/MacOS/streamSeekLatency --count 300 --seed 550 --csv before-coreaudio.csv
```

Rebuild against the after checkout and use a different CSV filename. Confirm
the printed backend, device, native sample rate, and callback sizes match
between runs. Keep the same default device and workload, run several batches,
and retain stdout alongside each CSV. No window, microphone, or loopback input
is needed. Do not change devices during a batch.

Linux follows the same CMake commands and runs
`./bench/streamSeekLatency/bin/streamSeekLatency`. If no device can open, the
tool exits with an explanatory error. Optional `--null` explicitly selects the
existing device-less test backend:

```sh
./bench/streamSeekLatency/bin/streamSeekLatency --null --count 10 --csv smoke.csv
```

**Null-backend numbers mean nothing for real-device latency or the #550
semaphore decision.** Native WASAPI/CoreAudio runs are still required.

## What is measured

At startup, the tool writes a unique temporary 60 s mono IEEE-float WAV at
48 kHz, with sample value `0.02 + 0.08 * frame / (48000 * 60)`. It removes the
file/directory on normal exit, including handled errors. Only one voice plays,
at unity gain, centered pan, normal speed, into a 48 kHz stereo engine.

`AudioEngine::audioOut.listen(..., audio::priority::Monitor)` receives samples
after Sound voices have been mixed. This public hook and `AudioOutBuffer` exist
in both revisions. API and invocation order were checked with:

```sh
git show fb2cfad9:core/include/tc/sound/tcSound.h
git show fb2cfad9:core/include/tc/events/tcEvent.h
```

The detector scans channel 0 of every output frame; the mono signal is copied
equally to both channels. The last nonzero encoded sample supplies the observed
current position. After a 100 ms rest, it chooses a random target in [3, 55] s,
at least 10 s from both that observed position and the previous target (also
protecting against a previous timed-out seek). `--seed` defaults to 550. Target
rejection depends on observed playback, so the same seed is reproducible in
intent but does not guarantee identical targets across devices or revisions.

The detector is armed before `t0 = steady_clock::now()`, immediately followed
by `setPosition(target)`. It records `t1` at entry to the **first** listener
invocation containing samples in [target - 1 ms, target + 2.1 s]. The wide
upper bound accepts any part of the first returned buffer, including a buffer
partly filled with silence; the 10 s separation rejects old audio. The lower
slack covers frame rounding and float amplitude quantization (well below one
48 kHz frame at these amplitudes). Silence cannot match the positive ramp.
No `getPosition()` fallback is used: it can report the requested target before
the mixer has reached it.

The reported latency is `(t1 - t0)` in milliseconds. This is **software callback
visibility**, not audible/DAC latency. `t1` is after the engine has mixed the
buffer, not at the backend callback's start. Detection has callback granularity
(nominally one buffer period, `1000 * frames / sample_rate` ms); CSV records the
matching sample offset and callback size. Audio-thread scheduling/preemption
and mixing time can add unbounded wall-clock delay, so this is not a hard
error bound. OS/device buffering and conversion occur later and are not
measured. Main-thread 1 ms polling does not enter `t1`. An OS pause of many
seconds could invalidate the old/target separation assumption; discard such
interrupted batches rather than interpreting their results as seek latency.

The callback performs a clock read, a linear sample scan, and lock-free atomic
communication; it does not allocate, print, lock, or call engine APIs. Per-seek
storage is allocated before playback. Callback scanning adds some CPU overhead
to every buffer, consistently across revisions. Shutdown stops callbacks before
destroying their captured data.

Playback and detection use public APIs. The only internal calls are the shared
`internal::audioDeviceReport(false)` for backend/device/native-period metadata
and null-fallback rejection, and `setNullAudioBackendForTests(true)` when
`--null` is explicitly requested. Neither revision has a public backend-name
getter. These helpers were also checked in `fb2cfad9`; they do not inspect or
change stream seek state. Zero device-period/buffer metadata means unknown;
observed minimum/maximum callback frame counts are printed as well.

## Results

Stdout reports backend/device, engine/native sample rates, buffer sizes,
requested count, observed count, mean, median, nearest-rank p95, maximum, and
seeks not observed within 2 s. Statistics include only observations within
2 s; timeouts are separately counted, never replaced by 2000 ms or hidden in
the statistics. All-timeout batches print `n/a` and still return success.
Startup/device/file/argument errors return 1. A normal batch takes about
30 seconds plus accumulated seek latency; timeouts can extend it substantially.

`--csv path` writes one row per request, including timeouts, with source/target
seconds, status, raw latency (blank for timeout), steady-clock timestamps,
decoded matched position, callback frame count, and matching sample offset.
An in-flight observation after the deadline may retain diagnostic fields but
is still a timeout. Steady-clock timestamps are meaningful only within a run.
CSV is written after audio shutdown; its destination must be writable and its
parent directory must exist. Existing CSV files are overwritten. `--help`
lists the options. There are no timing pass/fail thresholds.
