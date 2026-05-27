@module tc_fbomip
// =============================================================================
// fboMipDownsample.glsl — generate one mip level from the one above.
// =============================================================================
// Used by Fbo::end() when the FBO was allocated with mipmaps = true.
//
// Two things make this filter stronger than a plain "single bilinear tap"
// (i.e. raw 2x2 box):
//
//   1. **4x4 footprint**: four bilinear taps offset by ±0.5 source texels
//      average the surrounding 4x4 source neighbourhood. A wider low-pass
//      removes more of the high-frequency content that would otherwise
//      cascade into lower mips and resurface as moiré at small sizes.
//
//   2. **Gamma-correct average**: input pixels are treated as sRGB-encoded,
//      converted to linear light, averaged, and re-encoded. Without this
//      correction the average of a black/white pattern collapses to mid-
//      grey at 0.5 in sRGB space (perceptually too dark), and edges
//      between bright and dark colours read as too dark in lower mips —
//      both noticeably wrong on textures that include high-contrast areas.
//
// The source mip level is selected via a per-mip texture view, not through
// the LOD parameter. The `srcMipLevel` uniform is retained for padding but
// no longer read by the shader.
// =============================================================================

@vs vs
in vec2 position;
in vec2 texcoord0;
out vec2 uv;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    uv = texcoord0;
}
@end

@fs fs
layout(binding=0) uniform texture2D srcTex;
layout(binding=0) uniform sampler   srcSmp;

in vec2 uv;
out vec4 frag_color;

// Approximate sRGB <-> linear with gamma 2.2 (cheap, matches the convention
// the rest of the pipeline uses for non-sRGB-aware RGBA8 textures).
vec3 srgb_to_linear(vec3 c) { return pow(c, vec3(2.2)); }
vec3 linear_to_srgb(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }

void main() {
    // The bound view covers exactly one mip level (the source), so sample
    // at LOD 0 — the view's base level is already the correct source mip.
    vec2 srcSize = max(vec2(textureSize(sampler2D(srcTex, srcSmp), 0)), vec2(1.0));
    vec2 texel = 1.0 / srcSize;

    // Four bilinear taps at ±0.5 source texels = 4x4 source footprint.
    vec4 s0 = textureLod(sampler2D(srcTex, srcSmp), uv + vec2(-0.5, -0.5) * texel, 0.0);
    vec4 s1 = textureLod(sampler2D(srcTex, srcSmp), uv + vec2( 0.5, -0.5) * texel, 0.0);
    vec4 s2 = textureLod(sampler2D(srcTex, srcSmp), uv + vec2(-0.5,  0.5) * texel, 0.0);
    vec4 s3 = textureLod(sampler2D(srcTex, srcSmp), uv + vec2( 0.5,  0.5) * texel, 0.0);

    // Average in linear light, then encode back to sRGB. Alpha stays linear
    // (it's already a linear coverage value, not a perceptual quantity).
    vec3 linAvg = (srgb_to_linear(s0.rgb) + srgb_to_linear(s1.rgb)
                 + srgb_to_linear(s2.rgb) + srgb_to_linear(s3.rgb)) * 0.25;
    float alpha = (s0.a + s1.a + s2.a + s3.a) * 0.25;

    frag_color = vec4(linear_to_srgb(linAvg), alpha);
}
@end

@program downsample vs fs

// Simple 1:1 blit — copies a single-mip view into another texture at the
// same resolution. Used to transfer mip data from the scratch image back
// into the main FBO texture (see generateMipmaps_() in tcFbo.h).
@fs blit_fs
layout(binding=0) uniform texture2D srcTex;
layout(binding=0) uniform sampler   srcSmp;

in vec2 uv;
out vec4 frag_color;

void main() {
    frag_color = textureLod(sampler2D(srcTex, srcSmp), uv, 0.0);
}
@end

@program blit vs blit_fs
