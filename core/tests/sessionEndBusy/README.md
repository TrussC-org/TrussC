# macOS session end while a callback is paused (#239)

A logout, restart or shutdown that reaches a macOS app while one of its native
callbacks is on the stack (here: `update()` spinning the run loop) is handled
like Win32's WM_ENDSESSION. The App is not asked, and the session is not
cancelled. Only the App exit notification, audio stop and the exit log run.
Then the backend ends the process with `_exit(app exit code)`. No GPU, window
or App teardown happens, and no atexit handler or static destructor runs.

Run without arguments, the test launches itself as a real TrussC app once per
scenario. Each child dispatches a logout quit Apple event in-process. The
parent checks the child's output and exit status:

- **busy, code 7 and code 0:** exit status equals the app code. `App::exit`
  runs exactly once. At the final exit log line the GPU is still valid and
  audio is stopped. No secondary-window teardown happens, no atexit or
  static-destructor marker appears, and control never returns to the paused
  callback.
- **not busy, code 7:** the normal path. Full cleanup tears down the GPU and
  the secondary window's App, `std::exit` runs the process exit handlers, and
  the status is 7.

It needs a window server session (any macOS desktop or CI runner). Other
platforms print SKIP.
