# nodeTextPipeline

After building the core tests, run `allCoreTests nodeTextPipeline --gpu-check`
with a display. Pixel checks cover a temporary coverage pipeline left after
text, Fill in the next node, Add/Alpha changes, a custom Shader between text
nodes, nested Fbos, depth on/off, RGBA8/RGBA16F and 1x/4x MSAA. It reuses the
synthetic font from `renderTargetState`, with no system font dependency.

`allCoreTests nodeTextPipeline --benchmark 240` draws a frozen tree of 3,000
text nodes, one glyph per node. After 30 warmup frames it reports median
`drawTree` time, frame submission time (draw entry through `afterFrame`,
including graphics synchronization), and the number of sgl commands recorded
by the tree. Use the same Release build configuration, machine, explicit
`DISPLAY`, and private Xvfb/Mesa instance for baseline and changed builds.
Timings are informational and never decide the test result.

Pipeline loads alone do not split sgl commands. The pixel test also checks
that text with unchanged matrices batches across repeated style resets. Each
Node's matrix operations independently create command boundaries, so the tree
benchmark reports that cost separately from pipeline lookup performance.
