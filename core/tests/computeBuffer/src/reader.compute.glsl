@vs vs
struct InputItem { vec4 color; };
layout(binding=0) readonly buffer pattern_input { InputItem items[]; };
layout(binding=0) uniform draw_params { vec4 placement; };
layout(location=0) out vec4 vertex_color;
void main() {
    vec2 corners[6] = vec2[](vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,0), vec2(1,1), vec2(0,1));
    int i = gl_VertexIndex / 6;
    vec2 p = vec2(float(i), 0) + corners[gl_VertexIndex % 6];
    gl_Position = vec4(p * vec2(0.25, 2.0) + vec2(placement.x - 1.0, -1.0), 0, 1);
    vertex_color = items[i].color;
}
@end
@fs fs
layout(location=0) in vec4 vertex_color;
layout(location=0) out vec4 frag_color;
void main() { frag_color = vertex_color; }
@end
@program buffer_reader vs fs
