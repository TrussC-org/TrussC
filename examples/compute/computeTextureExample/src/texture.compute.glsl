@cs cs
layout(local_size_x=8, local_size_y=8, local_size_z=1) in;
layout(binding=0, rgba8) uniform writeonly image2D output_image;
void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(output_image);
    if (any(greaterThanEqual(p, size))) return;
    imageStore(output_image, p, vec4(vec2(p) / vec2(size), 0.5, 1.0));
}
@end
@program generate_texture cs
