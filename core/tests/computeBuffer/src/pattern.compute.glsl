@cs cs
layout(local_size_x=32) in;
struct Item { vec4 color; };
layout(binding=0) buffer pattern_output { Item items[]; };
layout(binding=0) uniform buffer_params { vec4 tint; };
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= 4u) return;
    items[i].color = vec4(tint.rg, float(i % 2), 1.0);
}
@end
@program buffer_pattern cs

@cs readback_cs
layout(local_size_x=32) in;
struct Sample { uvec4 integers; vec4 floats; };
layout(binding=0) buffer readback_output { Sample samples[]; };
layout(binding=0) uniform readback_params { vec4 step; };
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= 4u) return;
    samples[i].integers = uvec4(i + uint(step.x), 0xffffffffu - i, 0x12345678u, i * 37u);
    samples[i].floats = vec4(float(i) + step.x, -0.5, 0.125, 1024.0);
}
@end
@program buffer_readback readback_cs
