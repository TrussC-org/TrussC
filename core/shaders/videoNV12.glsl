@module tc_video_nv12
// NV12 (NVDEC output) → RGB shader for VideoPlayer on Linux/CUDA.
// Eliminates CPU-side sws_scale() by doing YUV→RGB in the fragment shader.
// Y plane:  R8  texture, full resolution
// UV plane: RG8 texture, half resolution (chroma subsampling)

@vs vs_nv12
layout(binding=0) uniform vs_params {
    mat4 mvp;
    vec4 rect; // destination x, y, width, height in target coordinates
};

in vec2 position;   // unit quad: (0,0) → (1,1)
out vec2 v_uv;

void main() {
    v_uv = position;
    gl_Position = mvp * vec4(rect.xy + position * rect.zw, 0.0, 1.0);
}
@end

@fs fs_nv12
layout(binding=0) uniform texture2D texY;
layout(binding=0) uniform sampler smpY;
layout(binding=1) uniform texture2D texUV;
layout(binding=1) uniform sampler smpUV;

in vec2 v_uv;
out vec4 frag_color;

void main() {
    float y  = texture(sampler2D(texY,  smpY),  v_uv).r;
    vec2  uv = texture(sampler2D(texUV, smpUV), v_uv).rg;

    // BT.601 limited-range YCbCr → RGB
    float yy = (y  - 16.0 / 255.0) * (255.0 / 219.0);
    float u  = (uv.r - 0.5) * (255.0 / 224.0);
    float v  = (uv.g - 0.5) * (255.0 / 224.0);

    frag_color = vec4(
        clamp(yy + 1.402   * v,               0.0, 1.0),
        clamp(yy - 0.34414 * u - 0.71414 * v, 0.0, 1.0),
        clamp(yy + 1.772   * u,               0.0, 1.0),
        1.0
    );
}
@end

@program nv12 vs_nv12 fs_nv12
