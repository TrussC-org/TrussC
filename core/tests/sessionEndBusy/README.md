# macOS session end while a callback is paused (#239)

A logout, restart or shutdown that reaches a macOS app while one of its native
callbacks is on the stack (here: an `update()` spinning the run loop) is
handled like Win32's WM_ENDSESSION. The App is not asked, and the session is
not cancelled. From the moment the session-end path starts until the process
ends, the framework is frozen: only the exit notifications, audio stop and the
exit log run. No tick, event, App attach/detach, deferred close or window
creation is admitted, and a Window released by an exit hook is not torn down.

The notification reaches the windows open when it starts: main first, then the
others newest first, each in its own window context, and only the BeforeApp
and App bands of `events().exit` (protected AfterApp hooks are skipped). Then
the backend ends the process with `_exit(app exit code)`. No GPU, window or
App teardown happens, and no atexit handler or static destructor runs.

Run without arguments, the test launches itself as a real TrussC app once per
scenario. Each child's App owns two secondary windows, A then B. Each child
dispatches a logout quit Apple event in-process. The parent checks the child's
output and exit status:

- **busy, code 7 and code 0:** exit status equals the app code. Every
  `App::exit` runs exactly once, in the order main, B, A, each seeing its own
  window's width and `events()`, with the App still attached. No AfterApp exit
  hook and no `App::cleanup()` runs. At the final exit log line the GPU is
  still valid and audio is stopped. No atexit or static-destructor marker
  appears, and control never returns to the paused callback.
- **exit hooks that act:** main's `App::exit` requests and reaches a detach of
  A, creates windows, dispatches a second logout, sends a key to B and spins
  the run loop; A's `App::exit` drops A's last owner. All of that is inert:
  each `App::exit` still runs once, creation is refused (logged once, no
  native window), and no tick or event reaches an App.
- **busy in A's callback:** main's `App::exit` drops A, whose context is the
  interrupted one; the session end keeps it valid (checked under ASan too).
- **not busy, code 7:** the normal path, recording the normal order (main,
  then B, A as main's App releases its members). Full cleanup runs every
  App's exit and cleanup, the AfterApp hooks and GPU teardown; `std::exit`
  runs the process exit handlers, and the status is 7.

It needs a window server session (any macOS desktop or CI runner). Other
platforms print SKIP.
