<!-- /* [TrussC #1447 DIAG] */ -->
# Temporary Windows multi-window CPU diagnostic

This branch measures the D3D11 run loop without changing pacing. It is enabled
by default, writes one loop line plus one line per live swapchain to **stderr**
about once per second, and must never be merged. Rates use the actual `dt`, not
an assumed one-second interval. QPC measures elapsed wall time, not CPU time:
compare the output with process/main-thread CPU usage and the scratch app's
own `App::draw()` counts.

## Build and run on the Windows device

Use the existing scratch multi-window app against **this checkout's core**.
Keep both windows drawing, visible on the same 120 Hz display, with swap interval
1. Rebuild the app's existing Windows CMake tree, for example:

```powershell
cmake --build <scratch-app-build-directory> --config Release
& <scratch-app-executable> 2> A3-B3-diag.log
```

Use a scratch executable with a working stderr handle (a console build or
redirected stderr). A GUI launch with no stderr handle cannot capture these lines.

Set the launch window's settings (A) and the `createWindow` settings (B) before
creating either window:

```cpp
/* [TrussC #1447 DIAG] */
WindowSettings a;
a.setSwapInterval(1).setMaxFrameLatency(3);  // change to 1 for A1 B3
WindowSettings b;
b.setSwapInterval(1).setMaxFrameLatency(3);
```

Pass `a` to the existing launch call and `b` to the existing secondary creation
call. Do not change latency after creation. Record A3 B3, A1 B3, and an A1 B1
control into separate logs; A3 B16 is optional. Let each settle, then collect
several full intervals with CPU usage and the app's own per-window draw counts.
Also collect covered/minimized and restored intervals. A new window's first
rate interval can be partial. Closing windows discards their unfinished interval.

## Printed fields

All comma-separated values follow the names in parentheses; `/s` means rate per
second, and `max_ms` is the largest individual call in that reporting interval.

| Field | Meaning |
| --- | --- |
| `loop dt`, `loops/s` | Actual interval and **every** outer run-loop pass, including passes with no tick. Modal OS loops are outside this count. |
| `waits/s`, `wake/s(frame,msg,timer,timeout,other)` | Every MsgWait return, partitioned into a frame waitable, Windows messages (`WAIT_OBJECT_0 + num_wait`), the armed pace timer, timeout, or other/error. |
| `zero/s`, `zero_subms/s` | MsgWait calls with timeout zero; the subset caused by truncating a positive submillisecond deadline when no pace timer was successfully armed. |
| Per-window `wake/s(own[index=...],peer,msg,timer,timeout,other)` | This window's frame-waitable returns and other windows' frame-waitable returns, followed by the shared loop reasons. Index is the last own waitable's index in the changing handle array; meaningful only with own wakes. These shared rates are repeated for context, not additional returns. Fairness samples are separate. |
| `zero_reason/s(saved,held,deadline)` | Windows that requested an immediate pass because of a saved waitable signal, a held credit, or a due timer-only deadline. Several windows may request the same pass, so these are not a partition of `zero/s`. |
| `ticks/s`, `credit/s(held,fresh,fallback)` | Entries past the native tick guard, partitioned by credit source. Fresh credits include signals captured by MsgWait or zero-timeout fairness/modal sampling. |
| `callbacks/s`, `draw_proxy/s`, `no_draw_proxy/s` | Completed tick callbacks; ticks whose completed callback did not request skip_present; remaining ticks. TrussC Runtime sets skip_present when `runWindowTick` did not draw, so this separates drawing from update-only ticks in the scratch app. This is a **proxy**, not a new hook inside App::draw: explicit application skip_present after drawing also counts as no draw; a raw sapp callback without App can count as draw. Retain the scratch app's exact App::draw counters as the independent check. |
| `skipped/s`, `why/s(skip_present,occluded,minimized,other)` | Ticks with **no ordinary Present call**, including early returns. Reasons are exclusive, prioritized minimized, occluded, skip_present, other (canceled, hidden, device loss, etc.). A Present attempt returning occluded is still a call; see successful/s. |
| `rejected/s(not_due,no_credit,other)` | Due checks rejected before entering a tick: future deadline, missing credit, or unavailable swapchain/construction/reentry. These are **not ticks** and must not be added to no-draw ticks. |
| `presents/s`, `successful/s`, `probes/s` | Ordinary Present calls, successful non-occluded calls, and separate `DXGI_PRESENT_TEST` probes. |
| `Hz`, `tick/Hz`, `present/Hz` | Existing per-window pacing refresh estimate and rates divided by that Hz. These ratios make excess native work visible even if App::draw stays at 120/s. |
| `Present(ms/s,max_ms)` | QPC elapsed time inside ordinary Present **and** occlusion probe Present, per window. |
| `MsgWait(ms/s,max_ms)` | QPC elapsed time inside MsgWait, loop-wide. Includes normal sleeping and any internal driver/OS spin. |
| `sample(ms/s,max_ms,calls/s)` | QPC elapsed time and calls for fairness `WaitForSingleObject(handle, 0)` (also modal samples); per window and loop total. |
| `frame_latency_wait=none` | There is no separate blocking frame-latency wait: MsgWait contains that wait, and zero-timeout samples are measured separately. |
| `DXGI/s(last,Present,PresentRefresh,SyncRefresh)` | Deltas of GetLastPresentCount and the three GetFrameStatistics counters, divided by dt. These describe presentation history, not queue depth. `-1` means unavailable, first baseline, disjoint, or a decreasing/reset counter. `stats_hr` is the frame-statistics HRESULT; errors are tolerated and invalidate that baseline. |

## Read the numbers

- **Extra loop work:** App draws stay near 120/s while loops/waits, zero passes,
  rejected checks, or no-draw ticks climb. Wake reasons identify the repeated
  source; saved/held/deadline reasons identify immediate passes. Fairness call
  rates show repeated zero-timeout sampling even without extra ticks.
- **Over-rendering:** native ticks or Present calls greatly exceed display Hz
  (`tick/Hz` or `present/Hz` much greater than 1). Use draw_proxy plus the app's
  exact draw counts to locate whether excess work reaches App::draw. Compare
  successful calls and DXGI deltas with attempted Presents.
- **Time in driver/OS calls:** loop/tick rates remain ordinary but Present or
  sampling accounts for much of the interval while main-thread CPU stays high.
  Large MsgWait time alone is normal sleeping; large MsgWait time **with high
  CPU and ordinary wake counts** suggests an internal busy wait. QPC alone cannot
  prove a busy wait; a device CPU profile is the next discriminator.
- **Other work:** high CPU with ordinary counts and little time in the measured
  calls points outside these calls (callbacks, message dispatch, other driver
  work). No scheduling fix is included in this branch.

Instrumentation adds counters and QPC reads on hot paths and performs formatting
and DXGI history queries once per interval. Reporting/queries themselves are not
included in Present or wait timings. Reports are emitted at the end of an outer
turn; a blocked callback/modal loop can delay them. No Windows execution or
real-display diagnosis is possible on the Linux worker.
