@module tc_fbostencil

@vs fullscreen
void main() {
    // Oversized triangle; no vertex buffers or interpolated gray values.
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
@end

@fs write_bits
precision highp float;
precision highp int;
layout(binding=0) uniform highp texture2D grayTex;
layout(binding=0) uniform sampler graySmp;
layout(binding=0) uniform bit_params { vec4 bitValue; };
void main() {
    // Same-size attachments share their native pixel coordinates on every
    // backend. texelFetch ignores filtering, mipmaps, alpha and gamma.
    float gray = texelFetch(sampler2D(grayTex, graySmp), ivec2(gl_FragCoord.xy), 0).r;
    int value = int(floor(clamp(gray, 0.0, 1.0) * 255.0 + 0.5));
    if ((value & int(bitValue.x)) == 0) discard;
}
@end

@fs read_bits
precision highp float;
layout(binding=0) uniform bit_params { vec4 bitValue; };
out vec4 frag_color;
void main() {
    frag_color = vec4(bitValue.x / 255.0, 0.0, 0.0, 1.0);
}
@end

@program write fullscreen write_bits
@program read fullscreen read_bits

@fs clear_fs
void main() {
}
@end

@program clear fullscreen clear_fs

// Same vertex layout and uniform blocks as sokol_gl's textured quads.
@vs preview_vs
layout(binding=0) uniform vs_params { mat4 mvp; mat4 tm; };
in vec4 position;
in vec2 texcoord0;
in vec4 color0;
out vec4 uv;
out vec4 color;
void main() {
    gl_Position = mvp * position;
    uv = tm * vec4(texcoord0, 0.0, 1.0);
    color = color0;
}
@end

@fs preview_fs
precision highp float;
layout(binding=0) uniform texture2D previewTex;
layout(binding=0) uniform sampler previewSmp;
in vec4 uv;
in vec4 color;
out vec4 frag_color;
void main() {
    float gray = texture(sampler2D(previewTex, previewSmp), uv.xy).r;
    frag_color = vec4(vec3(gray), 1.0) * color;
}
@end

@program preview preview_vs preview_fs
