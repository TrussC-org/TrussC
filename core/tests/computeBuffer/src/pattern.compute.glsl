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
