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
