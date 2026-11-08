@cs cs
layout(local_size_x=32) in;
struct Counter { uint count; };
layout(binding=0) buffer counter { Counter counters[]; };
void main() {
    if (gl_GlobalInvocationID.x >= 64u) atomicAdd(counters[0].count, 1u);
}
@end
@program count_values cs
