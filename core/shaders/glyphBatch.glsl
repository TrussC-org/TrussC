@module tc_glyphbatch
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
void main() {
    // Match Font at bilinear-safe rates, including TCY horizontal compression.
    // Far bands still sample the full chain with trilinear filtering.
    vec2 size = vec2(textureSize(sampler2D(tex, smp), 0));
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    float footprint = max(length(dx * size), length(dy * size));
    float useMips = footprint > 2.0 ? 1.0 : 0.0;
    float alpha = color.a * textureGrad(sampler2D(tex, smp), uv, dx * useMips, dy * useMips).r;
    frag_color = vec4(color.rgb * mix(1.0, alpha, premultiply), alpha);
}
@end
@program coverage vs fs
