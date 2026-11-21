# tcxInputReplay

[English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md)

Record one TrussC window's input and replay it in real time to revisit an interactive session after code changes. Scene marks let playback wait for the app when scene timing changes. This is an opt-in debugging addon; input alone is not a deterministic snapshot of the app. There is no full-state snapshot API.

Add `tcxInputReplay` to `addons.make` and include `<tcxInputReplay.h>`. All calls run on the main thread. Call `setup()` once in `tcApp::setup()`; it selects that window and releases its scoped listeners on app cleanup.

```cpp
void tcApp::setup() {
    tcx::inputreplay::setup();
}
```

## Record and replay input

Paths resolve through `getDataPath()`. Start recording with a destination path, then stop to write the end record.

```cpp
void tcApp::keyPressed(int key) {
    if (key == 'R') tcx::inputreplay::startRecording("session.jsonl");
    if (key == 'S') tcx::inputreplay::stopRecording();
}
```

Play a complete recording with its original coordinates, or select a scaling mode.

```cpp
void tcApp::keyPressed(int key) {
    if (key == 'P') tcx::inputreplay::play("session.jsonl", tcx::inputreplay::ScaleMode::Fit);
}
```

- `play(path)` defaults to `ScaleMode::None`; a different window size logs a Warning.
- `ScaleMode::Stretch` scales x and y separately from recorded dimensions to current dimensions.
- `ScaleMode::Fit` preserves aspect ratio and centers the recorded canvas. Positions outside the recorded canvas pass through unchanged, rather than being moved into the letterbox.
- Dimensions and coordinates use logical points normally and framebuffer pixels in pixelPerfect mode. A resize applies new factors from the next input event. Scroll deltas do not scale.
- Different DPI or pixelPerfect metadata logs a Warning. The addon has no API to change DPI or pixelPerfect mode; configure matching settings before launch. `None` retains the recorded numeric coordinates.

```cpp
void tcApp::keyPressed(int key) {
    if (key == 'P') tcx::inputreplay::play("session.jsonl", tcx::inputreplay::ScaleMode::None);
}
```

Mouse moves, drags (stored once as moves), buttons, scroll, keys including repeat, characters, modifiers, touch IDs/cancellation/pressure and file-drop paths are recorded. Injected input retains `injected` and `tag`; this addon's synchronous replay is omitted. Touch-generated mouse input is omitted because touch injection recreates it. Capture precedes ImGui's typed input consumption.

File drops contain paths as data; playback does not recreate files. The addon has no file-restoration API. Core's file-drop arguments have no provenance API, so recorded drops use `injected: false` and an empty tag. The synchronous replay guard still prevents re-recording them. Scroll position is saved as data; the injection API uses the destination window's current pointer position and has no explicit scroll-position argument.

## Synchronize scenes with marks

Call the same mark when the app reaches each scene, during both recording and playback.

```cpp
void tcApp::enterScene() {
    tcx::inputreplay::mark("scene2");
}
```

Playback waits indefinitely at the next recorded mark for the matching app mark. An early matching mark drops the remaining inputs in the previous scene; a late mark releases the wait. Either restarts the clock at that mark, and subsequent input uses its recorded offset from the mark. Other names do not advance playback. Marks do not restore app state: there is no API to reconstruct a scene from a recording.

Real input mixes with replayed input in arrival order and does not stop playback. Stop explicitly when the app needs to.

```cpp
void tcApp::stopDemo() {
    tcx::inputreplay::stopPlayback();
}
```

Natural completion emits `onPlaybackEnd()`. Keep the RAII listener as a `tcApp` member; explicit stop and errors do not emit it. The app can loop by starting playback in this callback.

```cpp
void tcApp::setup() {
    tcx::inputreplay::setup();
    endListener = tcx::inputreplay::onPlaybackEnd().listen([] {
        tcx::inputreplay::play("session.jsonl");
    });
}
```

Declare `tc::EventListener endListener;` in `tcApp`. Repeated setup/start/stop calls log a Warning every time. File and injection failures log Error and stop recording/playback. Recording cleanup writes an end record when possible.

## Keep recording files private

Recorded characters can include passwords. Treat recordings as sensitive files and stop recording before entering secrets. The addon has no redaction or encryption API.

```cpp
void tcApp::beforePasswordEntry() {
    tcx::inputreplay::stopRecording();
}
```

Input alone does not reproduce random state, clocks, network, cameras or sensors. Restore those app-specific conditions separately; the addon has no snapshot API for them.

## JSON Lines version 1

UTF-8, one object per line. The first object is a header with `type: "header"`, `version: 1`, `app`, `trussc`, `started` (Unix milliseconds as a string), `fps`, and one `windows` entry: `key: "main"`, `width`, `height`, `dpi`, `pixelPerfect`. Width/height are the recording's App coordinate extents.

Every subsequent record has `type`, elapsed `ms` and relative `frame`. Input also has `window: "main"`, `injected` and `tag`. Mouse input has `x`, `y`, `mods` (shift=1, ctrl=2, alt=4, super=8); buttons add `button` (0–2), scroll adds `scrollX`, `scrollY`. Keys have `key`, `mods`, `repeat`; characters have `codepoint`, `mods`, `repeat`. Touch has `canceled` and `touches` (`id`, `x`, `y`, `pressure`, `changed`). Drops have `x`, `y`, `files`. Marks have `name`; the final record has `type: "end"`.

Playback validates the whole file before injecting anything, reporting errors with a line number. Limits are 64 KiB per line excluding LF, 128 MiB per file including line endings, 1,000,000 objects including header/end, and seven days for `ms`. Timestamps and frames must be nondecreasing. Invalid/missing fields, non-finite or out-of-domain values, unknown versions/event kinds, missing end records and trailing records fail validation. Failed or interrupted recordings can be incomplete; there is no recovery API.

## Example and tests

`example-inputReplay` shows recording on R, stopping on S, playback on P and a scene mark on M. `tests` runs headlessly and covers file validation, exact bounds, round trips, scaling and fake-clock marks. `tests-display --display-check` records injected input and compares replay delivery on a display (Xvfb works on Linux).

The toolbar, hotkey, ring buffer, file dialogs, multi-window mapping and MCP controls belong to stage B.
