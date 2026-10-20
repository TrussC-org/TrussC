# Per-window shadows (#821 PR 5, part 2)

`allCoreTests perWindowShadows` checks lazy construction and byte arithmetic
without a display. `--gpu-check` runs the real Linux GL coordinator under Xvfb.
The display lane discovers this mode through `display-test`. Set
`TC_SHADOW_OUTPUT` to a directory to save actual/reference PNG pairs separately
from the existing renderRegression manifest.

Two windows render different slot-0 occluders and light matrices. Each result
must match its own isolated reference across the entire image (RGBA tolerance
2). Cases cover both execution orders, distinct resolutions, Fbo PBR, growth
while an older recording remains queued, foreign commits, a parked open shadow
pass, same-owner slot rewriting, and native close with pending work. Independent
checks require a visible cast shadow and distinct A/B reference images. A third
window never uses shadows and must allocate no shadow generation.

The fixture reports image payload bytes from Sokol image descriptors: four R32F
layers plus one four-byte depth image. Both windows keep the 1024 default
(20,971,520 bytes each); views, sampler and driver overhead are additional.
The `shaderScopeDummy` coordinator also covers partial allocation failure,
checked arithmetic, foreign-owner isolation and generation retirement under
ASan/UBSan, without allocating large native images. Its sustained C++ allocation
failure probe keeps failures enabled through generation growth, shared-pointer
control-block failure, partial native creation, final pin release and close.
Each rejected request emits one Error, preserves the other owner, and recovers
when allocations work again. Dummy probes also cover retained viewport/scissor
during shadow-consumer replay, recycled SG IDs across device restart, and
discard/retirement for null native objects at every generation creation stage.

`shaderScopeDummy --native-allocation-check` runs the separate Linux GL fixture.
It injects native texture/storage/view/sampler failures and checks local discard,
retirement and recovery, with another owner's R32F contents preserved. A stale
`glEnable(0)` error before creation or growth must allow allocation and emit a
separate Warning naming `0x0500` as left pending by earlier GL code. The drain
consumes that code; reuse and invalid-size rejection leave the user's error
pending. Production Sokol behavior is unchanged. For an isolated
debug-backend injection run, disable only `_SG_GL_CHECK_ERROR()` in that test
backend and set `TC_SHADOW_DEBUG_BACKEND=1` (skip Sokol's unchanged zero-name
assertions); descriptor validation remains enabled. The default display mode
uses the ordinary release core archive and tests all six fault stages.

On macOS, the standalone dummy project's `shadowMetalSyntax` object target
compiles the host's actual native checks as Objective-C++ against Metal/Sokol,
without creating a device or adding a Sokol seam. Native Metal execution still
requires a macOS/iOS machine.

```sh
LIBGL_ALWAYS_SOFTWARE=1 \
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
xvfb-run -a core/tests/allCoreTests/bin/allCoreTests perWindowShadows --gpu-check
```
