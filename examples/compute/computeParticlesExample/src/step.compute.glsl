@cs cs
layout(local_size_x=64) in;
struct Particle { vec4 state; };
layout(binding=0) buffer particle_state { Particle particles[]; };
layout(binding=0) uniform step_params { vec4 step_time; };
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(step_time.y)) return;
    vec4 p = particles[i].state;
    p.xy = mod(p.xy + p.zw * step_time.x + 1.0, 2.0) - 1.0;
    particles[i].state = p;
}
@end
@program step_particles cs
