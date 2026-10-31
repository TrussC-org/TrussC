# OS session ending

Run `test.py` and `native-test.py` through tcbuild.sh on Linux.
These tests never log out the host. Historical HEAD-negative probes and their failing assertions are retained in
`.verification/c3-trim/regressions/` and the matching logs.

Busy means App code on the main stack, including constructors and native ticks.
Busy commit calls no App exit, cleanup or watchdog. Windows returns to the OS;
macOS takes `_exit(0)` without static destructors or an App callback. The Linux
seam compiles the actual mac busy probe and commit decision to verify that route.
Workers alone do not bypass an idle main's veto.

Idle coverage retains veto/cancel/query dedupe, reverse exact-once exits,
commit dedupe, complete cleanup, one shared deadline (default 3 seconds), numeric
configuration and zero/infinity. Native seams retain Windows hidden-HWND
session authority, macOS reasons and main-queue workspace notification delivery.
`test.py --tsan` instruments Runtime and the deadline implementation.

`--mac-device` runs only idle and busy-no-exit Apple-event children on a real
Mac. Run under an external timeout; no macOS device result is claimed on Linux.
Real Win/mac drag/modal logout, idle 2/5/10-second exits, the hidden HWND and
headless delivery still require supervised device passes. The workspace observer
stays until a device pass establishes whether every termination Apple event
arrives, making the observer redundant.
