# App::getWindow() verification app

`repro/appGetWindowTest` checks what `App::getWindow()` returns across the
whole attach / swap / detach / close / destroy cycle of secondary windows.
This branch (`test/app-get-window`) sits on top of `feat/app-get-window`; the
app is not part of that branch.

## Setup

1. `trusscli new appGetWindowTest`, then replace the generated
   `src/tcApp.h` / `src/tcApp.cpp` with the files from
   `repro/appGetWindowTest/src/`. Keep the generated `main.cpp`.
2. Configure against THIS checkout's core:
   `cmake --preset <platform> -DTRUSSC_DIR=<this-checkout>/core`, then build.
   Built against any other core it does not compile (`getWindow` is not a
   member of `App`) — so a successful build already proves the right core.
3. Run it. It needs no input: it opens and closes two extra windows by itself,
   logs every check, prints `RESULT: ...` and quits within a few seconds.

## Expected result

```
[PASS] T1 .. T16
RESULT: 16 passed, 0 failed
```

plus exactly one expected error line in the middle (T9 deliberately makes a
rejected attach):

```
[ERROR] [Window] setApp(): this App already drives another window
```

What the checks cover:

| id | situation | getWindow() must return |
|----|-----------|-------------------------|
| T1, T4, T16 | the main App started by runApp() | nullptr |
| T2 | an App never attached to a window | nullptr |
| T3 | `subApp->getWindow()` called from the MAIN window's update | the sub App's window (not the active one) |
| T5, T6, T8 | inside a sub App's own update() | its window, agreeing with internal::currentWindow() |
| T7 | two secondary windows at once | each App its own window |
| T9 | rejected setApp() (App already drives another window) | unchanged for both Apps |
| T10, T11 | setApp(other) on a live window | old App nullptr, new App the window |
| T12, T13 | setApp(nullptr), then re-attach | nullptr, then the window again |
| T14 | window closed while the App is still held | nullptr |
| T15 | Window object destroyed while the App is still held | nullptr |

If the app never quits on its own, check for a locked screen or a throttled
display first: the final step waits ~120 main-window frames before exiting.
