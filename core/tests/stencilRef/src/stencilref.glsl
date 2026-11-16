@module stencilref
@vs vs
in vec2 position;
in vec3 color;
out vec3 vertex_color;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    vertex_color = color;
}
@end
@fs fs
in vec3 vertex_color;
out vec4 frag_color;
void main() { frag_color = vec4(vertex_color, 1.0); }
@end
@program test vs fs
