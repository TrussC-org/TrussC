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
