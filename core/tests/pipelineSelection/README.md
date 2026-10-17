# Target pipeline selection (#821 PR 4a)

Run `allCoreTests pipelineSelection --gpu-check [--msaa4] [--record <dir>]`.
The daily display lane runs both sample counts. CPU-only invocations skip.

The suite covers RGBA8/RGBA16F targets at samples 1/4, shared raw pipelines and
exclusive immediate contexts, descriptor differences, parked-generation
retirement, scope-local Shader move/reload/destruction, shadow DEPTH metadata,
point MVP snapshots and 12x12 attachment masks at simulated DPI 1/2, one-device-pixel
POINTS coverage (MSAA may spread it over adjacent resolved pixels), style blend and premult sources, clipped/transformed LUT/NV12, transformed Hap, and
fifty real sgl rebuilds with alternating buffer capacities with two surviving Fbos
and, where `internal::supportsSecondaryWindows()` is true, a secondary window.
Single-window platforms print a SKIP and omit 51 assertions (one creation check
and fifty secondary-window generation checks); both Fbo resize checks still run.
GL adversarial resize regressions record one triangle on a lazy bottom target,
request a budget rebuild or change secondary-window dimensions, then present:
each must submit exactly one draw, produce a red triangle in native readback,
and rebuild at the following boundary. This multi-present synthetic window probe
is GL-only because Metal retains one drawable per app tick.
Without `--record`, Linux GLCORE expects 685 assertions per mode. Linux GLES3
expects 622: 51 fewer for the secondary-window SKIP and 12 fewer for the existing
GL-only lazy-resize SKIP. Float-format targets (RGBA16F/RGBA32F) are read with
readPixelsFloat(): GLES3 cannot read a float attachment as unsigned bytes.
The dummy suite also forces buffer growth after each rebuild and verifies that
releaseSglBuffers clears all three CPU capacities and the GPU vertex buffer.
NV12 uses generated planes through the actual VideoPlayer draw routing; no
codec, video clock, camera or NVDEC device is needed.

`--record` writes a separate pipeline manifest of PNGs. Its `blend-after-end.after`
image has lazy and active rows, each with RGB (192,64,64) in both rectangles.
The matching public-API fixture built on the base records
`blend-after-end.before`: first rectangle (160,32,32), second (192,64,64).
These captures do not change renderRegression's existing 38-name manifest.

On the Air, additionally run `allCoreTests renderRegression --self-check`.
Its pointMeshTransform.fbo assertion requires **12x12 = 144 attachment pixels
per point even at DPI 2**, where the base records 24x24 = 576. The screen
retains 24x24 device pixels with pixelPerfect off. Every backend records its own
base and compares head, then runs `tools/adjudicate_render_pr4a.py <base>
<base.compare> --output <report.json>` (requires Pillow). This checks the
three-name allowlist, samples, masks, changed regions, Fbo alpha and unchanged
tolerances. The ordinary comparator must still fail on the three intentional
changes; retain its raw logs and diff PNGs. It is never globally suppressed.

ImGui and capture routing are deferred to PR 4b and PR 5 respectively.
