# Runtime session ending (#239, C3)

Run from the repository root, through the shared build guard:

```sh
~/TrussC-dev/trussc-ai-tools/tcbuild.sh python3 core/tests/sessionEndBusy/test.py
~/TrussC-dev/trussc-ai-tools/tcbuild.sh python3 core/tests/sessionEndBusy/test.py --tsan
~/TrussC-dev/trussc-ai-tools/tcbuild.sh python3 core/tests/sessionEndBusy/native-test.py
```

The production-linked CPU test injects committed session callbacks while update,
input, draw, onRender, construction, ordinary ending and generation retirement
are active, and while a real Null AudioEngine callback remains in flight. It
checks reverse attachment order, exactly-once exits, refusal of new work, no
veto/cleanup/destruction on the interrupted stack, and inert late native closes.
A replacement reserved in the ledger but still waiting on the outgoing exit is
an unpublished candidate and receives no exit.

The supervisor runs children that deliberately wait 2/5/10 seconds in exit,
with the default 3-second deadline and a positive configured deadline. Multiple
short exits share one budget. Zero, infinity and invalid values are covered;
infinite waits are supervised externally. The exact deadline and cancellation
arbitration use virtual clock values, independent of wall-clock scheduling.
Ordinary quit retains full cleanup and never arms the watchdog. Running the
binary without arguments checks the deterministic clock/race cases only.

The native seam compiles the actual macOS applicationShouldTerminate and power-off
forwarders, Win32 hidden control HWND creation/query/commit, the timeout setter,
and GPU release/acquisition boundaries. Mutants must compile and fail the intended
assertion. These Linux seams do not establish actual OS logout timing.

The existing macOS Apple-event probe is retained as an explicit device mode:

```sh
sessionEndBusy --mac-device
TC_LOGOUT_EXIT_DELAY=5 sessionEndBusy --mac-device real-logout
TC_LOGOUT_EXIT_DELAY=2 TC_LOGOUT_TIMEOUT=4 sessionEndBusy --mac-device real-logout
```

The first command supervises synthetic logout Apple events, including nested
run loops, secondary callbacks, repeated notifications and native GPU admission.
The real-logout mode waits for an auditor to initiate OS logout from a separate
session; it does not initiate logout itself. Set TC_LOGOUT_EXIT_DELAY to 2, 5 or
10 seconds; each secondary exit deliberately waits that long. TC_LOGOUT_TIMEOUT
optionally supplies float seconds. The default is 3 seconds for the entire run.
The auditor must supervise the process and OS separately. Do not launch real
logout tests on an unattended worker. macOS/device execution is UNVERIFIED on
Linux; device runs are routed to combined pass 1.

Busy session ending freezes new admission, retains the interrupted objects and
calls only each completed App's exit, once in reverse attachment order. There
are no window exit-event bands, audio waits, tree cleanup, module unload or clean
completion claims. Idle session commit uses complete common cleanup under the
same watchdog. Native process termination bypasses atexit/static destructors.
