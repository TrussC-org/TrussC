Public custom blending regression (#1436). `allCoreTests customBlend` runs value,
validation and Style checks in a headless App. `--gpu-check` verifies RGBA equations
for all three presets and Maximum on Fbo, MSAA Fbo and window targets, including
Font, bitmap and retained GlyphBatch coverage, textures, premultiplied Fbo sources
and restoration. screenKeepAlpha and premultipliedAdd run with premultiplySource
off and on for shapes, textures, Font, bitmap text and GlyphBatch. Transparent
quad coverage leaves the destination unchanged with conversion on. Fbo draws
verify that conversion off composites once and conversion on is darker from
double premultiplication. The same GlyphBatch uses all six descriptors in one frame.
`--msaa4` also uses a multisampled window.

Thirty measured frames alternate 4,000 flag-only custom changes or Add/Alpha selections.
After warm-up, canonical lookup and pipeline counts must remain flat. Submission
timings are printed for comparison only. Custom frames also exercise 2,000 text
reset/reselect sequences and repeated GlyphBatch draws outside the timed shapes
loop. Neither immediate nor raw pipeline counts may grow after warm-up.
