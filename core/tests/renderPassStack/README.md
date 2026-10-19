# Target pass stack (#821, PR 3)

`allCoreTests renderPassStack --gpu-check [--msaa4]` runs deterministic pixels
and state checks on the native backend; the default invocation skips the display.
Both modes are registered in the daily display lane.

The cases cover A/B/C nesting at unequal extents, equal/different formats and
samples 1/4; color/depth LOAD, fresh activations, explicit clear, lazy screen
submission, exclusive pooled contexts, transforms/camera/scissor/pipeline restore,
version-copy preparation, mip finalization, and public readback restrictions.
The MSAA-interruption test checks that red drawn before the child survives beside
green drawn after resume, after resolving the outer Fbo. LOAD passes also issue
triangle counts 1 and 2 and require the persistent red image to survive; these
incomplete primitives expose Mesa V3D trimming before colour bookkeeping (#858).
A colour CLEAR with zero draws must update the resolve. Run these on every backend.

Shader scopes remain open across repeated children and nested scopes. Their full
RGBA output must equal a flattened reference, and half-alpha probes must
contribute exactly once. Screen variants also span consuming flushes and children.
`shaderScopeDummy` separately preserves the reviewer's real-API submission probe:
both modes require `draws=2 expected=2`. It also forces a lease allocation failure
and inspects final-tail versus resumable callback store actions and commit counts.
The ELF dummy override runs on Linux; native pixel checks run on every backend.

Misuse checks cover recursion, wrong-order/window/token end, active mutation and
move rejection, unallocated/inactive pairs, failed preparation and bottom pop.
Fresh Fbos inherit Add blending and the logical depth flag; overlapping
half-alpha rectangles must retain both color contributions. Internal scopes cover
early return, exceptions, finalization failure, throwing transfer completion and
native transfer begin/end ordering. An unfinished public activation exercises present's
LIFO recovery. Nested IBL outputs are compared with top-level face/mip/LUT bakes.

Existing renderRegression captures retain their manifest and tolerances. Record
on `34f17b79` and compare on this change on the same machine. Air checks use
`MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1`: renderRegression --self-check and
the PR 3 tests must exit 0. A validation violation aborts with SIGABRT/rc 134; an
oversized viewport is not a validation error, so pixel tests cover viewport
correctness. GPU frame capture is not required. D3D11, Metal and Pi GLES
verification remains a device gate.

Review round 1 (adversarial) also extends `shaderScopeDummy` with the real
pipeline-descriptor probe, one-shot `bad_alloc` during Shader replay (explicit
pop and RAII destructor), lease reuse after failure, and unfinished window-owned
shadow/Shader teardown with either the originating or another window current.
Run the teardown regression with both the probe and coordinator instrumented:

```sh
cmake -S core/tests/shaderScopeDummy -B core/tests/shaderScopeDummy/build-sanitized -DTC_DUMMY_SANITIZERS=ON
cmake --build core/tests/shaderScopeDummy/build-sanitized
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 core/tests/shaderScopeDummy/build-sanitized/shaderScopeDummy --teardown
```

Leak detection is disabled for the process-lifetime backend/host pools. Address
and undefined-behavior checks remain enabled. Use the machine's build wrapper
for both build commands where required.

For the #858 activation Notice regression, run the GPU modes with
`--expect-v3d-notice` under the test preload's `TC_TEST_GL_RENDERER='V3D 7.1.10.2'`
override (`TC_TEST_V3D_NO_POISON=1`), and with `--expect-no-v3d-notice` on native
non-V3D. These options check the exact message and Notice level, absence before
any resolve, delivery by the first resolving pass, and no repeats across three
more resolves and the remaining stack suite. Both samples 1/4 modes apply.
The override exists only in `.p858/v3d-empty-preload.c`, never in production.
