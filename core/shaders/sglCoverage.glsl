@module tc_sglcov
// =============================================================================
// sglCoverage.glsl — sokol_gl shader for single-channel coverage textures.
// =============================================================================
// Drop-in shader for an sgl pipeline, with the same vertex stage and ABI as
// sglPremult.glsl and the shader embedded in sokol_gl_tc.h (vertex layout
// pos/uv/color/psize, vs_params = mvp+tm, tex/smp at binding 0), so sgl can use
// it via sg_pipeline_desc.shader.
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
in float psize;
out vec4 uv;
out vec4 color;
void main() {
    gl_Position = mvp * position;
    // No gl_PointSize write (see sglPremult.glsl): this shader is only bound for
    // textured quads. psize stays declared so the vertex layout sgl forces still
    // matches.
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
