# tcxInputReplay tests

`tests` is the ordinary addon console harness: `src/main.cpp`, `addons.make` and `local.cmake`, discovered by the addon test runner. It uses the core's headless sokol shim. It covers JSONL validation and round trips, exact file/line/record/time limits, scaling in App coordinates (including framebuffer pixels), and scene synchronization with a supplied clock. The same smoke sequence also runs here through the core's virtual native Window producer, with DPI 1/2 and pixelPerfect off/on.

`../tests-display` is an opt-in display harness (`tests-display --display-check`; its default invocation skips) using the shared `tcDisplayTest.h` verdict helper. It records scripted injected mouse/drag/scroll/key/character/touch/drop input, replays it through `Window::injectInput`, and compares all recorded fields and the `replayed` tag. A typed consumer at ImGui's priority verifies recording before consumption. It also checks repeated operations, real input mixing, marks, exclusion of its own replay, validation before injection, and cleanup finalization.

On Linux, run the display binary on Xvfb or an existing display. Use a private display number when starting Xvfb, and stop the Xvfb process by its PID. There is no addon API for starting a display server.

The normal build runner generates project metadata. In an offline worker where project generation is forbidden, stage the `emptyExample` CMake recipe with the relative core fallback `../../../core`, then configure/build each project through the prescribed build guard with `-DFETCHCONTENT_FULLY_DISCONNECTED=ON`. No addon dependencies need fetching.
