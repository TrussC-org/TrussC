# core/tests

Headless **behavioral regression tests** for the TrussC core. Each test's
`main()` returns non-zero on failure. CI builds and runs every `core/tests/*/`
here (`build_all.py --core-tests-only`); a non-zero exit fails the job. This is
the same convention bundled addons use (`addons/*/tests/`).

This is the *behavioral* tier — it complements, and does not replace, the
**build canaries** in `examples/tests/` (e.g. `AllFeaturesExample`), which prove
the API still compiles/links/instantiates but do not assert runtime behaviour.

## What belongs here

- A test guards **one invariant or one fixed bug** — a contract a future refactor
  could silently break while still compiling. Not "exercise the API for coverage"
  (that's what `AllFeaturesExample` is for).
- **Fast and headless** — no window/GPU. Use plain `main()` for pure-logic checks,
  or `runHeadlessApp<App>()` when you need a realistic `setup()`/`update()` runtime
  (it runs the per-frame `internal::drainMainThreadQueue()`, so `runOnMainThread` works).
- **Assertion-based**, clear names, exit code = pass/fail.

## Two test shapes

1. **trusscli project tests** (default) — a `src/` dir; built via `trusscli` and
   linked against libTrussC. `threadSafety/` is one.
2. **Standalone CMake unit tests** — a committed `CMakeLists.txt` with source at
   the dir root (no `src/`); built with plain `cmake`, **not** linked against
   libTrussC. Use this when a test must compile a library directly with options
   that would clash with the copy already baked into libTrussC — e.g. driving
   `sokol_gl` on `SOKOL_DUMMY_BACKEND` (no GPU) to assert GPU-resource
   accounting. `build_all.py` tells the two apart by the committed `CMakeLists.txt`
   + absence of `src/`, and runs both under `--core-tests-only`.

### Also on web (`web-test` marker)

A trusscli project test that also has a `web-test` file in its dir is built for
WebAssembly too and run under **node** (`build_all.py --web-only
--core-tests-only`; in CI, the daily run's `sweep-web` job, not the per-PR
lane). Use it when the invariant lives in
web-only code (`#ifdef __EMSCRIPTEN__`), which the web example builds only ever
compile. The test is the regular web app build (Emscripten's default
environment includes node), so it must not touch the canvas / GPU: plain
`main()` and no drawing. It still runs natively under `--core-tests-only`, so
give the native side something real to check (or an explicit skip).
Locally: source `emsdk_env.sh` first (for `emcmake` and `EMSDK_NODE`).

## Keep it curated (avoid rot)

- Default workflow: **when you fix a bug, add the regression test that would have
  caught it.**
- **Delete a test when its invariant becomes obsolete** (feature removed, contract
  intentionally changed). A stale suite is worse than a small one.

## Tests

- `threadSafety/` — main-thread affinity: `runOnMainThread` defers + delivers on
  the main thread, `Event` `Deliver::Main` marshals worker-fired notifies onto the
  main thread, and `Node::destroy()` is safe from any thread.
- `threadLifecycle/` — destroying a `tc::Thread` never calls `std::terminate`
  (#257): not after its worker returned on its own, not after only
  `stopThread()`, not right after `startThread()` (the worker skips
  `threadedFunction()` instead of calling the pure virtual), not after a
  restart, and, for a subclass that does not wait, not on its own worker:
  from its `threadedFunction()` (the base destructor detaches instead of
  joining itself, and the worker writes nothing to the freed object) or at
  worker exit when a `thread_local` `shared_ptr` on the worker was its last
  owner. A subclass that
  calls `waitForThread()` in its own destructor never has `threadedFunction()`
  running after its members are gone, and the base destructor logs exactly one
  warning when the subclass did not wait, including after only `stopThread()`
  and right after `startThread()` (also when the worker skipped
  `threadedFunction()`). Not covered: a waiting subclass destroyed on its own worker,
  from its `threadedFunction()` or by a `thread_local` owner at thread exit (it
  still terminates, see the "Destruction" notes in `tcThread.h`), and a
  destruction at the very moment the worker calls `threadedFunction()`.
- `audioDiagnostics/` — a play the AudioEngine refuses is never silent (#231):
  `Sound::play()` returns false for every drop reason, drops are counted and
  reach the TrussC logger (rate limited, and only from the main thread — an
  off-main drop is counted and reported by `runHeadlessApp`'s own frame pump;
  its exit flush and `AudioEngine::shutdown()` log what the rate limit held
  back), the audio thread's meters (peak / RMS / clipped samples / voice
  level / load) work and shutdown clears them, a reused `SoundBuffer`'s
  `getPath()` follows its last fill (memory / PCM / generated fills clear it),
  and `tc_get_audio_state` reports it all, the microphone included. Runs on
  miniaudio's null backend (`internal::setNullAudioBackendForTests()`), so no
  sound card is needed.
- `eventRemovalDuringNotify/` — a `notify()` pass whose listener list changes
  (#256, #107), for `Event<T>` and `Event<void>`: a listener that an earlier
  one disconnects or destroys is not called in that pass, `clear()` stops the
  rest of the pass, a listener that removes itself does not stop the later
  ones, and a listener added during a pass starts from the next one. Includes
  the `Tween` shape: objects in a vector listen with `[this]` and re-listen in
  their move constructor; growing the vector inside the pass sends no call to
  a moved-from object.
- `audioListenerTeardown/` — nothing on the audio thread reaches an object
  after its owner let it go (#256), on the real `AudioEngine` over miniaudio's
  null backend: `AudioEngine::waitForCallbackIdle()` waits for an `audioOut`
  pass in flight, returns at once with no audio running and from inside a
  listener, and gives up (warning, `false`) on a listener stuck for a second;
  an App torn down by `runHeadlessApp` while its `audioOut()` runs keeps the
  hook through `cleanup()`, then its destructor neither starts during
  `audioOut()` nor sees it called afterwards (the windowed exit, hot reload
  and closing a secondary window use the same `internal::detachAppAudio()`);
  that teardown waits for a stuck `audioOut()` past one second without
  destroying the App (one error logged; the public barrier still gives up
  after a second meanwhile) and goes on once it returns; a secondary window's
  App attached again after its window closed gets `audioOut()` back through
  one hook and runs `setup()` again, and moving an App between windows without
  a close restarts nothing;
  `AudioRecorder::stop()` waits for the pass in flight, and a capture held in
  flight by a test hook (`internal::setAudioRecorderCaptureHookForTests()`)
  while another thread calls `stop()` still ends up in the WAV and in
  `getRecordedSeconds()`. A watchdog turns a barrier that never returns into
  a FAIL.
- `sglLayerUpload/` — *(standalone, dummy backend)* the sokol_gl `_sgl_draw()`
  vertex upload is done **once per frame** and shared across layer draws, instead
  of re-appending the whole vertex set per layer. Guards against the O(N layers ×
  V vertices) GPU-buffer blow-up that grew the buffer until allocation failed
  (Metal `id:52`), the root cause of disappearing deferred 2D/PBR content.
- `screenshotContract/` — *(also on web)* the screenshot APIs report what they
  actually do (#230). Web: `grabScreen()` / `saveScreenshot()` return false,
  nothing is queued or created, and each API warns once. Native:
  `saveScreenshot()` still creates the destination folder, queues the capture
  and returns true. The per-PR CI runs only the native half, which passes with
  or without the #230 fix: it catches the web early return leaking into native
  builds. The web half is what guards #230; the daily run (`daily.yml`,
  `sweep-web`) runs it under node.
- `frameTiming/` — time handling (#228, #229): one steady elapsed clock with its
  origin at program start, `resetElapsedTimeCounter()` as a display offset only,
  `getFrameElapsedTime()` constant within a frame (through the main loop's frame
  start); fixed-Hz update steps report the nominal `1/updateFps`, catch-up is
  capped at `setMaxUpdateSteps()` steps per frame (default 10, `<= 0` runs
  every step; also per `runHeadlessApp` pass at any rate: the pass sleeps
  only until the next step is due, on a timer that doesn't round up to a
  ~15.6 ms Windows tick, so 1 kHz keeps up), each loop warning once, without
  starving `runOnMainThread` work; runtime mode switches
  don't replay old time and re-applying the current rates every frame changes
  nothing; `getFrameRate()` is the measured rate (steady at non-integer ratios;
  also in the default draw-synced mode, the independent VSYNC update and
  headless); the fixed-fps draw skip doesn't drop frames at the display rate;
  Node timers are countdowns that keep their phase and are not charged for
  time before they were created, and `callEveryCatchUp` fires once per due
  interval up to its limit (a cancel from the callback stops it); the
  `ScreenRecorder` pacer (its `start()`/`tick()` are all the timing
  `ScreenRecorder` reads) stays exact after long uptime and, like the
  `tc_get_health` uptime, ignores `resetElapsedTimeCounter()`.
