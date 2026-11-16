@module stream
@vs color_vs
layout(location=0) in vec3 position;
layout(location=1) in vec2 texcoord0;
layout(location=2) in vec4 color0;
out vec4 tint;
void main() {
    gl_Position = vec4(position, 1.0);
    // Keep all ShaderVertex attributes live for D3D11 input semantics.
    // Test vertices have zero UVs, so their colors remain exact.
    tint = color0 + vec4(texcoord0, 0.0, 0.0) * 0.001;
}
@end
@fs color_fs
in vec4 tint;
out vec4 frag_color;
void main() { frag_color = tint; }
@end
@program color color_vs color_fs

@fs slots_fs
in vec4 tint;
out vec4 frag_color;
layout(binding=0) uniform texture2D tex0;
layout(binding=0) uniform sampler smp0;
layout(binding=1) uniform texture2D tex1;
layout(binding=1) uniform sampler smp1;
layout(binding=2) uniform texture2D tex2;
layout(binding=2) uniform sampler smp2;
layout(binding=3) uniform texture2D tex3;
layout(binding=3) uniform sampler smp3;
layout(binding=4) uniform texture2D tex4;
layout(binding=4) uniform sampler smp4;
layout(binding=5) uniform texture2D tex5;
layout(binding=5) uniform sampler smp5;
layout(binding=6) uniform texture2D tex6;
layout(binding=6) uniform sampler smp6;
layout(binding=7) uniform texture2D tex7;
layout(binding=7) uniform sampler smp7;
layout(binding=8) uniform texture2D tex8;
layout(binding=8) uniform sampler smp8;
layout(binding=9) uniform texture2D tex9;
layout(binding=9) uniform sampler smp9;
layout(binding=10) uniform texture2D tex10;
layout(binding=10) uniform sampler smp10;
layout(binding=11) uniform texture2D tex11;
layout(binding=11) uniform sampler smp11;
layout(binding=12) uniform texture2D tex12;
layout(binding=12) uniform sampler smp12;
layout(binding=13) uniform texture2D tex13;
layout(binding=13) uniform sampler smp13;
void main() {
    int column = int(gl_FragCoord.x) / 4;
    if (column == 0) frag_color = texture(sampler2D(tex0, smp0), vec2(0.5));
    else if (column == 1) frag_color = texture(sampler2D(tex1, smp1), vec2(0.5));
    else if (column == 2) frag_color = texture(sampler2D(tex2, smp2), vec2(0.5));
    else if (column == 3) frag_color = texture(sampler2D(tex3, smp3), vec2(0.5));
    else if (column == 4) frag_color = texture(sampler2D(tex4, smp4), vec2(0.5));
    else if (column == 5) frag_color = texture(sampler2D(tex5, smp5), vec2(0.5));
    else if (column == 6) frag_color = texture(sampler2D(tex6, smp6), vec2(0.5));
    else if (column == 7) frag_color = texture(sampler2D(tex7, smp7), vec2(0.5));
    else if (column == 8) frag_color = texture(sampler2D(tex8, smp8), vec2(0.5));
    else if (column == 9) frag_color = texture(sampler2D(tex9, smp9), vec2(0.5));
    else if (column == 10) frag_color = texture(sampler2D(tex10, smp10), vec2(0.5));
    else if (column == 11) frag_color = texture(sampler2D(tex11, smp11), vec2(0.5));
    else if (column == 12) frag_color = texture(sampler2D(tex12, smp12), vec2(0.5));
    else if (column == 13) frag_color = texture(sampler2D(tex13, smp13), vec2(0.5));
    else frag_color = tint;
}
@end
@program slots color_vs slots_fs

@fs samplerless_fs
in vec4 tint;
out vec4 frag_color;
layout(binding=0) uniform texture2D source_texture;
// shdc's reflection currently needs a pair even for texelFetch. The test
// removes the unused sampler from the non-GL descriptor before loading it.
layout(binding=0) uniform sampler unused_sampler;
void main() {
    frag_color = texelFetch(sampler2D(source_texture, unused_sampler), ivec2(int(gl_FragCoord.x) / 4, 0), 0) + tint * 0.001;
}
@end
@program samplerless color_vs samplerless_fs
