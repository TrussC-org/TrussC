# Metal uniform growth (#859)

This macOS owned-binary display test starts `sg_setup` with a 256-byte reserve. Its
private backend wrapper temporarily enlarges storage for six reference frames,
then restores every ring slot to 256 bytes after a synchronous queue flush.
It renders the same six frames, three passes and six distinct PBR triangles
per pass, comparing every readback byte with its corresponding reference.

The real `PbrPipeline::executePbrDraw` is used. At two draws in each of the
first tiny frame's three passes, the test places the 192-byte vertex block in
the last 256-byte slice. The following fragment upload must grow the buffer;
the wrapper counts six actual buffer changes between those calls. Subsequent
frames exercise rotate-slot catch-up and reuse. The final probes check two
compute uniform blocks at different native slots (GPU result must be 709),
allocation failure in growth/upload/catch-up, and unrepresentable sizes.

The base's blanket rebind points the pending vertex slot to the fragment
block at offset zero, replacing its model/view/normal matrices with unrelated
floats. Thus the base cannot reproduce the reference triangles at these six
deterministic splits. The test files can be overlaid on base without changing
its production code. If pixel/compute checks fail, the unsafe base nil-allocation
probe is skipped so the test still prints its final FAIL verdict.

Build with `trusscli update -p core/tests/metalUniformGrowth`, configure its
generated CMake project with `-DCMAKE_OSX_ARCHITECTURES="$(uname -m)"`, then run
the app executable with `MTL_DEBUG_LAYER=1` and `--gpu-check`. The final verdict
is emitted during cleanup, before macOS exits:

```text
metalUniformGrowth: PASS (split-PBR=6, frames=12, passes=36, compute, rollback)
```

Other platforms skip the GPU harness. `test_backend.py` instead extracts the exact
production allocation/growth/upload function bodies and compiles them against
GNU Objective-C buffer/device/encoder mocks (needs clang++ and libobjc.so.4 on Linux).
Run it through the build wrapper
(`tcbuild.sh python3 core/tests/metalUniformGrowth/test_backend.py`). An optional
header path selects base source. The base fails both pending raster and compute
binding checks; head must also pass failure/size/retirement checks. This is CPU
state validation, not a substitute for Apple SDK compilation or Metal pixels.
