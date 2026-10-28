# tcxLut

3D `.cube` LUTs for GPU color grading. Add `tcxLut` to `addons.make` and
include `<tcLut.h>`.

```cpp
tcx::lut::Lut3D lut;
bool loaded = lut.load("luts/grade.cube"); // relative to bin/data
```

Only the **0..1 input domain** and **full-range LUTs** are supported.
Resolve's usual exports with `LUT_3D_INPUT_RANGE 0.0 1.0` load, as do
3D-only files with `LUT_1D_INPUT_RANGE 0.0 1.0`,
`DOMAIN_MIN 0 0 0` and `DOMAIN_MAX 1 1 1`. The parser compares the parsed
double values exactly with 0 and 1, without an epsilon.

Other declared ranges fail to load with an error naming the keyword.
Previously, non-default `DOMAIN_MIN` / `DOMAIN_MAX` loaded without applying
their domain, producing wrong colors; these files now fail clearly.
Resolve's bare `LUT_IN_VIDEO_RANGE` / `LUT_OUT_VIDEO_RANGE` flags declare
video (legal) range and are rejected. Full range is expressed by omitting
those flags; arguments to the flags are malformed. Range lines with missing,
extra, or non-finite values are rejected. Trailing `#` comments are allowed.
1D LUTs (`LUT_1D_SIZE`) remain unsupported.
