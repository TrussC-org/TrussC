@module tc_sgl
// =============================================================================
// sglPremult.glsl — premultiplied-alpha variant of sokol_gl's built-in shader.
// =============================================================================
// Replacement shader for an sgl pipeline: reads pos/uv/color from the standard
// 40-byte sgl vertex, using an explicit three-attribute sg_pipeline_desc.layout.
// The unused point-size slot is skipped. Uniforms (vs_params = mvp+tm) and
// tex/smp at binding 0 match the shader embedded in sokol_gl_tc.h.
//
// The fragment stage's output is PREMULTIPLIED
// (rgb *= a). This is what lets the Screen / Multiply blend modes respect source
// alpha — with a straight-alpha fragment output their fixed-function blend
// equations ignore α (Screen) or can't fade by it (Multiply). With premultiplied
// output, SRC_COLOR carries rgb·a, so:
//   Screen   (src=ONE, dst=ONE_MINUS_SRC_COLOR)   = Photoshop screen, exact for
//                                                    any backdrop alpha.
//   Multiply (src=DST_COLOR, dst=ONE_MINUS_SRC_ALPHA) = α-correct over an opaque
//                                                    backdrop.
// Only Screen/Multiply pipelines use this; everything else keeps sgl's straight
// shader, so Alpha/Add/text/images/FBO compositing are byte-for-byte unchanged.
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
    // NOTE: no gl_PointSize write — this variant is only bound for Screen/Multiply
    // filled primitives, and writing a non-constant PointSize fails shdc/SPIRV-Cross
    // validation.
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
    vec4 c = texture(sampler2D(tex, smp), uv.xy) * color;
    frag_color = vec4(c.rgb * c.a, c.a);   // premultiplied output
}
@end

@program premult vs fs
