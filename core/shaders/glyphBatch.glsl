@module tc_glyphbatch
@include textCoverage.glslinc

@vs vs
layout(binding=0) uniform glyph_params {
    mat4 mvp;
    vec4 atlas; // inverse page size, premultiplied blend flag, unused
};
in vec4 position;
in vec2 texcoord0;
in vec4 color0;
out vec2 uv;
out vec4 color;
flat out float premultiply;
void main() {
    gl_Position = mvp * position;
    uv = texcoord0 * atlas.xy;
    color = color0;
    premultiply = atlas.z;
}
@end
@fs fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
in vec2 uv;
in vec4 color;
flat in float premultiply;
out vec4 frag_color;
@include_block text_coverage
void main() {
    float alpha = color.a * textCoverage(uv);
    frag_color = vec4(color.rgb * mix(1.0, alpha, premultiply), alpha);
}
@end
@program coverage vs fs
