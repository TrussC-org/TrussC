@cs cs
layout(local_size_x=64) in;
struct Particle { vec4 state; };
layout(binding=0) buffer particle_state { Particle particles[]; };
// x = delta time, y = particle count, zw = mouse position (clip space)
layout(binding=0) uniform step_params { vec4 params; };
// x = kick strength (0 = no click this frame), y = random seed
layout(binding=1) uniform kick_params { vec4 kick; };

// cheap pseudo-random number in -1..1 from an integer (GLSL has no random())
float rand(uint n) {
    n = (n << 13u) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return float(n & 0x7fffffffu) / float(0x7fffffff) * 2.0 - 1.0;
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(params.y)) return;
    vec4 p = particles[i].state;
    float dt = params.x;
    // on a click, add a random velocity; each particle gets its own direction
    uint seed = uint(kick.y);
    p.zw += vec2(rand(i * 2u + seed), rand(i * 2u + 1u + seed)) * kick.x;
    // accelerate toward the mouse (+0.01 avoids dividing by zero right on it)
    vec2 toMouse = params.zw - p.xy;
    p.zw += toMouse / (length(toMouse) + 0.01) * 2.0 * dt;
    // a little drag so they don't speed up forever
    p.zw *= 1.0 - 0.5 * dt;
    p.xy += p.zw * dt;
    // bounce off the screen edges (-1..1): mirror the position back, flip the velocity
    if (abs(p.x) > 1.0) { p.x = sign(p.x) * 2.0 - p.x; p.z = -p.z; }
    if (abs(p.y) > 1.0) { p.y = sign(p.y) * 2.0 - p.y; p.w = -p.w; }
    particles[i].state = p;
}
@end
@program step_particles cs
