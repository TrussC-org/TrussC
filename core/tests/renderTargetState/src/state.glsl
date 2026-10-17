@module targetstate
@vs full_vs
in vec2 position;
in vec2 texcoord0;
out vec2 uv;
void main() { gl_Position = vec4(position, 0.0, 1.0); uv = texcoord0; }
@end
@fs discard_fs
in vec2 uv;
out vec4 frag_color;
void main() {
    if (uv.x < 0.95) discard;
    frag_color = vec4(0.0, 0.0, 1.0, 1.0);
}
@end
@program discard full_vs discard_fs
