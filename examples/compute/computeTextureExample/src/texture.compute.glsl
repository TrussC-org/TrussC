@cs cs
// 8x8 threads per group; tcApp dispatches 32x32 groups = one thread per pixel of 256x256
layout(local_size_x=8, local_size_y=8, local_size_z=1) in;

// the texture this shader writes into (tcApp binds it with setStorageImage)
layout(binding=0, rgba8) uniform writeonly image2D output_image;

// x = seconds since start, set by tcApp every frame
layout(binding=0) uniform texture_params { vec4 params; };

void main() {
    // this thread's pixel
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);

    // skip threads outside the image (not needed at 256x256, but safe for other sizes)
    ivec2 size = imageSize(output_image);
    if (any(greaterThanEqual(p, size))) return;

    // pixel position as 0..1
    vec2 uv = vec2(p) / vec2(size);

    // rings that move outward from the center as time passes
    float dist = length(uv - 0.5);
    float ripple = 0.5 + 0.5 * sin(dist * 40.0 - params.x * 4.0);

    // red = x, green = y, blue = the moving rings
    imageStore(output_image, p, vec4(uv, ripple, 1.0));
}
@end
@program generate_texture cs
