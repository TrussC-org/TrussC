@module tc_sglcov
// =============================================================================
// sglCoverage.glsl — sokol_gl shader for single-channel coverage textures.
// =============================================================================
// Uses the same explicit pos/uv/color layout and 40-byte vertex stride as
// sglPremult.glsl, skipping the unused point-size slot. Uniforms (vs_params =
// mvp+tm) and tex/smp at binding 0 match sokol_gl's built-in shader.
//
// The texture holds coverage in its R channel (the TrueType glyph atlas, R8,
// one byte per texel). The fragment stage uses that coverage as alpha:
//   frag_color = vec4(color.rgb, color.a * coverage)
// which is what sgl's built-in shader gives for an RGBA texel of
// (1, 1, 1, coverage). Output is straight alpha, for the Alpha blend pipeline.
// =============================================================================

@vs vs
layout(binding=0) uniform vs_params {
    mat4 mvp;
    mat4 tm;
};
in vec4 position;
in vec2 texcoord0;
in vec4 color0;
out vec4 uv;
out vec4 color;
void main() {
    gl_Position = mvp * position;
    // No gl_PointSize write (see sglPremult.glsl): this shader is only bound for
    // textured quads.
    uv = tm * vec4(texcoord0, 0.0, 1.0);
    color = color0;
}
@end

@fs fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
in vec4 uv;
in vec4 color;
out vec4 frag_color;
void main() {
    float coverage = texture(sampler2D(tex, smp), uv.xy).r;
    frag_color = vec4(color.rgb, color.a * coverage);
}
@end

@program coverage vs fs
