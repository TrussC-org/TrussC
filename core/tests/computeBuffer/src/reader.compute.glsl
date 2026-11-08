@vs vs
struct InputItem { vec4 color; };
layout(binding=0) readonly buffer pattern_input { InputItem items[]; };
layout(binding=0) uniform draw_params { vec4 placement; };
layout(location=0) out vec4 vertex_color;
void main() {
    vec2 corners[6] = vec2[](vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,0), vec2(1,1), vec2(0,1));
    uint v = uint(gl_VertexIndex);
    uint i = v / 6u;
    vec2 p = vec2(float(i), 0) + corners[v % 6u];
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

@vs instanced_vs
struct InputItem { vec4 color; };
layout(binding=0) readonly buffer pattern_input { InputItem items[]; };
layout(binding=0) uniform draw_params { vec4 placement; };
layout(location=0) out vec4 vertex_color;
void main() {
    vec2 corners[6] = vec2[](vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,0), vec2(1,1), vec2(0,1));
    uint v = (uint(gl_InstanceIndex) % 2u) * 3u + uint(gl_VertexIndex);
    uint i = uint(gl_InstanceIndex) / 2u;
    vec2 p = vec2(float(i), 0) + corners[v % 6u];
    gl_Position = vec4(p * vec2(0.25, 2.0) + vec2(placement.x - 1.0, -1.0), 0, 1);
    vertex_color = items[i].color;
}
@end
@program buffer_instanced_reader instanced_vs fs

@vs ordinary_vs
layout(location=0) in vec3 position;
layout(location=1) in vec2 texcoord0;
layout(location=2) in vec4 color0;
layout(location=0) out vec4 vertex_color;
void main() {
    gl_Position = vec4(position, 1.0);
    vertex_color = color0 + vec4(texcoord0, 0, 0) * 0.001;
}
@end
@program buffer_ordinary ordinary_vs fs
