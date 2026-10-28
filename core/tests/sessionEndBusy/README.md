# macOS session end while a callback is paused (#239)

A logout, restart or shutdown that reaches a macOS app while one of its native
callbacks is on the stack (here: `update()` spinning the run loop) is handled
like Win32's WM_ENDSESSION. The App is not asked, and the session is not
cancelled. Only the exit notification, audio stop and the exit log run. The
notification reaches every open window's App (main first, as on a normal exit),
each in its own window context, and only the BeforeApp and App bands of
`events().exit`; protected AfterApp hooks are skipped. Then the backend ends the
process with `_exit(app exit code)`. No GPU, window or App teardown happens, and
no atexit handler or static destructor runs.

Run without arguments, the test launches itself as a real TrussC app once per
scenario. Each child dispatches a logout quit Apple event in-process. The
parent checks the child's output and exit status:

- **busy, code 7 and code 0:** exit status equals the app code. The main and
  the secondary `App::exit` each run exactly once, main first, each seeing its
  own window's width and `events()`, with the App still attached. A protected
  AfterApp exit hook does not run. At the final exit log line the GPU is still
  valid and audio is stopped. No `App::cleanup()` runs, no atexit or
  static-destructor marker appears, and control never returns to the paused
  callback.
- **not busy, code 7:** the normal path. Full cleanup runs both Apps' exit and
  cleanup, the AfterApp hook and GPU teardown; `std::exit` runs the process
  exit handlers, and the status is 7.

It needs a window server session (any macOS desktop or CI runner). Other
platforms print SKIP.
