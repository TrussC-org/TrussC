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
  module and `id:<item> line:<line>` when sokol passes no message. Each
  output has its own level (#311): console, file and system default to
  Notice, `setLogLevel()` overwrites all three and a later per-output call
  wins; the file and the console filter by their own level while `onLog`
  listeners get every line. POSIX
  only, each in a forked child: a panic reaches the log file and still
  aborts through `slog_func`; a panic while another thread holds the
  Logger's lock does not wait for it (the line goes to stderr); and on
  Linux, with no X display and `TRUSSC_LOG_FILE` set, `runApp()`'s
  `XOpenDisplay()` failure lands in that file.
- `onceGate/` — the warn-once gate `OnceGate` (#308): `isFirstTime()` is
  true the first time and false afterwards, per gate object (a `static` per
  call site, a member per object); with an interval it is true again once the
  interval has passed since the last true, not before (0, below 0 or NaN
  means once); many threads calling one gate get exactly one true between
  them; and a `static` gate works from a static destructor at exit. It is
  checked at compile time to be trivially destructible, not copyable or
  movable, and `constinit`-constructible.
- `pbrLightLimits/` — the PBR light limits (#333): `addLight()` registers up
  to 8 lights per window and logs one warning for lights past that, however
  many; re-adding a registered light on a full list is silent. The pure
  `internal::selectPbrSpecialLightSlots()` gives the single projector slot to
  the first Spot light with a projection texture and the single IES slot to
  the first light with a profile (among the first 8), and flags a further
  projector or IES light that gets no slot (the PBR draw warns once from it).
- `dataPathWrites/` — the core file writers share one path rule (#356):
  `setLogFile`, `FileWriter::open` (also in append mode), `saveTextFile`,
  `appendToFile`, `saveJson`, `Xml::save` and `Pixels::save` resolve a
  relative path against `getDataPath()` (not the working directory, which the
  test moves elsewhere), use an absolute path as given, create a missing
  parent folder, and log an Error and return false when that folder cannot be
  created (a regular file in the way; on POSIX, unless root, a read-only
  folder). UTF-8 folder and file names land on disk by their real names. For
  `setLogFile`, `getLogFilePath()` is the resolved absolute path, and a failed
  call (folder or open failure) keeps the current log file open, with the
  error line and later lines in it.
- `dataPathLoads/` — the loaders share the same path rule (#273):
  `Pixels::load` / `loadHDR`, `Sound::load` / `loadStream` and tcxLut's
  `Lut3D::load` resolve a relative path against `getDataPath()` with no
  working-directory fallback (the test moves the CWD elsewhere; a file only
  there is not found), `Pixels::save("a.png")` then `Pixels::load("a.png")`
  round-trips, and a UTF-8 WAV name loads. `getDataPath()` called from two
  threads at once, before anything else, agrees with the main thread. `Lut3D`
  is checked up to its `.cube` parse; `Font::load` needs a GPU and is not run.
- `audioDiagnostics/` — a play the AudioEngine refuses is never silent (#231):
  `Sound::play()` returns false for every drop reason, drops are counted and
  reach the TrussC logger (rate limited, and only from the main thread — an
  off-main drop is counted and reported by `runHeadlessApp`'s own frame pump;
  its exit flush and `AudioEngine::shutdown()` log what the rate limit held
  back), the audio thread's meters (peak / RMS / clipped samples / each
  playing sound's level / CPU usage) work and shutdown clears them, a reused `SoundBuffer`'s
  `getPath()` follows its last fill (memory / PCM / generated fills clear it),
  and `tc_get_audio_state` reports it all, the microphone included. Runs on
  miniaudio's null backend (`internal::setNullAudioBackendForTests()`), so no
  sound card is needed. A `.ogg` file that is not Ogg Vorbis fails with
  `DecodeFailed` and is closed once (counted on Linux by `src/fcloseProbe.cpp`).
  `SoundBuffer::mixFrom()` counts its offset in frames and refuses (logs)
  channel mismatches and ends past what a buffer holds. Decoders size buffers
  from what decodes: a FLAC or Ogg Vorbis stream (`src/vorbisTone.cpp`) whose
  stated length is larger than its data, or unknown for Vorbis, loads what it
  holds with no allocation sized from the stated length (the largest request
  is recorded by `src/allocProbe.cpp`), and growth past the first reservation
  lands on a correctly stated length. A voice on a buffer with no frames
  stops at its first mix. A file reached through `<dir>/..` is reported as
  given, the same string in `getPath()`, the drop warning, `getPlayingSounds()`
  and the tool (#365); `getBufferSize()` and the tool's `requestedBufferSize`
  are the requested size, while `AudioDeviceChangedArgs::bufferSize` is the
  period the device runs with (also with the default request 0).
- `soundVoiceLifetime/` — a `Sound` plays only while it, or a copy of it, is
  alive (#281), on the real `AudioEngine` over miniaudio's null backend:
  `maxPolyphony + 8` scoped looping Sounds each play and a new Sound plays
  afterwards, a scoped copy does not stop the original, a scoped one-shot
  stops when its scope ends, copy / move assignment release the old voice (a
  move keeps the moved voice playing), a paused voice is released too, and a
  streamed voice closes its file on `stop()` and when its last handle goes
  away (checked through `/proc/self/fd` on Linux).
- `streamSeek/` — a streamed `Sound` seeks for real and a stream it cannot
  read ends (#280), on the real `AudioEngine` over miniaudio's null backend,
  measured on `audioOut` with files of DC levels: `setPosition()` moves the
  audio (the level ~200 ms later is the target's), `getPosition()` reports
  the target from the call on and never the old position, a paused stream
  reports the target at once while the voice itself does not move, and
  resumes from it, and of several seeks the last wins (also while paused).
  While a seek is pending (the worker held back by
  `internal::setStreamFaultForTests(Stalls)`) no block after the call holds
  the old position's audio, and a non-looping stream does not end at the old
  data's end; a seek after an underrun at speed 10 keeps the ring bounded, so
  another stream is still refilled; after a re-init at another rate
  `getPosition()` carries over and `setPosition()` lands at the target; eager
  sounds seek at once. `loadStream()` rejects a file with no frames
  (`DecodeFailed`) and accepts a FLAC whose length is unknown (STREAMINFO
  total 0), which plays to its end and ignores `setPosition()` with one
  warning. A looping stream whose file was emptied after loading ends with
  one error log while another stream keeps being refilled; a decoder read
  error, a failed loop seek and a failed seek request each end the stream
  with one error log: a non-looping voice ends, a looping one stays playing
  but silent (#448) and `setPosition()` makes it play again. The frames a
  failing read still returned are played before the voice ends. An MP3
  stream's decoder gets a seek table (one point per second, at most 1024),
  also after a re-init; `setPosition(getDuration())` on an ~18 minute MP3,
  whose float duration is past the last frame, loops instead of failing. An
  ended voice's position (and pending seek) carries over a re-init. A
  watchdog turns a StreamWorker that never comes back into a FAIL.
  An `AudioEngine::init()` that can't open the output device (forced with
  more channels than miniaudio accepts) returns false, logs one error through
  the logger that names the requested device, and a later `init()` succeeds
  (#279). An `init()` on the null backend the test requested logs no
  "no usable audio backend" warning (that warning is for a fallback to it).
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
  null backend: `AudioEngine::waitForAudioCallbacks()` waits for an `audioOut`
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
- `audioRecorderWav/` — the WAV header `AudioRecorder` writes never wraps past
  4 GiB of samples (#336). Every file reserves a 36-byte `JUNK` chunk after
  `WAVE` (samples start at byte 80 for S16, 92 for F32; the data chunk
  header is at 72 / 84). The header sizes are 64-bit:
  a take stays plain RIFF up to the last frame whose RIFF size fits 32 bits
  and is RF64 from the next frame on, including the gap where only the RIFF
  size overflows; the RF64 patch turns `JUNK` into `ds64` with the 64-bit
  sizes and sets the 32-bit fields to `0xFFFFFFFF`. Checked on the header
  alone (`internal::wavSizeFields()` / `writeWavHeader()` /
  `patchWavHeader()` on a memory stream), so no 4 GiB file is written. A
  short S16 and F32 take on the null backend is plain RIFF with the `JUNK`
  chunk and loads through `SoundBuffer` with `numSamples` equal to
  `getRecordedSeconds()` times the sample rate. On Linux, a take whose file
  writes fail (recorded into `/dev/full`) makes `stop()` log one error and
  neither the RF64 notice nor the "stopped" notice. Not covered: `stop()`'s
  RF64 notice and the seek of a real file past 4 GiB (they need a 4 GiB take).
- `appAudioAttach/` — an App's `audioOut()` / `audioIn()` are subscribed
  right after its first `setup()` returns (#426), on the real `AudioEngine`
  over miniaudio's null backend: for the main App (`runHeadlessApp`) and a
  secondary window's App (setup on the window's first tick), whose `setup()`
  allocates what `audioOut()` reads, no `audioOut()` runs before `setup()`
  has returned and no hook is subscribed while it runs; afterwards there is
  exactly one hook each, also after more ticks or a move to another window;
  an App that is constructed but never run gets no callbacks; the App's
  `audioOut()` still runs before the default-priority listeners its `setup()`
  subscribed (the order the constructor subscription gave); the attach is
  idempotent and subscribes nothing once `internal::detachAppAudio()` ended
  the App. The hot reload generation's path is in `hotReloadLifecycle/`.
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
- `scopedStack/` — `scopedMatrix()` / `scopedStyle()` (#492): the guard
  pushes when it is made and pops when it goes out of scope, read from the
  matrix and style stack depth before, inside and after the scope, on an
  early return, a return from a loop, an exception, nested guards and a
  guard inside a `pushMatrix()` / `pushStyle()` pair (pops only its own
  entry); the matrix and color set inside are undone after it. Checks run in
  a release build only (headless `pushMatrix()` reaches sokol_gl).
- `sglLayerUpload/` — *(standalone, dummy backend)* the sokol_gl `_sgl_draw()`
  vertex upload is done **once per frame** and shared across layer draws, instead
  of re-appending the whole vertex set per layer. Guards against the O(N layers ×
  V vertices) GPU-buffer blow-up that grew the buffer until allocation failed
  (Metal `id:52`), the root cause of disappearing deferred 2D/PBR content.
- `hotReloadScan/` — *(standalone, plain CMake)* the configure step and the
  pre-build check decide "does this project use hot reload" the same way:
  `tc_hot_reload_scan()` finds `TC_HOT_RELOAD` in any `.cpp` under `src/` and
  ignores commented-out forms (#234), and `tc_hot_reload_decide()` is OFF on
  platforms without hot reload (web / Android / iOS) even with the macro in
  source (#329). The end-to-end build of a macro app as a normal app is
  `examples/tests/HotReloadFallback`, built by the daily sweeps.
- `screenshotContract/` — *(also on web)* the screenshot APIs report what they
  actually do (#230). Web: `grabScreen()` / `saveScreenshot()` return false,
  nothing is queued or created, and each API warns once. Native:
  `saveScreenshot()` still creates the destination folder, queues the capture
  and returns true. The per-PR CI runs only the native half, which passes with
  or without the #230 fix: it catches the web early return leaking into native
  builds. The web half is what guards #230; the daily run (`daily.yml`,
  `sweep-web`) runs it under node.
- `mcpHttpGuard/` — a web page in the user's browser cannot drive the
  loopback MCP server (#238): a foreign Host (DNS rebinding) or Origin gets
  403, a non-JSON POST 415, and a missing or wrong bearer token 401, while
  native clients keep working. Also the port line (#311): after bind, the
  server logs `[MCP] HTTP server listening on http://HOST:PORT/mcp` through
  the Logger at Notice, exactly once and with the actual port, and the line
  lands in the log file.
- `winsockLifetime/` — creating and destroying TcpClient / TcpServer any
  number of times leaves networking working (#254): after 200 of each, a raw
  `socket()` still succeeds and a UdpSocket that was already receiving still
  gets a loopback packet. The per-class counts used to call `WSACleanup()` on
  every 0 -> 1 -> 0 cycle and tear Winsock down for the whole process. Only
  Windows can fail it; elsewhere the same steps run and pass.
- `tcpClientSigpipe/` — *(POSIX)* `TcpClient::send()` to a peer that reset
  the connection returns false instead of raising SIGPIPE, which killed the
  process without a trace (#254). One part keeps `connected_` set (no receive
  thread) so every send reaches the dead socket; one races the receive thread.
- `tcpClientIsolation/` — each TcpClient's `onReceive` gets only its own
  bytes (#254): two clients with different receive buffer sizes take 8 MB each
  from two loopback peers. The receive buffer used to be one function-local
  static shared by every client's receive thread.
- `tcpClientReconnect/` — `TcpClient::connect()` after the peer closed the
  connection reconnects (#254); it used to assign the new receive thread over
  the old, still-joinable one (`std::terminate`). Also checks that 20
  reconnects leak no descriptors and that a reconnect from an inline
  `onDisconnect` or `onReceive` listener leaves exactly one receive thread
  (both counted on Linux), that reconnecting through `connectAsync()` works,
  and that refused attempts release the old socket (counted on Linux) before
  a later `connect()` succeeds. With an auto-reconnect `onDisconnect`
  listener attached, `disconnect()` from another thread reports exactly one
  "Disconnected by client" and leaves no connection: the receive thread
  used to report the EOF of `disconnect()`'s own shutdown as a remote close,
  and the listener reconnected while `disconnect()` was joining that thread.
  Destroying a client whose listener reconnects on every `onDisconnect`
  finishes without any `onDisconnect` and without reconnecting: the
  destructor does not notify. When an `onError` listener reconnects, the
  replaced attempt reports no `onConnect(false)` (#393), through
  `connectAsync()` and without threads; a refused `connectAsync()` with no
  reconnect reports `onConnect(false)` exactly once.
- `mcpHttpGuard/` — the MCP HTTP server refuses browser-driven requests
  (#238): a non-loopback `Host`, a foreign `Origin` (403) and a non-JSON
  `Content-Type` (415), and checks the bearer token on `/mcp` (401). It also
  reports a port that is already in use: on a fixed port another server
  listens on, `mcp::startHttpServer()` fails to bind, logs exactly one
  "Failed to bind" error through the Logger, and the other server keeps
  answering every request.
- `mcpOccludedWindow/` — the MCP screenshot tools and hidden secondary
  windows (#347): `tc_list_windows` reports `Window::isOccluded()` as
  `occluded` on each secondary entry (none on the main one), and
  `tc_get_screenshot` / `tc_save_screenshot` fail at once with a specific
  error for a window whose flag is set, instead of waiting 5 s for a frame
  it will not render. A window whose flag is not set is unchanged: the
  request is deferred to its tick and, with no tick, answered by the 5 s
  timeout. Headless: the flag is driven through the test seam
  `internal::windowOccludedHookForTests()`; the native flags (macOS
  occlusionState, Win32 `WM_SIZE` / `DXGI_STATUS_OCCLUDED`, X11 `WM_STATE` /
  `VisibilityNotify`) are checked by hand.
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
- `entryStacks/` — push/pop containment per entry point (#349): a push that
  the prelude (`runOnMainThread` work), `update()` (synced, independent VSYNC
  and every fixed-Hz step, headless), the App's `setup()` or `exit()` leaves
  open is popped when it returns, back to the depth it was entered at (not
  0), with a warning naming it (`update() ended with 1 pushMatrix() ...`),
  rate-limited per entry point; values set outside a push carry on. Checks
  run in a release build only (headless `pushMatrix()` reaches sokol_gl).
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
- `tcpServerClients/` — `TcpServer` client bookkeeping: the threads of a
  client that leaves (closes or resets) are joined while the server runs,
  not held until `stop()` (on Linux the address space stays flat over 200
  clients; an unjoined thread leaves `/proc/self/task` but keeps its stack
  mapped), and joined by an idle server too, with no later connection to
  prompt it; `start(port, N)` closes connections beyond N, logs one warning per
  burst and hands a freed slot to the next client; `start(port)` has no limit.
  Listeners that tear down from their own thread: `disconnectClient()` of its
  own client in `onReceive` (destroying the server then waits for that
  thread), `stop()` in `onReceive`, and `stop()` in `onClientConnect` on the
  accept thread, which closes the listening socket before it returns (the old
  port refuses connections; on Linux, where `shutdown()` alone already does
  that, the socket's descriptor must be gone too), where `start()` is
  refused, and after which the accept thread disconnects the client once the
  listener returns. A listener's teardown waits for none of the server's
  threads, and the server's destruction waits for all of them: `stop()` in
  `onError` after a send timed out mid-payload (the receive thread removes
  the client meanwhile) returns and a later `start()` works (SKIP where the
  send is not cut mid-payload, as Winsock may take the whole payload in one
  `send()`);
  `disconnectClient()` of its own client, or `stop()`, in `onSendComplete`,
  with the server destroyed while that listener still runs (the destruction
  waits for the writer); two `onReceive` listeners disconnecting each
  other's client both return; two `onReceive`, or two `onSendComplete`,
  listeners each calling `disconnectAllClients()` both return and the
  server keeps running; and `disconnectAllClients()` on another
  thread, while a client connects, returns and leaves that client
  connected. `stop()` on several threads at once returns on all of
  them: from `onClientConnect` on the accept thread while another client's
  thread is parked in `onReceive` or `onSendComplete` and calls it too
  (either one first); from two clients' `onReceive`, or two `onSendComplete`;
  from a plain thread together with `onSendComplete` (the listener calls it
  while the stop hook holds the plain one after its accept-thread join, and
  the plain one returns only once that listener is done); from
  `onClientConnect` once a plain thread's `stop()` has taken the accept
  thread and waits for it (ordered by
  `internal::setTcpServerAcceptTakenHookForTests()`; the listener's `stop()`
  still closes the listening socket before it returns); and from two plain threads while the
  accept thread is held in a listener (neither throws). Every client ends up
  disconnected. `start()` while another thread's `stop()` is still waiting
  for the accept thread waits for it too, and the restarted server accepts
  clients; so does `start()` while that `stop()` has joined the accept thread
  but not yet disconnected the clients (held there by
  `internal::setTcpServerStopHookForTests()`), and a client of the restarted
  server stays connected with no `onClientDisconnect`. `start()` from
  `onReceive` is refused and the server keeps running. A watchdog turns a
  hang there into a FAIL line and a non-zero exit.
  Linux only, in forked children: failing `accept()` calls (descriptors
  exhausted under a low `RLIMIT_NOFILE`) back off instead of spinning, log
  once and reach `onError` again after the 5 s interval if they persist; a
  thread that cannot start closes that connection and reports it through
  `onError` instead of ending the process — the writer under `RLIMIT_NPROC`,
  the receive thread through a `pthread_create` wrapper in the test binary
  that fails one chosen call (the client is announced, then disconnected, and
  that is reported even right after a different failure; the accept thread
  does not wait for that client's writer, whose listener waits for the
  disconnect); and when the
  accept thread cannot start, `start()` returns false, reports it once and
  leaves nothing listening, and a later `start()` on that port works. Each
  server binds a port the OS just handed out, not a fixed one.
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
- `pcmByteOrder/` — `SoundBuffer::loadPcmFromMemory()` decodes both byte
  orders (#419). Known 16-bit byte pairs, little- and big-endian, with bytes
  of 0x80 and above in either position, give exactly `value / 32768` (the old
  big-endian swap sign-extended, so `01 80` came out as -128 instead of 384);
  big-endian stereo keeps its channel order. Known 32-bit float byte quads in
  both orders, with bytes of 0x80 and above in every position, keep their
  exact bits. Both also from a data pointer that is not aligned to the
  sample size. The tcxHap side (`twos` / `fl32` files) is in
  `addons/tcxHap/tests/`.
- `fontSfntCheck/` — font data is checked before it is given to stb_truetype:
  `FontAtlasManager::setupFromMemory()` returns false with a warning for 0
  bytes, a `.ttc` header whose font count or offset points outside the data,
  a table directory or a table past the end of the data (offset + length is
  checked in 64 bits), a missing required table (a directory entry at
  offset 0 counts as missing, as in stb), head / hhea / maxp /
  cmap shorter than the fields stb reads, cmap encoding records or a used
  subtable offset past cmap, `numberOfHMetrics` outside 1..numGlyphs, a short
  hmtx or loca, an unknown loca format, a loca entry past glyf or below the
  one before it, and a CFF CharStrings INDEX whose count or offset array
  cannot be read within the CFF table; returns false for a CFF table of 0 to
  3 bytes, a CFF INDEX with an offset size outside 1..4 and an empty Top
  DICT, in Debug and Release builds alike; and for
  copies of a TrueType, a CFF and a collection font cut short in the header,
  the table directory and each table. A glyph index from the cmap past
  numGlyphs (for CFF, past the number of CharStrings) and a codepoint above
  U+10FFFF draw as .notdef. Valid fonts load, including
  `numberOfHMetrics == numGlyphs`, a cmap format 12 subtable, a table of
  length 0 and tables that share bytes. Also guards the TrussC patches in
  `stb_truetype.h`: CFF data is read within the CFF table's length; CFF
  vertex counting (a glyph over the vertex limit, one whose closing vertex
  is the one over it, one far over any limit through nested subroutines,
  and one whose vertex array cannot be allocated come back empty); and the
  flattened point count (a glyph with more points than the limit is not
  drawn, one at the limit is). The test lowers these limits through
  `internal::setStbttLimitsForTests()`. A glyph with a one-point contour
  loads and rasterizes. Run the core tests under AddressSanitizer after
  changing stb_truetype or `stb_impl.cpp`; CI does not build with ASan.
  For this test, from the repository root:

  ```sh
  tools/bin/trusscli update -p core/tests/fontSfntCheck --tc-root "$PWD" --ide cmake
  cd core/tests/fontSfntCheck
  cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
    -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address
  cmake --build build-asan -j4
  ./bin/fontSfntCheck
  ```

  (Prefix the last line with `setarch -R` where ASan fails to start because
  of the kernel's address randomization.)
  The fonts are built at runtime; fonts installed at
  the usual system paths are also loaded and cut short when present.
  `fontSfntCheck --dump <files>` prints glyph metrics to compare two builds.
- `extensionCase/` — loaders and savers match the file extension
  case-insensitively; file names keep their case as written (#305). `Sound::load()` picks
  its decoder for `.Wav` / `.Mp3` / `.OgG` / `.Flac` / `.M4a` as
  `SoundBuffer::load()` and `loadStream()` do (garbage under such a name
  reaches the decoder instead of failing as an unsupported extension), and
  `Pixels::save()` writes JPEG / BMP / PNG for `.Jpg` / `.jPeG` / `.Bmp` /
  `.PnG` (checked by magic bytes) under exactly the name given. On a
  case-sensitive file system `a.wav` and `a.WAV` load as two files; otherwise
  that part prints SKIP. The hot reload watcher's side is in
  `hotReloadLifecycle/`; the per-platform screenshot savers are not covered
  (they need a framebuffer).
- `trusscliPresets/` — trusscli's project files (#350): `update`, `addon add`
  and `addon remove` keep the project's IDE, web / android / ios targets and
  web backend. Its `local.cmake` compiles trusscli's own sources
  (`tools/src`, without trusscli's `main.cpp` and GUI) into the test. The IDE
  written by `ProjectGenerator` as `"vendor": {"trussc": {"ide": ...}}` reads
  back for every IDE, and so do the targets and the web backend; the presets
  fill the settings and explicit flags win (`--ide`, `--no-web` /
  `--no-android` / `--no-ios`, `--web` with `--no-web` is an error); without a
  `CMakePresets.json` the old defaults stay (vscode, native only, WebGPU).
  Saved settings that cannot be used are reported as warnings: a file that
  does not parse or cannot be read, wrongly typed entries, an unknown IDE id,
  and an IDE this OS cannot generate (xcode off macOS, vs off Windows).
  `TC_WEB_BACKEND` is read the way CMake builds it, as a string or as
  `{"type": ..., "value": ...}`: `"WGPU"` (or unset / null) is WebGPU, every
  other value GLES3, with a warning unless it is `"GLES3"`.
  `prepareRegeneration()`, the settings setup that `update`, `addon add` and
  `addon remove` all call, is checked directly (presets kept, flags win,
  defaults without a file, warnings and summary line). The toolchainFile of a
  kept web / android preset survives a regeneration from a shell without
  emsdk / the NDK when the saved file still exists (the test sets `EMSDK`,
  `PATH`, `ANDROID_NDK_HOME` / `ANDROID_HOME` per case), and a kept target
  whose configure fails (a toolchain that fails on purpose; needs `cmake` in
  `PATH`) is a warning naming `trusscli update --no-android`, while the same
  target passed as a flag fails the update.
  For `trusscli build` / `clean` (#357): one preset-to-build-folder mapping
  (`ios` -> `xcode-ios`) that the written presets, `build` and `clean` follow;
  a build folder without a CMake cache, or with only the cache of a failed
  configure, is configured first with one message, and a cache that already
  holds what was asked for is not; a Visual Studio update that removed a
  pinned MSVC / Windows SDK / ninja path is found (fake filesystem, and on
  Windows through the real writer), only a native Windows build refreshes the
  presets, and the refresh replaces only the `windows` preset's pins (the
  no-VS fallback entry pins nothing).
  Not covered: the argument parsing and output of the commands in
  `tools/src/main.cpp` (including the ones that call the build / clean
  helpers), the IDE files, the native CMake configure, and Visual Studio
  detection on a real toolchain change (manual Windows check).
