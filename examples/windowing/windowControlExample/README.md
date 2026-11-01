# windowControlExample

Per-window window control on a **secondary** window: fullscreen, screenshot,
recording and frame-rate — all driven through the context-aware global
functions, which use the calling App's own Window.

- **Launch window** (800x600, dark, red badge): instructions.
- **Secondary window** (500x400, blue, moving shape): the window the per-window
  controls act on.

Runtime retains both Windows and their Apps. Releasing a handle does not close
its Window. Closing one leaves the other running; closing the last asks
`exitRequested` before normal shutdown. Code `Window::close()` skips
`closeRequested` but still asks `exitRequested` for the last Window or one with
`setQuitOnClose(true)`. A veto keeps every currently open Window open.

## Keys

`keyPressed` is per-window, so the **focused** window is the one that reacts —
the context-aware global functions use that App's Window.

| Key | Action |
|-----|--------|
| `f` | Toggle fullscreen on the **focused** window (works from either window) |

Secondary (blue) window only:

| Key | Action |
|-----|--------|
| `s` | `saveScreenshot("winctrl_secondary.png")` from this window |
| `r` | Start / stop a 3-second `startRecording("winctrl_secondary.mp4", 3.0f)` from this window |
| `1` / `2` / `3` | Set the secondary window's fps to 15 / 30 / vsync (`Window::setFps(VSYNC)`) |

## Autotest

With `TC_WINCTRL_AUTOTEST=1` the secondary window runs a timed script instead of
waiting for keys (t in seconds, measured inside the secondary's own tick):

- t=1 log marker + `saveScreenshot("winctrl_auto_pre.png")`
- t=2 `setFullscreen(true)`
- t=4 `setFullscreen(false)`
- t=5 `saveScreenshot("winctrl_auto_post.png")`
- t=6 log `AUTOTEST DONE`

Each step logs the size the secondary window sees. Both PNGs are written next to
the executable's data path and should be BLUE-dominant; the mid-fullscreen state
is observable via the logged sizes.
