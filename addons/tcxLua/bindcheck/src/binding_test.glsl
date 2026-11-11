@module binding_test
@vs vs
flat out float red;
void main() {
    vec2 position = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
    red = float(gl_InstanceIndex + 1) * 0.5;
}
@end
@fs fs
flat in float red;
out vec4 frag_color;
void main() { frag_color = vec4(red, 0.0, 0.0, 1.0); }
@end
@program triangles vs fs
