@module pipeline
@vs mesh_vs
in vec3 position;
in vec2 texcoord0;
in vec4 color0;
out vec4 tint;
void main() {
    gl_Position = vec4(position, 1.0);
    tint = color0 + vec4(texcoord0, 0.0, 0.0) * 0.001;
}
@end
@fs mesh_fs
in vec4 tint;
out vec4 frag_color;
void main() { frag_color = tint; }
@end
@program mesh mesh_vs mesh_fs

