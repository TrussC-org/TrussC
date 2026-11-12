@cs cs
// 32 threads per group; tcApp dispatches 4 groups = 128 threads
layout(local_size_x=32) in;

// one uint shared by every thread; tcApp reads it back
struct Counter { uint count; };
layout(binding=0) buffer counter { Counter counters[]; };

void main() {
    // this thread's index, 0..127
    uint i = gl_GlobalInvocationID.x;

    // only 100 numbers to add; the last 28 threads do nothing
    if (i >= 100u) return;

    // thread 0 adds 1, ..., thread 99 adds 100.
    // atomicAdd keeps simultaneous adds from overwriting each other
    atomicAdd(counters[0].count, i + 1u);
}
@end
@program sum_values cs
