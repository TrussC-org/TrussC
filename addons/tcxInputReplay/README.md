# tcxInputReplay

A small input recorder and replay toolbar built from plain Nodes. Add `Toolbar` to an App to record mouse, keyboard, touch, and file-drop input in that window. Playback injects events on their recorded schedule and waits at scene marks. JSONL version 1 remains compatible with existing single-window recordings; there is no ImGui dependency.

This addon uses `Window::injectInput`. If you want to build your own input-injection tool, read `Player.h`.

The example draws a light-blue mouse trail and orange click dots that fade over time, making playback visible.

## Add the toolbar to an App

Include `<tcxInputReplay.h>` and use the example's `using namespace std;`, `using namespace tc;`, and `using namespace tcx;`. Declare these members in `tcApp`:

```cpp
shared_ptr<inputreplay::Toolbar> replay;
vector<Vec2> dots;
```

Add the toolbar in `setup()`. Use `isPointerOver()` in the App's mouse handler to keep toolbar clicks out of the drawing:

```cpp
void tcApp::setup() {
    // F9 shows and hides the toolbar. With no argument, no key is assigned.
    replay = make_shared<inputreplay::Toolbar>(KEY_F9);
    // Optional: start and stop recording from the keyboard (no key by default).
    // replay->setRecordKey(KEY_F8);
    addChild(replay);
}
void tcApp::mousePressed(Vec2 pos, int button) {
    if (replay->isPointerOver()) return;   // the click is for the toolbar
    dots.push_back(pos);
}
```

The toolbar is placed once at its parent's top center. Move it with the normal Node API. Record becomes a red Stop button while recording; Play becomes a blue Stop button while playing. Open or dropping a `.jsonl` file selects a recording, and hovering over its filename shows the full path. Toolbar Stop (including a ring flush) immediately opens the OS save dialog. Saving moves the staging file from the data directory's `input-replay` folder to the chosen path; across volumes, it copies then deletes the staging file. Cancel deletes the staging file. Save appears only after a failed save so you can retry. Code calls to `stopRecording()` and `flush()` never open a dialog.

Names without an extension gain `.jsonl`. If that appended path already exists, a Warning names the path and Save lets you choose another name; the pending recording stays intact.

## Record input to a file or ring

File paths resolve against the App's data directory. File recording closes with an end record:

```cpp
replay->startRecording("session.jsonl");
replay->stopRecording();
```

Ring recording retains the latest duration in memory. Flush writes that interval and stops recording; `stopRecording()` discards an active ring:

```cpp
replay->startRecording(inputreplay::RecordingMode::Ring, chrono::seconds(30));
replay->flush("last-30-seconds.jsonl");
```

`isRecording()` reports either mode. `getRecordedCount()` counts input events, excluding headers, marks, and end records. In ring mode it counts only retained input. Each Toolbar records only its own window, including toolbar clicks. Injected events are omitted.

## Play input and synchronize scenes

Play uses the same window as the Toolbar. `ScaleMode::None` keeps coordinates, `Stretch` scales each axis, and `Fit` preserves aspect ratio with centered letterboxing:

```cpp
replay->play("session.jsonl", inputreplay::ScaleMode::Fit);
replay->mark("scene");
```

Call `mark("scene")` at the corresponding scene transition while recording and while playing. Playback waits for the next matching mark; an early mark skips the remainder of the previous scene. `stopPlayback()` stops playback and `isPlaying()` reports its state. Replayed events carry `injected = true` and the source tag `"replayed"`; Buttons and toolbar hotkeys ignore them.

## Enable MCP controls explicitly

Only a Toolbar whose `registerMcpTools()` is called is controlled by the addon's MCP tools:

```cpp
replay->registerMcpTools();
```

The five Control-level tools are `tcx_inputreplay_start_recording` (optional `path`, otherwise ring with optional `durationMs`), `tcx_inputreplay_stop_recording`, `tcx_inputreplay_play` (`path`, optional `scale`), `tcx_inputreplay_stop_playback`, and `tcx_inputreplay_flush` (`path`). The most recent explicit registration selects the controlled instance. Recording and playback otherwise use no global addon state.
