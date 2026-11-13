@module stroke_overlap
@vs vs
layout(binding=0) uniform params { vec4 targetSize; };
in vec2 position;
in vec4 color0;
out vec4 color;
void main() {
    gl_Position = vec4(position / targetSize.xy * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
    color = color0;
}
@end
@fs fs
in vec4 color;
out vec4 frag_color;
void main() { frag_color = color; }
@end
@program triangles vs fs
