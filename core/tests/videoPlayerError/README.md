# Video playback errors (#288)

The default headless test uses a `VideoPlayerBase` subclass. A joined worker
reports duplicate errors; `update()` delivers once on the main thread after
stopping, while retaining the loaded state, position and last picture. It also
checks retry, close/load, pending errors during moves, and close/reload from a
listener. There are no elapsed-time assertions.

Build with the repository's guarded build workflow. The test is automatically
included in `allCoreTests`, and can also be built as its own trusscli project.

```sh
allCoreTests videoPlayerError
allCoreTests videoPlayerError --gpu-check
allCoreTests videoPlayerError --video-check path/to/working.mov
allCoreTests videoPlayerError --video-check path/to/truncated.mov error
```

The optional GPU check compares FBO pixels before and after an injected error.
The real-player check waits for explicit failure or normal EOF. Run graphical
checks under Xvfb on Linux and apply an external process timeout for hangs.
A truncated file that the backend treats as normal EOF is intentionally not
classified as an error; choose one that produces an explicit decoder failure.

To recreate the Linux fixture used here (a HAP MOV with its third video sample
cut in half), run from the repository root:

```sh
python3 core/tests/videoPlayerError/make-fixture.py \
  addons/tcxHap/tests/bin/data/sine_sowt.mov truncated.mov
```

Real display/platform checks required by the Decision remain manual: run a
truncated supported video on Windows, macOS/iOS, Linux and Web, and tcxHap.
Check event count/thread, `hasError()`, `!isPlaying()`, retained picture and load
state, and app-directed retry/reload. On Windows also check an explicit
resource/device-loss signal. Normal EOF and temporary stalls must not report
an error.

A Node mock executes the Web backend's actual element-creation script and
checks error message/code capture, pause, preserved readiness, reset on load,
and rejection of stale events. From the repository root:

```sh
node core/tests/videoPlayerError/check-web-error.js
```
