@cs cs
layout(local_size_x=8, local_size_y=8, local_size_z=1) in;
layout(binding=0, rgba8) uniform writeonly image2D output_image;
layout(binding=1) uniform texture2D source_image;
layout(binding=1) uniform sampler source_sampler;
layout(binding=0) uniform params { vec4 tint; };
void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(p, imageSize(output_image)))) return;
    vec4 source = texelFetch(sampler2D(source_image, source_sampler), p, 0);
    imageStore(output_image, p, source * tint);
}
@end
@program compute_pattern cs

@cs half_cs
layout(local_size_x=8, local_size_y=8, local_size_z=1) in;
layout(binding=0, rgba16f) uniform writeonly image2D half_output;
void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(p, imageSize(half_output)))) return;
    imageStore(half_output, p, vec4(0.25, 0.5, 0.75, 1.0));
}
@end
@program compute_half half_cs
