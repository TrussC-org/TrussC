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
onRender UI at DPI 2 with pixelPerfect off/on. Pass `--native-dpi2` on a
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
and 256 distinct queued records are reclaimed. Pending tool edits, including text input, per UI context
are bounded: 256 per context and 1,024 total, well above interactive bursts.
The bound remains local to the addon. Window and offscreen contexts both test
N+1 and repeated overflow: a JSON-RPC error (-32000), counted rejection, no
queue or deferred-response growth, and capacity recovery after settlement.
The total limit is tested across multiple contexts and released on destruction. Owning-window idle frames and
shutdown settlement are checked separately. This records platform calls; it
does not replace mobile-device testing.

For both window and offscreen value edits, `queueValue()` creates the pending
record and `tcx_imgui_input` defers to that record's token. `handleToolsCall()`
returns an empty internal result, but `processHttpQueue()` retains the request
promise in `deferredResponses()`. The HTTP worker remains in `future.get()`;
`finishPendingValues()` calls `onDone`, which drains that token and resolves
the promise. Core cancellation/timeout can also resolve the promise. There is
no MCP admission-slot counter on this base. The permanent local queue bound
therefore stands independently of HTTP worker lifetime or future admission
machinery; no hold-the-slot follow-up is required.

The probe also sends 1,000 text requests to an idle offscreen InputText. Text
shares the edit count and reserves input event and byte capacity before insertion
(8,192 events / 128 KiB per context; 32,768 events / 512 KiB total). Rejections
are counted. Deadline and idle-frame cleanup reclaim tool events while preserving
native events; context destruction releases aggregate capacity. Oversized input
and mixed value/text budgets are covered.

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
