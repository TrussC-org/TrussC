# Windowed app exit protocol

Window close, Windows session end (including `ENDSESSION_CLOSEAPP`), macOS
application termination and Linux/macOS SIGTERM/SIGINT issue the ordinary
`events().exitRequested` event. Setting `cancel` vetoes that request. Windows
uses `reason` as the shutdown-block explanation, or “The app is still busy.”
when it is empty. After cancellation, the user starts shutdown again. The
block is removed when the request is resolved or Windows cancels session end.

The first SIGTERM/SIGINT only records a flag in the signal handler. The next
frame requests exit on the main thread, even in event-driven rendering. A
second signal while that request is still pending, including during cleanup,
terminates immediately without another event or cleanup. A veto clears the
pending signal and its delivery state: the next signal starts a fresh,
cancellable request, and only another signal while that request is pending
forces immediate exit (status `128 + signal`, normally 130 or 143). Forced OS shutdown
and SIGKILL cannot be vetoed. Windows runs cleanup inside `WM_ENDSESSION(TRUE)`
even after a veto, then calls `TerminateProcess(GetCurrentProcess(), 0)` inside
that message, including for `ENDSESSION_CLOSEAPP`. This prevents returning to
interrupted user code or running DLL detach/guest static destructors after
teardown. Cleanup and its log writes finish first. Windows grants only a few
seconds: keep exit handlers short.

The Logger emits these stable **message payloads**, with its usual timestamp,
level and `[System]` prefix:

```text
exit: begin reason=os-session-end pid=1234
exit: clean reason=os-session-end code=0 pid=1234
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
`tc-quit`, `os-session-end` (Windows), `os-logoff`, `os-restart`, `os-shutdown`
(macOS when supplied by the quit Apple event), `app-quit` (macOS: a quit Apple
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

Run `osExit --window accept` and `osExit --window cancel` (or
`allCoreTests osExit --window ...`). On Windows also run `--window cancel-empty` for the generic explanation and
`--window forced`
to send an accepted `ENDSESSION_CLOSEAPP` notification after a veto. Accepted
session-end tests verify cleanup before process termination, which skips
`atexit`; check status 0 and one begin/clean pair in `TRUSSC_LOG_FILE` as well.
These use synthetic messages; verify real logoff/restart/shutdown separately, including Restart
Manager, a custom/empty block reason, cancellation by another application,
and forced shutdown while a modal dialog or window drag is active. Check the
app's saved state and the single log pair after signing back in.

On macOS verify Cmd+Q and real logout/restart/shutdown with and without a veto,
including the Apple event reason and a modal dialog. On Linux/macOS also run
`osExit --window escalate`: the first signal is vetoed, the next signal delivers
a fresh `exitRequested` event, and another signal inside that listener exits
with status 130, without a cleanup pair. `--window escalate-term` checks the
same sequence with status 143. `--window log-warning` and `--window log-silent`
check normal cleanup with all levels set to Warning and, respectively, the
file level additionally set to Silent: both NOTICE records must reach the
open file, but neither reaches the console. Linux screen runs require Xvfb in CI.
