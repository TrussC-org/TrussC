# Target-bound ImGui display tests

Run under a display (Linux: Xvfb/Mesa). The addon test runner builds it and
skips display execution without arguments; run the modes below explicitly.
This is a separate manifest; the
38-capture core renderRegression manifest stays unchanged.

```
trusscli update -p addons/tcxImGui/tests-renderTarget --ide cmake
cmake -S addons/tcxImGui/tests-renderTarget -B addons/tcxImGui/tests-renderTarget/build-linux -DTC_HOT_RELOAD=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build addons/tcxImGui/tests-renderTarget/build-linux
addons/tcxImGui/tests-renderTarget/bin/tests-renderTarget --record imgui-images
```

`--compare <directory>` compares matching screen captures byte for byte and
runs the offscreen self-checks. Record screen baselines using this same fixture
on the base with `TC_IMGUI_BASE_FIXTURE` defined and `--screen-only`; the base
has no target-bound scope API. Offscreen captures are new capability evidence,
not invented base images. `samples.json` includes backend, attachment samples,
error counts and all capture names. No tolerances are raised for screen images.

Fixtures cover RGBA8/RGBA16F, samples 1/4, the five-row byte-alpha table,
premultiplied chaining, clipping, user reset callbacks, both pipeline recipes,
context isolation, same-context refusal, pending window preservation, nested
context restoration, next-commit recovery, and ordinary main/secondary window
onRender UI at DPI 2 with pixelPerfect off/on. All screen readbacks and their
pixel/image checks run in the capturing window's `afterFrame`, after `present()`
has committed and before another draw. The pending-across-commit marker gets a
separate present before the ordinary screen scene clears it; its check still
samples at the DPI saved when the pending frame was built. The ordinary screen
scenes run once each and retain their capture names and byte-exact baselines.

The main window polls secondary progress by wall clock, including before the
secondary's first setup. Three seconds without a secondary frame prints a SKIP
naming both `imguiScreenDpi2.pixelPerfectOff.secondary` and
`imguiScreenDpi2.pixelPerfectOn.secondary` (occluded or hidden), then allows the
other checks to finish. The whole run fails if it exceeds 30 seconds.
Single-window platforms
(Raspberry Pi, web, mobile) print a SKIP for the secondary window. Pass `--native-dpi2` on a
DPI-2 display (Linux Xvfb: set the Xft.dpi resource to 192 and enable high DPI).
Add `--msaa4` to request a four-sample swapchain.
`requestedScreenSamples` records that request; `screenSamples` and each screen
capture's `targetSamples` record the actual counts. A backend may fall back to
one sample. Report that fallback separately from the Fbo samples 1/4 matrix.
Without `--native-dpi2`, the fixture overrides window metrics to DPI 2. Xvfb is not
a physical Retina display check. If RGBA32F is filterable,
unfilterable texture dispatch is explicitly skipped; its pipeline descriptor is
still checked. A backend requiring unfilterable float textures must run it too.

Review round 1 (adversarial) adds a Linux surfaceless EGL regression. Enable it
with `-DTC_IMGUI_NATIVE_PROBE=ON` when configuring this project, then build and
run `bin/imguiNativeProbe` (Mesa EGL development libraries required). Linker
wrappers record native keyboard/cursor calls: offscreen frames leave both
untouched, while ordinary window frames still show/hide the keyboard and set
the cursor. The probe calls `tcx_imgui_input` through real deferred tool
envelopes and advances the existing hook clock, delivering main `afterFrame`
events without redrawing the Fbo. Applied edits settle at their check deadline;
unrendered edits expire at their write deadline; 200 superseding tool requests
and 300 distinct queued records are reclaimed. Window and offscreen contexts
accept more than the former 256 pending edits; several contexts accept more
than the former 1,024 total. Owning-window idle frames and shutdown settlement
are checked separately. This records platform calls; it does not replace
mobile-device testing.

For both window and offscreen value edits, `queueValue()` creates the pending
record and `tcx_imgui_input` defers to that record's token. The HTTP worker waits
for its deferred response until settlement, cancellation or timeout resolves it.
Core MCP ingress still bounds HTTP bodies, sockets and workers.

The probe also accepts 1,000 text requests to an idle offscreen InputText and
one text above 128 KiB. Deadline and idle-frame cleanup reclaim tool events
while preserving native events. Fixed-size edits can follow queued text.
The addon has no pending-edit, event or byte admission caps. Allocation failure
inside Dear ImGui behaves as it does for any ImGui frame.

Cancellation also covers partially consumed text: one `NewFrame` consumes the
tool's Ctrl and left-button presses, then either a 3-second clock advance and
`afterFrame` or owning-window idle settlement cancels the request. Two more
renders must release both inputs and drain the queue. Both modifier mappings
are tested, as is preservation of Ctrl and mouse state held by native input.
Cleanup retains only matching releases for tool presses already consumed;
it never sends focus loss to clear unrelated native input.

Run `bin/imguiNativeProbe --static-exit` separately to test a global owned manager
with a real recorded checkbox interaction, followed by `std::exit(0)` without
the framework exit event. Registry-owned lifetime guards prevent access after
static registry destruction; ordinary owned-manager RAII cleanup is tested by
the main probe. Run both modes with ASan+UBSan as well as the normal build.

For #947 part 4, run `bin/tests-renderTarget --secondary-exit` on a desktop
with a display. It creates a real secondary window with window/offscreen ImGui,
NodeInspector input and an Fbo recorder. Closing it checks that App exit runs
once while those services remain available, and that clearing exit subscriptions
from App exit preserves mandatory AfterApp teardown and a readable recording.
On Linux, use Xvfb with the software Mesa environment used by the display suite.

The surfaceless probe also covers #340 buffer growth: real frames above 65,536
vertices must reach callbacks in every draw list, including the foreground.
It checks per-context Notices, repeated growth, explicit/default initial sizes
through both manager setup paths, two staging allocation failures, a partial
GPU allocation failure, old-buffer fallback and next-frame recovery.
Run `bin/imguiNativeProbe --buffer-grow` to isolate these checks. The fixture
also verifies GPU readback of the last list, index-only growth, exact staging
doubling and safe rejection of unrepresentable Sokol byte offsets.

The same native-probe option builds `bin/imguiBufferSizeProbe`, which uses an
isolated dummy backend. It checks the signed-int byte-size boundary, caps
otherwise overflowing doubling, rejects unrepresentable initial sizes before
allocation, and checks one Error plus old-buffer partial rendering for
unrepresentable vertex and index demands. Large staging requests receive a
1-byte stand-in and are never copied into; the test allocates no multi-GB
CPU or GPU blocks. Run this probe separately from the real-GL modes.
