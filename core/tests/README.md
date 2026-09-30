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
- `loggerThreadSafety/` — the Logger is safe to call from any thread, and
  sokol's messages go through it (#265). Threads logging at once while
  another thread switches the file (`setLogFile()`) and toggles the levels
  leave every line whole and exactly once, in the file and on the console;
  `setLogFile()` / `closeFile()` toggled under load tear or duplicate
  nothing, and nothing is written after `closeFile()` returns; an `onLog`
  listener that logs again, itself or through a thread it waits for, does
  not deadlock. The sokol bridge (`internal::sokolLog`) maps panic / error /
  warning / info to Fatal / Error / Warning / Verbose, with the tag as the
  module and `id:<item> line:<line>` when sokol passes no message. POSIX
  only, each in a forked child: a panic reaches the log file and still
  aborts through `slog_func`; a panic while another thread holds the
  Logger's lock does not wait for it (the line goes to stderr); and on
  Linux, with no X display and `TRUSSC_LOG_FILE` set, `runApp()`'s
  `XOpenDisplay()` failure lands in that file.
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
  sound card is needed. A `.ogg` file that is not Ogg Vorbis fails with
  `DecodeFailed` and is closed once (counted on Linux by `src/fcloseProbe.cpp`).
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
  after a second meanwhile) and goes on once it returns; an App runs once:
  `Window::setApp()` refuses an App whose window closed (one error, the
  window keeps its App, no hook comes back, no second `setup()`) and any App
  on a window that is not open;
  `AudioRecorder::stop()` waits for the pass in flight, and a capture held in
  flight by a test hook (`internal::setAudioRecorderCaptureHookForTests()`)
  while another thread calls `stop()` still ends up in the WAV and in
  `getRecordedSeconds()`. A watchdog turns a barrier that never returns into
  a FAIL.
- `mediaDecode/` — *(also on web)* the bundled decoders read every image
  format and Ogg Vorbis through the TrussC entry points:
  `Pixels::loadFromMemory()` / `load()` / `loadHDR()` for PNG (8 and 16-bit),
  JPEG, BMP (24-bit and 8-bit paletted), TGA (raw and RLE), HDR, GIF (first
  frame of a two-frame file) and PNM, checking size and pixels;
  `SoundBuffer::loadOgg()` / `loadOggFromMemory()` for channels, rate, length
  and levels. Also guards the two TrussC patches in `stb_image.h`: BMP pixels
  that index past the stored palette read as black (the stack is painted
  first, so the check cannot pass on a zeroed stack), and a GIF with the
  longest LZW prefix chains decodes on a 64 KB thread stack (native POSIX
  only; elsewhere only its output is checked). Image fixtures are made at
  runtime; the Ogg clip is embedded (`src/toneOgg.h`).
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
- `serialHangup/` — a lost serial device is reported (#260): when the device
  behind a `Serial` goes away, `available()` / `readBytes()` / `readByte()` /
  `writeBytes()` each notice it on their own, close the port, log one warning,
  and `isConnected()` turns false so `setup()` can reconnect; a quiet but
  present device is not a loss. `onDisconnect` fires once per open connection:
  for the loss (`wasClean` false, the port, the rate and the warning's text as
  `reason`) and for `close()` of an open port, but not from the destructor or
  a move assignment. A listener may call `setup()` from inside the
  notification, and the call that found the loss must leave that new
  connection alone. Only the I/O calls find a loss: `close()` or `setup()`
  after an unplug nobody noticed fire once as a clean close and leak no
  descriptor. Several threads may share one `Serial` (it has its own
  reader-writer lock): when they race an unplug, the loss is reported once,
  the fd is closed once, and the descriptors the kernel hands out right after
  (recognized by inode) are neither closed nor written to. The I/O calls never
  wait for each other, even for a write that takes 200 ms; `close()` waits for
  the writes in progress, is not starved by writes that keep coming, and no
  write reaches the closed fd; a loss found on one connection never closes the
  next one. `isConnected()` / `getDevicePath()` answer even from a thread that
  `close()` is waiting for, and a Logger listener may call back into the
  `Serial` whose `setup()` / `close()` logged (both would deadlock otherwise;
  a watchdog turns that into a failure). The Android backend's guard against
  joining its USB worker from the worker itself (`internal::isThisThread()`)
  is checked on its own; the backend itself needs Android. `src/slowWrite.cpp`
  plays the slow write by defining `write()` (Linux only). A pseudo-terminal plays the device, and closing its master
  stands in for the USB unplug. POSIX only (SKIP on Windows).
- `serialBaudRate/` — `Serial::setup()` does not report success after opening
  at a speed other than the one asked for (#260): rates without a termios
  B-constant used to open at 9600 and report success. Linux must apply any rate
  exactly (termios2); on macOS a pty rejects `IOSSIOSPEED`, so there such rates
  must fail cleanly. A real Linux driver that cannot generate a rate writes
  another one back instead of failing, B-constant rates included, and
  `setup()` must then fail rather than report the requested rate, also when
  the rate it writes back is the one the tty had. A driver that applies no
  rate at all (a USB gadget's `/dev/ttyGS*`) keeps its old rate whatever is
  asked, and there `setup()` must succeed with a warning. A pty does neither,
  so on Linux the test defines its own `ioctl()` that makes `TCGETS2` report
  both (`src/fakeDriver.cpp`). POSIX only, except the Windows write timeout
  `setup()` derives from the rate (at least 4 times the wire time plus 5 s),
  which is checked on every platform.
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
- `nodeRemoval/` — node lifetime in mouse dispatch (#255): the window context
  holds the hovered / grabbed / selected node weakly and dispatch holds a
  strong reference while handlers run, so a node freed by `removeChild()` /
  `removeAllChildren()` is never touched by the next hover update, drag or
  release and `getSelectedNode()` returns null; a handler or `Event` listener
  that removes its own node or an ancestor (grab release and drag, the
  press / release / move / scroll bubbling, mouseEnter / mouseLeave) runs on
  a live node, its mods still get the event (checked for the grab release),
  and the node is freed when dispatch returns;
  `isMouseOver()` / `getSelectedNode()` never match a new node at a freed
  node's address, and `setSelectedNode()` with a node no `shared_ptr` owns
  clears the selection. Working code is unchanged: a removed node the app
  still holds gets its Leave, reparenting fires no extra Enter / Leave,
  `destroy()` drops hover / grab / selection at once. Runs in a secondary
  window's context and in the main one. A freed probe's memory holds a
  sentinel node that counts any call reaching it, so a stale pointer fails
  the test instead of depending on heap reuse.
- `appRoot/` — the running App is `getRootNode()` (#255): the root is a weak
  reference, so the App can't register itself from its constructor, and the
  code that creates it through a `shared_ptr` does. `runApp()`'s setup
  callback (called here without `sapp_run()`) registers the App, and the
  root is gone once the cleanup callback freed it. Inside the App's
  constructor `getRootNode()` is not the App yet and `App::setSize()`
  resizes no window and warns once however often it is called (an App on
  the stack likewise); from `setup()` it goes to the main window without a
  warning. `runHeadlessApp()` owns
  the App with a `shared_ptr`, so it is the root in `setup()`, `update()` and
  `cleanup()`, `setup()` can `addChild()`, and the root is cleared when the
  run ends.
- `pixelsIndex/` — `Pixels` indexes with `size_t`, so images past `INT_MAX`
  bytes work: the offset of the far corner of a 23171x23171 RGBA image, and
  an `allocate()` byte count that is checked before it can wrap (against a
  32-bit limit too, standing in for wasm32). A size that would wrap, a
  negative one, or a channel count other than 1-4 logs an error and leaves the
  buffer empty, and `crop()` / `Image::allocate()` stop there. With 64-bit
  `size_t` and at least 4 GiB of memory available (on Linux, the lower of
  `MemAvailable` and the cgroup v2 `memory.max` headroom), it also allocates real buffers just past 2 GiB and checks
  `getColor()` / `setColor()` at the far corner and `halve()` reading pixels
  past `INT_MAX` (about 6 s, 2.6 GB peak); otherwise that part prints SKIP.
- `fontSfntCheck/` — font data is checked before stb_truetype reads it:
  `FontAtlasManager::setupFromMemory()` returns false with a warning for 0
  bytes, a `.ttc` header whose font count or offset points outside the data,
  a table directory or a table past the end of the data (also when offset +
  length wraps in 32 bits), a missing required table, head / hhea / maxp /
  cmap shorter than the fields stb reads, cmap encoding records or a used
  subtable offset past cmap, `numberOfHMetrics` outside 1..numGlyphs, a short
  hmtx or loca, an unknown loca format, and a loca entry past glyf; and for
  copies of a TrueType, a CFF and a collection font cut short in the header,
  the table directory and each table. A glyph index from the cmap past
  numGlyphs draws as .notdef. Also guards the TrussC patch in
  `stb_truetype.h` (CFF data is read with the CFF table's own length) and the
  padded `STBTT_malloc` (a glyph whose last contour is one off-curve point;
  meaningful under ASan). The fonts are built at runtime; fonts installed at
  the usual system paths are also loaded and cut short when present.
  `fontSfntCheck --dump <files>` prints glyph metrics to compare two builds.
