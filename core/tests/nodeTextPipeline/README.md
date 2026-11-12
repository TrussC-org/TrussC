# nodeTextPipeline

After building the core tests, run `allCoreTests nodeTextPipeline --gpu-check`
with a display. Pixel checks cover a temporary coverage pipeline left after
text, Fill in the next node, Add/Alpha changes, a custom Shader between text
nodes, nested Fbos, depth on/off, RGBA8/RGBA16F and 1x/4x MSAA. It reuses the
synthetic font from `renderTargetState`, with no system font dependency.

Run a frozen workload with one of these modes:

```
allCoreTests nodeTextPipeline --benchmark 240 text
allCoreTests nodeTextPipeline --benchmark 240 shader
allCoreTests nodeTextPipeline --benchmark 240 pbr
```

Each mode submits 3,000 draws in a fixed grid: text Nodes, custom Shader
triangles, or PBR meshes. After 30 warmup frames it reports median/p95 CPU
submission time, frame time (draw entry through `afterFrame`, including graphics
synchronization), and recorded draw-command count (sgl plus deferred Shader/PBR
commands). Use the same Release configuration, machine, explicit `DISPLAY`,
and private Xvfb/Mesa instance for interleaved baseline and changed runs.
Timings are informational and never decide the test result.

Pipeline loads alone do not split sgl commands. The pixel test also checks
that text with unchanged matrices batches across repeated style resets. Each
Node's matrix operations independently create command boundaries, so the tree
benchmark reports that cost separately from pipeline lookup performance.
