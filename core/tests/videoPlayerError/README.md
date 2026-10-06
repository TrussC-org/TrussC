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
allCoreTests videoPlayerError --video-check path/to/one-bad-packet.mov bad-packet
allCoreTests videoPlayerError --video-check path/to/two-bad-packets.mov bad-packets
```

The optional GPU check compares FBO pixels before and after an injected error.
The real-player check waits for explicit failure or normal EOF. Run graphical
checks under Xvfb on Linux and apply an external process timeout for hangs.
A truncated file that the backend treats as normal EOF is intentionally not
classified as an error; choose one that produces an explicit read failure.
The `bad-packet` mode requires one invalid-packet/frame warning (except on
Windows, where Media Foundation can hide the packet internally), no error event
or error log, and playback reaching the last frame and normal EOF. Linux and
the separate tcxHap tests still require the warning. The error mode checks one
runtime error log and that the player stays loaded. Readiness, retained
pixels/texture, and a recovery seek uploading its poster before `play()` are
required only if a picture (including an auto poster) was available before the
error. With or without a prior picture, seeking and the subsequent update must
return safely, retain the loaded/stopped/error state and message, and produce
no duplicate error event/log.
The `bad-packets` mode uses two consecutive invalid packets, checks one warning
for the burst, waits past the five-second warning interval, then replays and
checks that the next warning includes the accumulated skip count.

To recreate the Linux fixture used here (a HAP MOV with its third video sample
cut in half), run from the repository root:

```sh
python3 core/tests/videoPlayerError/make-fixture.py \
  addons/tcxHap/tests/bin/data/sine_sowt.mov truncated.mov
python3 core/tests/videoPlayerError/make-fixture.py --bad-packet \
  addons/tcxHap/tests/bin/data/sine_sowt.mov one-bad-packet.mov
python3 core/tests/videoPlayerError/make-fixture.py --bad-packets \
  addons/tcxHap/tests/bin/data/sine_sowt.mov two-bad-packets.mov
```

Real display/platform checks required by the Decision remain manual: run a
truncated supported video on Windows, macOS/iOS, Linux and Web, and tcxHap.
Check one `onError` and one `logError` line per failure, callback thread,
`hasError()`, `!isPlaying()`, retained picture and load
state, and app-directed retry/reload. On Windows also check an explicit
resource/device-loss signal. Normal EOF and temporary stalls must not report
an error.

Also check that a file containing one bad packet keeps playing on each platform.
Web error handling must be checked in a real browser.
