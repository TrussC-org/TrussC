# tcxInputReplay

[English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md)

Record TrussC windows' input and replay it in real time to revisit an interactive session after code changes. Scene marks let playback wait for the app when scene timing changes. This is an opt-in debugging addon; input alone is not a deterministic snapshot of the app. There is no full-state snapshot API.

Add `tcxInputReplay` to `addons.make` and include `<tcxInputReplay.h>`. All calls run on the main thread. Call `setup()` once in `tcApp::setup()`; it selects that window and releases its scoped listeners on app cleanup.

```cpp
void tcApp::setup() {
    tcx::inputreplay::setup();
}
```

## Use the input replay toolbar

`setup()` adds a small toolbar at the top center of its window. Record starts a temporary file; Stop recording opens the native save dialog and saves JSONL. Cancel discards a toolbar recording. When code started a file recording, cancel keeps the original file. Stopping a ring from the toolbar offers the same save dialog. Code calls to `stopRecording()` and `flush()` never open dialogs.

```cpp
void tcApp::setup() {
    tcx::inputreplay::setup();
}
```

Failed saves keep the recording and leave Save available for retry; the Error log includes its path. Toolbar saving requires a desktop file dialog. On Web and Android, Save keeps the staging file and logs a Warning with its path on each attempt. Use code to close a file recording or save a ring without a dialog:

```cpp
void tcApp::saveCapture() {
    tcx::inputreplay::stopRecording();
}
```

For a ring, save directly to a chosen path:

```cpp
void tcApp::saveCapture() {
    tcx::inputreplay::flush("session.jsonl");
}
```

Toolbar staging files live in the `setFlushDirectory()` folder (default: `input-replay` under the data folder), with owner-only permissions on POSIX. A crash or cleanup with an unsaved recording can leave a staging file there. A toolbar recording stopped by a recording limit remains available through Save. Starting a new recording discards the previous unsaved staging file with a Warning.


Open chooses a recording; the toolbar shows its filename and hovering it shows the full path. Dropping files selects the first `.jsonl` path (case sensitive) on the setup window, without starting playback. The app still receives every drop, recording continues, and the drop is recorded normally. Other extensions are ignored by the addon. Open and drop select a path; Play validates and starts it.

F9 toggles only toolbar visibility, is enabled by default, and ignores key repeat. On macOS, press it with fn, or change the key. Use core's integer key constants to change it or disable it for an app that already uses that key.

```cpp
void tcApp::configureReplay() {
    tcx::inputreplay::setHotKey(tc::KEY_F8);
    tcx::inputreplay::setHotKeyEnabled(false);
}
```

Playback hides the toolbar and restores its previous visibility when it ends or is stopped. The hotkey still toggles it during playback. Replayed input can operate ImGui, including the toolbar; there is no API to isolate the toolbar from replayed input.

The addon loads tcxImGui automatically. Apps using `imguiBegin()` / `imguiEnd()` share that frame automatically; otherwise the addon starts its own frame before rendering. No manual toolbar draw call is needed. The toolbar is attached to the setup window's ImGui context; keep that context initialized until app cleanup (there is no API to reattach after an explicit `imguiShutdown()`).


Hot-reload builds must list tcxImGui explicitly in `addons.make`:

```makefile
tcxInputReplay
tcxImGui
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

## Retain recent input in a ring

Ring recording keeps the last N minutes in memory for incident capture, bounded by a record capacity and the JSONL byte limits. No file is opened until flush. Both duration and count apply: this keeps at most five minutes and 100,000 input/mark records.

```cpp
void tcApp::beginCapture() {
    tcx::inputreplay::startRecording(tcx::inputreplay::RecordingMode::Ring, std::chrono::minutes(5), 100000);
}
```

The duration is `std::chrono::milliseconds` (1 ms through seven days); chrono minutes/seconds convert directly. Capacity excludes header/end, defaults to 999,998, and must be 1–999,998. Older complete records are evicted when time, count or bytes reach a bound. An oversized individual line stops recording with an Error. There is no API to store an oversized line.

`flush(path)` writes the retained segment and stops recording, including on a file error. It creates parent folders and opens no dialog. Restart explicitly to capture another incident.

```cpp
void tcApp::saveIncident() {
    tcx::inputreplay::flush("incidents/session.jsonl");
}
```

`flush()` uses a Unix-microsecond timestamped filename in `input-replay` under the data folder. Set the folder after setup if desired.

```cpp
void tcApp::configureCapture() {
    tcx::inputreplay::setFlushDirectory("incidents");
}
```

Save with the automatic name:

```cpp
void tcApp::saveIncident() {
    tcx::inputreplay::flush();
}
```

Flush writes a valid header/end and rebases time/frame to the first retained record. If the first mark was evicted, playback starts at the retained segment; it cannot restore the earlier scene. There is no app-state restoration API. `stopRecording()` discards a ring; save it with `flush()` instead.

```cpp
void tcApp::saveIncident() {
    tcx::inputreplay::flush();
}
```

Calling flush while stopped logs a Warning every time. File recording continues to use `startRecording(path)` and `stopRecording()`; flush is for ring recording. Start the ring before flushing.

```cpp
void tcApp::beginCapture() {
    tcx::inputreplay::startRecording(tcx::inputreplay::RecordingMode::Ring, std::chrono::minutes(5), 100000);
}
```

## Map multiple windows explicitly

`setup()` still maps `main` to its current window. To capture multiple windows, create them first and pass stable keys with Window pointers to setup. Declare `std::shared_ptr<tc::Window> preview;` in tcApp and assign the result of `tc::createWindow(settings)` before this setup call.

```cpp
void tcApp::setup() {
    tcx::inputreplay::setup({{"main", getWindow()}, {"preview", preview.get()}});
}
```

Only mapped windows are captured. Use the same keys when replaying, even if Window creation order or titles change. Missing, closed or duplicate keys/destinations reject setup/play; a mapped window closing stops playback with an Error, including while waiting at a mark. Map each open destination once before recording or playback.

```cpp
void tcApp::setup() {
    tcx::inputreplay::setup({{"main", getWindow()}, {"preview", preview.get()}});
}
```

Mapping stays fixed until app cleanup; there is no live remapping API. Input coordinates and scaling use each key's own recorded and current dimensions. Core's open-window registry tracks lifetime; transient native IDs and titles are never persisted as keys.

## Control input replay through MCP

Setup registers five tools at `Control` level. Registering them does not grant access or start a server. Enable the existing MCP Control grant in a debug app to use them.

```cpp
void tcApp::enableRemoteDebugging() {
    if (debugBuild) tc::mcp::setToolLevel(tc::mcp::ToolLevel::Control);
}
```

| Tool | Arguments |
| --- | --- |
| `tcx_inputreplay_start_recording` | `path` for a file; omit it for a ring, with optional `durationMs` (default 300000) and `capacity` (default 999998) |
| `tcx_inputreplay_stop_recording` | none; discards a ring |
| `tcx_inputreplay_play` | `path`, optional `scale`: `none`, `stretch`, `fit` |
| `tcx_inputreplay_stop_playback` | none |
| `tcx_inputreplay_flush` | optional `path`; omission uses the configured folder |

Control functions return `bool`; tools return `status: "ok"` or error text. MCP calls themselves are not recorded. This is a debugging tool: avoid enabling it in distributed apps. Guard setup and the Control grant in your debug build.

```cpp
void tcApp::setup() {
    if (debugBuild) tcx::inputreplay::setup();
}
```

Declare a build-controlled `bool debugBuild` member; only call `enableRemoteDebugging()` in that debug build.

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

UTF-8, one object per line. The first object is a header with `type: "header"`, `version: 1`, `app`, `trussc`, `started` (Unix milliseconds as a string), `fps`, and a `windows` array with unique keys (default `key: "main"`), `width`, `height`, `dpi`, `pixelPerfect`. Width/height are the recording's App coordinate extents.

Every subsequent record has `type`, elapsed `ms` and relative `frame`. Input also has its mapped `window` key (default `"main"`), `injected` and `tag`. Mouse input has `x`, `y`, `mods` (shift=1, ctrl=2, alt=4, super=8); buttons add `button` (0–2), scroll adds `scrollX`, `scrollY`. Keys have `key`, `mods`, `repeat`; characters have `codepoint`, `mods`, `repeat`. Touch has `canceled` and `touches` (`id`, `x`, `y`, `pressure`, `changed`). Drops have `x`, `y`, `files`. Marks have `name`; the final record has `type: "end"`.

Playback validates the whole file before injecting anything, reporting errors with a line number. Limits are 64 KiB per line excluding LF, 128 MiB per file including line endings, 1,000,000 objects including header/end, and seven days for `ms`. Timestamps and frames must be nondecreasing. Invalid/missing fields, non-finite or out-of-domain values, unknown versions/event kinds, missing end records and trailing records fail validation. Failed or interrupted recordings can be incomplete; there is no recovery API.

## Example and tests

`example-inputReplay` shows recording on R, stopping on S, playback on P and a scene mark on M. `tests` runs headlessly and covers file validation, exact bounds, round trips, scaling and fake-clock marks. `tests-display --display-check` records injected input and compares replay delivery on a display (Xvfb works on Linux).

`tests` also covers toolbar controls, dialogs through injected file choices, drop selection and auto-hide. `tests-display --toolbar-check` and `--toolbar-app-check` verify drawing without and with an app ImGui frame on a display server. Native dialog interaction requires a human check.
