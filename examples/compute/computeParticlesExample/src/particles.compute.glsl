@vs vs
struct InputParticle { vec4 state; };
layout(binding=0) readonly buffer particle_input { InputParticle particles[]; };
void main() {
    vec2 corners[3] = vec2[](vec2(-0.004, -0.005), vec2(0.004, -0.005), vec2(0.0, 0.005));
    gl_Position = vec4(particles[gl_InstanceIndex].state.xy + corners[gl_VertexIndex], 0.0, 1.0);
}
@end
@fs fs
layout(location=0) out vec4 frag_color;
void main() { frag_color = vec4(0.3, 0.8, 1.0, 1.0); }
@end
@program draw_particles vs fs
