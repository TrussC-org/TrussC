# Snapshot cost probe (untracked, #821 PR 5 Q2)

See the clone-root `SNAPSHOT-COST.md` for methodology, limitations, measured
results, backend policy analysis, and exact native device commands.

This is a TrussC App at a711686f, with a probe-only Sokol implementation TU
that inserts a native full-screen copy between final end-pass and commit.
On GLES, an alpha-free RGB8 surface is retained as RGB8 so MSAA resolve formats match.
No tracked production code is modified. It is a cost experiment, not a PR 5
capture implementation or capture correctness acceptance test.

`run.py` runs one configuration and records raw block CSV, sample-weighted
summary JSON, environment and source hashes. `local-matrix.py` runs the local
GL/GLES Mesa/Xvfb matrix sequentially; do not use its software-renderer
environment overrides for routed hardware measurements.

Defaults: eight ABBAABBA blocks, 60 warmup + 180 measured frames per block.
Two-window modes synchronize at block boundaries. `--samples` accepts 1/4;
`--windows` accepts 1/2, but the base's GLES3 framework is single-window and
rejects 2. GL/D3D11 native pacing coerces swap interval zero to one, so use
GPU query deltas as well as delivered FPS.
