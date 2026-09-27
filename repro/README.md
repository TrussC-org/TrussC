# App::setSize() verification app

`repro/appSetSizeTest` checks that `App::setSize()` resizes the App's OWN
window, whichever window's callback makes the call. This branch
(`test/app-setsize-own-window`) sits on top of `fix/app-setsize-own-window`;
the app is not part of that branch.

## Setup

1. `trusscli new appSetSizeTest --tc-root <checkout>`, then replace the
   generated `src/` with `repro/appSetSizeTest/src/` — **including main.cpp**
   (it sets the main window to 760x420 and reads `TC_PIXEL_PERFECT`).
2. Build. `getWindow()` must exist in the core you build against (main
   after #224 has it), otherwise it does not compile.
3. Run it. No input needed: it resizes both windows from both contexts,
   logs every check, prints `RESULT: ...` and quits after a few seconds.
4. Run it again with `TC_PIXEL_PERFECT=1` in the environment. On a HiDPI
   display (scale != 1) this is the run that checks the framebuffer ->
   logical conversion; at scale 1 it is identical to step 3.

To see the bug itself, build the same app against `main` instead: the
checks below fail there.

## How the checks work

Each cross-window call is compared with a **reference**: the same call made
from the App's own window context, which has always resized the right
window. So the expected value never has to be hard-coded — it holds in
pixel-perfect mode, and on platforms where the main window cannot be resized
(Linux: `setWindowSizeLogical` is not implemented) both sides are a no-op.
Each check also verifies that the OTHER window did not change. R1/R2 make
sure the references themselves resized something — a broken reference would
let S1/S3 pass for free (on macOS, `setWindowSizeLogical` used to resize the
FOCUSED window, so the main-window reference hit the second window).

| id | call | made from | must |
|----|------|-----------|------|
| R1 | `sub->setSize()` (reference) | second window | actually resize the second window |
| R2 | `mainApp->setSize()` (reference) | main window | actually resize the main window (skipped on Linux) |
| S1 | `sub->setSize()` | main window | resize the second window like the reference |
| S2 | (same) | | leave the main window alone |
| S3 | `mainApp->setSize()` | second window | resize the main window like the reference |
| S4 | (same) | | leave the second window alone |
| S5 | `unattached->setSize()` | second window | resize no window |
| S6 | (same) | | still set that App's own Node size |
| S7 | `setApp(unattached)` after S5 | main window | not resize the window: a setSize() made before setApp() is not carried over |
| S8 | (same) | | give the App the window's size (the window's size wins, like the main window) |

## Expected results

With the fix: `RESULT: 10 passed, 0 failed` on macOS / Windows,
`9 passed, 0 failed` on Linux (R2 skipped), in both runs.

Without the fix (`main`), measured on Linux (GNOME, X11):

```
[FAIL] S1  ... (got 360x240, own-context reference 480x300)
[PASS] S2        <- Linux cannot resize the main window, so the misroute is invisible
[PASS] S3        <- same
[FAIL] S4  ... (360x240 -> 800x450)
[FAIL] S5  ... (second 360x240 -> 300x200 ...)
[FAIL] S6  ... (100x100)
[PASS] S7        <- S7/S8 pin long-standing behavior; they pass before the fix too
[PASS] S8
RESULT: 5 passed, 4 failed   (R1 passes too)
```

On Windows, where the main window does resize, S2 and S3 fail as well
(measured before R1/R2 were added: 2 passed, 6 failed). On macOS before the
fix, R2 fails because the main-window reference resized the focused window.

`[WARNING] [Platform] setWindowSize not yet implemented on Linux` lines are
expected on Linux: they mark every call that reached the main window.
