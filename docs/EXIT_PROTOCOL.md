# Windowed app exit protocol

OS close asks the addressed Window's closeRequested. If accepted, quitOnClose
or the last open Window asks process exitRequested before any Window closes.
Either veto keeps every Window open; acceptance uses common process ending. Runtime dispatches
exitRequested first to its own listeners and then to attached Apps in reverse
attachment order, ORing vetoes and retaining the first nonempty explanation.
Code Window::close() skips closeRequested. For the last open Window or
setQuitOnClose(true), Runtime asks exitRequested once before anything closes.
Any veto keeps all Windows open; acceptance uses normal process ending.
A non-last plain code close closes only that Window without request events;
normal Window exit notification and App ending still run.
Each close() is decided when it is called; a later exitRequested veto does not undo an earlier close() from the same frame.
Process shutdown bypasses closeRequested.

On macOS, accepted process ending cancels open async dialog sheets before
App ending and AppKit window closure. A veto leaves confirm, load and save
sheets and their callbacks pending. An open `alertDialogAsync` sheet is the
exception: AppKit dismisses a one-button alert as soon as quit starts, before
exitRequested, so its callback runs even if the quit is then vetoed. Cancellation invokes each live owner's callback once, before its
exit/cleanup: alert completes without a result, confirm receives `false`,
and load/save receive an empty unsuccessful result. AppKit's later completion
does not invoke the callback again. Callbacks of an already ended App remain
suppressed by the usual ownership token.

OS session query and commit belong to Runtime. Windows receives
WM_QUERYENDSESSION/WM_ENDSESSION on its own hidden top-level control HWND;
closing the first user Window cannot remove that authority. Idle queries reuse
the process veto dispatcher and register its explanation with Windows. A nested
busy query accepts without reentering user callbacks. WM_ENDSESSION(FALSE)
resolves the query without ending the Runtime. A committed session end, including
ENDSESSION_CLOSEAPP, bypasses veto. macOS applicationShouldTerminate forwards
logout/restart/shutdown Apple-event reasons through the same query/commit;
NSWorkspaceWillPowerOff commits on the main operation queue. Ordinary Cmd+Q remains deferred and
vetoable at the existing safe boundary.

Busy means App code on the main stack: a host callback, constructor, native
callback or tick. Workers do not make an idle main busy. A busy query accepts
without entering App callbacks. On busy commit, no App::exit, exit events,
cleanup or watchdog runs. Windows returns from WM_ENDSESSION and the OS ends
the process. macOS calls _exit(0) before returning NSTerminateNow; it never
uses an unanswered NSTerminateLater that could block logout. A blocked main
that cannot receive the callback is subject to the OS termination policy.
Busy termination and OS-killed ends skip atexit handlers and C++ static destructors.
A logout that arrives while a framework call is pumping messages synchronously
(for example, Windows VideoPlayer::load waiting for metadata) also counts as busy.
An open async macOS sheet alone does not make the main thread busy: idle
session ending cancels it through normal ending. A synchronous modal call
still on an App's stack counts as busy and follows the existing immediate
session termination policy, without dialog callbacks or App cleanup.

An idle commit runs normal reverse attachment ending synchronously: exit events,
exactly one App::exit, audio detach and cleanup, then complete service/device
cleanup. One process-wide watchdog (default 3 seconds,
runtime().setLogoutTimeout(float seconds)) bounds the whole idle ending.
Negative or NaN values log Error and retain the old setting; zero ends
immediately, positive infinity disables the deadline. Ordinary quit arms none.
Windows may impose an earlier deadline. After idle cleanup, Runtime closes the
Logger file sink and flushes stdout/stderr, preserving the final exit lines
before normal process exit. On both Windows and macOS, idle commit calls
std::exit with the application exit code: atexit handlers and C++ static
destructors run as on ordinary exit. The watchdog stays armed through these
handlers and destructors; if they block past the deadline, it ends the process
immediately without completing them. On Windows, destructors and atexit
callbacks of loaded DLLs run after the OS has stopped the watchdog thread, so
only the OS deadline bounds them. Busy termination does not flush sinks.
Neither path returns to frames. Linux has no session receiver and cannot
promise logout delivery.

A quit requested during setup (exitApp/requestExitApp) is handled before the
first service prelude, and exitRequested still gets its veto. For a Window's root
App (including headless), the rule is:
**accepted exit requested in root setup → normal ending, then no console, update or draw**.
If vetoed, the loop continues normally.
A child App leaves quit pending until the current tree traversal unwinds; the
outer turn handles it before the next services. The current traversal may
continue; no App callbacks run after cleanup.

The first SIGTERM/SIGINT only records a flag in the signal handler. The next
frame requests exit on the main thread, even in event-driven rendering. A
second signal while that request is still pending, including during cleanup,
terminates immediately without another event or cleanup. A veto clears the
pending signal and its delivery state: the next signal starts a fresh,
cancellable request, and only another signal while that request is pending
forces immediate exit (status `128 + signal`, normally 130 or 143). SIGKILL
cannot deliver a callback or cleanup.

The Logger emits these stable **message payloads**, with its usual timestamp,
level and `[System]` prefix:

```text
exit: begin reason=exit-app pid=1234
exit: clean reason=exit-app code=0 pid=1234
```

`begin` is emitted at the start of `_cleanup_cb`, before MCP, app `exit()` and
audio shutdown. `clean` follows framework `cleanup()`. Each appears once.
The `reason=TOKEN` field is omitted when the origin is unknown. Field order,
spacing, decimal `code` (the application's exit code), and decimal `pid` are
part of the protocol. A PID identifies
a run in an appended log; correlate with its startup/time since PIDs can be
reused. When a Logger file is open (via `TRUSSC_LOG_FILE` or `setLogFile`), both
exit records are written and flushed at NOTICE regardless of its level,
including Warning or Silent. Console and system outputs retain their normal
level filtering. `onLog` listeners receive both records normally. Ordinary
log records still obey the file level; the bypass is internal to the runtime.

Known reason tokens include `last-window-close` (last-window or quit-on-close), `request-exit-app`, `exit-app`,
`tc-quit`, `os-session-end` (idle desktop session commit), `app-quit` (macOS: a quit Apple
event without one of those reasons, i.e. Cmd+Q, the Dock's Quit or an
AppleScript `quit`), `sigterm`, `sigint`, and `device-lost` where the backend
detects loss. A cancelled request clears its origin. Cleanup freezes it so
callbacks cannot change the reason halfway through the pair.

| Observed markers for a run | Meaning |
| --- | --- |
| `begin` and `clean` | Normal exit completed |
| `begin` only | Interrupted during cleanup |
| Neither | No observed cleanup; unknown, not necessarily a crash |

At MCP HTTP shutdown, all queued requests and deferred replies receive a
JSON-RPC response containing `the app is exiting normally (reason=TOKEN)`.
The parenthesized reason is omitted if unknown. Queued requests use error
code `-32000` and retain their request ID; deferred tools keep their existing
JSON-RPC tool-result envelope. Requests racing shutdown are answered without
waiting for another frame. No push channel or session is introduced.

TrussC installs no built-in crash handler and writes no crash report. Use a
platform debugger or external crash reporting when needed.

## Platform verification

Run the Linux-compiled session policy and native seam tests described in
core/tests/sessionEndBusy/README.md. Windows osExit --window accept/cancel/
cancel-empty/forced addresses the hidden Runtime HWND; it schedules the query
outside update so veto is meaningful. Native user HWND tests assert that neither
first nor subsequent user windows own session termination.

Actual Windows logout/restart/shutdown and macOS Air logout with 2/5/10-second
exit delays require a supervised device check. Check busy main drag/modal
logout gets no App exit and does not block shutdown; separately check synchronous
performClose with two secondaries during update. Check
saved state and OS deadlines separately; busy termination has no clean log claim.
Run real logout checks only on a supervised host.

On macOS verify Cmd+Q and real logout/restart/shutdown with and without a veto,
including the Apple event reason and a modal dialog. On Linux/macOS also run
`osExit --window escalate`: the first signal is vetoed, the next signal delivers
a fresh `exitRequested` event, and another signal inside that listener exits
with status 130, without a cleanup pair. `--window escalate-term` checks the
same sequence with status 143. `--window log-warning` and `--window log-silent`
check normal cleanup with all levels set to Warning and, respectively, the
file level additionally set to Silent: both NOTICE records must reach the
open file, but neither reaches the console. Linux screen runs require Xvfb in CI.
