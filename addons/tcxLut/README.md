# tcxLut

Load 1D curves, 3D color tables, and 1D shaper + 3D `.cube` LUTs on the CPU
and GPU. Add `tcxLut` to `addons.make` and include `<tcLut.h>`.
The existing `Lut3D` name is retained for all three forms; `load()` selects the
stages from the file, with no kind selection needed.

```cpp
tcx::lut::Lut3D lut;
if (lut.load("luts/grade.cube")) { // relative to bin/data
    tc::Color graded = lut.apply(tc::Color(0.25f, 0.5f, 0.75f));
}
```

For GPU drawing, keep the LUT and source texture alive while using the shader:

```cpp
// setup(): source is an Image, Texture, Fbo, or another texture provider.
tcx::lut::LutShader shader;
shader.load();
shader.setLut(lut);
shader.setTexture(source);
shader.setBlend(0.8f); // 0 = source, 1 = full LUT effect

// draw():
shader.draw(0, 0, 640, 480);
```

## Sampling

- `LUT_1D_SIZE N`: N RGB rows, sampled independently for red, green, and blue
  with linear interpolation. For example, rows `0 0 0`, `0.25 0.5 0.75`,
  `1 1 1` map input `(0.25, 0.5, 0.75)` to `(0.125, 0.5, 0.875)`.
- `LUT_3D_SIZE N`: N³ RGB rows, with red varying fastest, then green, then blue.
  The existing RGBA8 storage is preserved: outputs are clamped to 0..1 and
  truncated to 8-bit values when uploaded. The default filter is trilinear;
  `setFilter(TextureFilter::Nearest)` selects nearest sampling for this stage.
- Both declarations: the first N1D rows form the shaper, followed by N3D³ rows
  for the 3D table. Apply the 1D curve first, then the 3D transform. Either size
  declaration order is accepted. All size declarations must precede data.

`apply(const tc::Color&)` returns the transformed color and preserves alpha.
An empty or cleared LUT returns the input unchanged. Pass finite RGB inputs;
inputs to each stage are clamped to 0..1. The 1D table retains float outputs,
including values outside 0..1; a following 3D stage clamps its input.
The CPU uses the same table precision and interpolation as the GPU.
`LutShader::setBlend()` blends the final transformed RGB with source RGB and
preserves source alpha before the usual drawing blend.

The GPU stores the 1D table in an N×1 RGBA32F texture and interpolates with
explicit texel fetches. It requires no float texture filtering on WebGPU.
Changing `setFilter()` affects only the 3D stage; 1D curves stay linear.

## Loading and resource reference

| API | Behavior |
| --- | --- |
| `bool load(const fs::path&)` | Load any supported `.cube` form; relative paths use `getDataPath()`. |
| `void allocate(int, const float* = nullptr)` | Allocate a 3D table from RGB rows; clears any previous 1D stage. |
| `Color apply(const Color&) const` | Apply the loaded stages on the CPU, preserving alpha. |
| `bool isAllocated() const` | Whether the LUT's GPU resources are ready. |
| `int getSize() const` | 3D side length, or 0 for a 1D-only or empty LUT. |
| `const std::string& getTitle() const` | File TITLE, or the file stem when no title is given. |
| `setFilter()` / `getFilter()` | 3D texture filter (linear by default). |
| `getImage()` / `getView()` / `getSampler()` | Raw 3D resource access; use `LutShader` to apply all loaded stages. A 1D-only LUT binds an internal identity 3D texture. |
| `void clear()` | Release GPU resources and CPU tables, and reset size and title. |

Sizes must be at least 2. Table storage must fit host storage, and texture
sizes must fit the active GPU's 2D/3D limits. The parser grows storage only as
rows arrive, rather than reserving from an untrusted size declaration.
Missing or extra rows, duplicate sizes, malformed rows, and non-finite entries
are rejected. Comments (`#`), blank lines, and `TITLE` are supported.

Only the **0..1 input domain** and **full-range LUTs** are supported.
`DOMAIN_MIN 0 0 0`, `DOMAIN_MAX 1 1 1`, `LUT_1D_INPUT_RANGE 0 1`, and
`LUT_3D_INPUT_RANGE 0 1` are accepted, including when the corresponding stage
is absent. The parser compares parsed double values exactly with 0 and 1,
without an epsilon. Other declared ranges fail with an error naming the
keyword, as decided in #1025. Resolve's bare `LUT_IN_VIDEO_RANGE` /
`LUT_OUT_VIDEO_RANGE` flags declare video (legal) range and are rejected;
full range omits these flags. Missing, extra, or non-finite range values fail.
Trailing `#` comments are allowed.

A failed load keeps the previous LUT, including its title, CPU tables, and
GPU resources. Allocation exceptions from parsing are handled by `load()`;
the parser has no separate allocation-error handler. Load, allocate, clear,
and GPU drawing require the active graphics context. `apply()` reads the
loaded CPU tables and makes no GPU calls.

ファイルが不正な場合や GPU リソースの作成に失敗した場合も、読み込み失敗時は
以前の LUT を保持します。解析中のメモリ確保例外は `load()` が処理し、
パーサー内で重ねて捕捉しません。

파일이 잘못되었거나 GPU 리소스를 만들지 못해 읽기가 실패하면 이전 LUT를 유지합니다.
파싱 중의 메모리 할당 예외는 `load()`가 처리하며 파서에서 중복으로 처리하지 않습니다.

## Tests

`tests/bin/data` contains only small synthetic fixtures: identity and gamma
2.2 curves, and per-channel shapers followed by identity and BRG 3D tables.
The console test checks parsing and import failures. Run the test executable
with `--gpu` in a graphics session (Linux: Xvfb + Mesa software rendering) for
CPU reference values and Fbo readback comparisons, including blend amounts.
No third-party LUT files are required or included.
