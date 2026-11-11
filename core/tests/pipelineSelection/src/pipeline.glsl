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

@vs full_vs
in vec2 position;
in vec2 texcoord0;
out vec2 uv;
void main() { gl_Position = vec4(position, 0.0, 1.0); uv = texcoord0; }
@end
@fs half_red_fs
in vec2 uv;
layout(binding=0) uniform texture2D source;
layout(binding=0) uniform sampler source_sampler;
out vec4 frag_color;
void main() { frag_color = texture(sampler2D(source, source_sampler), uv); }
@end
@program full full_vs half_red_fs

@vs generated_vs
out vec2 uv;
void main() {
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    uv = positions[gl_VertexIndex] * 0.5 + 0.5;
}
@end
@program generated generated_vs half_red_fs

