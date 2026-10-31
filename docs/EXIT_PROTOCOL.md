# Windowed app exit protocol

OS close asks the addressed Window's closeRequested. After closure, quitOnClose
or the last Window requests normal process quit. Runtime dispatches
exitRequested first to its own listeners and then to attached Apps in reverse
attachment order, ORing vetoes and retaining the first nonempty explanation.
Code close and process shutdown bypass closeRequested.

OS session query and commit belong to Runtime. Windows receives
WM_QUERYENDSESSION/WM_ENDSESSION on its own hidden top-level control HWND;
closing the first user Window cannot remove that authority. Idle queries reuse
the process veto dispatcher and register its explanation with Windows. A nested
busy query accepts without reentering user callbacks. WM_ENDSESSION(FALSE)
resolves the query without ending the Runtime. A committed session end, including
ENDSESSION_CLOSEAPP, bypasses veto. macOS applicationShouldTerminate forwards
logout/restart/shutdown Apple-event reasons through the same query/commit;
NSWorkspaceWillPowerOff commits directly. Ordinary Cmd+Q remains deferred and
vetoable at the existing safe boundary.

Before OS-session exits, Runtime freezes new admission and arms one process-wide
watchdog (default 3 seconds, runtime().setLogoutTimeout(float seconds)). Negative
or NaN values log Error and retain the previous setting; zero ends immediately,
and positive infinity leaves the watchdog unarmed. The budget covers all App
exits together. Windows may impose an earlier deadline. No fixed macOS logout
allowance is asserted. Ordinary quit never arms this watchdog.

Busy commit calls each completed App::exit once from the common reverse
attachment snapshot. It retains interrupted objects and skips Window exit-event
bands, audio/thread waits, tree cleanup, module unload and native/GPU disposal.
There is no begin/clean log pair or claim that abandoned work completed. Idle
commit performs common complete cleanup under the same deadline. Both end the
process without atexit/static destructors. An OS that supplies no session
callback cannot guarantee this delivery. Save briefly in exit; do not use the
GPU or wait for other threads.

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

Known reason tokens are `window-close`, `request-exit-app`, `exit-app`,
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
| Crash marker from the crash-reporting protocol (#252) | Crash |

At MCP HTTP shutdown, all queued requests and deferred replies receive a
JSON-RPC response containing `the app is exiting normally (reason=TOKEN)`.
The parenthesized reason is omitted if unknown. Queued requests use error
code `-32000` and retain their request ID; deferred tools keep their existing
JSON-RPC tool-result envelope. Requests racing shutdown are answered without
waiting for another frame. No push channel or session is introduced.

Headless log-file/MCP startup and anchorbolt classification are separate work.

## Platform verification

Run the Linux-compiled session policy and native seam tests described in
core/tests/sessionEndBusy/README.md. Windows osExit --window accept/cancel/
cancel-empty/forced addresses the hidden Runtime HWND; it schedules the query
outside update so veto is meaningful. Native user HWND tests assert that neither
first nor subsequent user windows own session termination.

Actual Windows logout/restart/shutdown and macOS Air logout with 2/5/10-second
exit delays require separate auditor supervision. Include modal/audio-in-flight
cases and synchronous performClose with two secondaries during update. Check
saved state and OS deadlines separately; busy termination has no clean log claim.
No worker initiates real logout on an unattended host.

On macOS verify Cmd+Q and real logout/restart/shutdown with and without a veto,
including the Apple event reason and a modal dialog. On Linux/macOS also run
`osExit --window escalate`: the first signal is vetoed, the next signal delivers
a fresh `exitRequested` event, and another signal inside that listener exits
with status 130, without a cleanup pair. `--window escalate-term` checks the
same sequence with status 143. `--window log-warning` and `--window log-silent`
check normal cleanup with all levels set to Warning and, respectively, the
file level additionally set to Silent: both NOTICE records must reach the
open file, but neither reaches the console. Linux screen runs require Xvfb in CI.
