Public custom blending regression (#1436). `allCoreTests customBlend` runs value,
validation and Style checks in a headless App. `--gpu-check` verifies RGBA equations
for all three presets and Maximum on Fbo, MSAA Fbo and window targets, including
Font, bitmap and retained GlyphBatch coverage, textures, premultiplied Fbo sources
and restoration. The same GlyphBatch is drawn with all four descriptors in one
frame; each draw must use the descriptor selected then.
`--msaa4` also uses a multisampled window.

Thirty measured frames alternate 4,000 custom selections or Add/Alpha selections.
After warm-up, canonical lookup and pipeline counts must remain flat. Submission
timings are printed for comparison only. Custom frames also exercise 2,000 text
reset/reselect sequences and repeated GlyphBatch draws outside the timed shapes
loop. Neither immediate nor raw pipeline counts may grow after warm-up.
