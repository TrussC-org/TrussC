// =============================================================================
// lut.glsl - LUT (3D Look-Up Table) shader for color grading
// =============================================================================
// Applies 3D LUT color transformation to input texture
// =============================================================================

// Vertex shader
@vs vs
in vec2 position;
in vec2 texcoord0;
out vec2 uv;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    uv = texcoord0;
}
@end

// Fragment shader - with LUT
@fs fs_lut
layout(binding=0) uniform texture2D srcTex;
layout(binding=0) uniform sampler srcSmp;
layout(binding=1) uniform texture3D lutTex;
layout(binding=1) uniform sampler lutSmp;

layout(binding=0) uniform fs_params {
    float lutSize;
    float blend;
    float _pad0;
    float _pad1;
    vec4 viewport;  // x, y, w, h (normalized 0-1)
};

in vec2 uv;
out vec4 frag_color;

void main() {
    // Map UV from viewport rect to full texture coordinates
    vec2 texUV = viewport.xy + uv * viewport.zw;

    // Sample source texture
    vec4 src = texture(sampler2D(srcTex, srcSmp), texUV);

    // Calculate LUT sampling coordinates (texel center)
    // The scale and offset ensure we sample at texel centers
    float scale = (lutSize - 1.0) / lutSize;
    float offset = 0.5 / lutSize;
    vec3 lutCoord = src.rgb * scale + offset;

    // Sample 3D LUT
    vec3 lutColor = texture(sampler3D(lutTex, lutSmp), lutCoord).rgb;

    // Blend between original and LUT-applied color
    frag_color = vec4(mix(src.rgb, lutColor, blend), src.a);
}
@end

@program lut_apply vs fs_lut

// Fragment shader - passthrough (no LUT, just draw texture)
@fs fs_passthrough
layout(binding=0) uniform texture2D srcTex;
layout(binding=0) uniform sampler srcSmp;

layout(binding=0) uniform fs_params {
    float lutSize;
    float blend;
    float _pad0;
    float _pad1;
    vec4 viewport;
};

in vec2 uv;
out vec4 frag_color;

void main() {
    vec2 texUV = viewport.xy + uv * viewport.zw;
    frag_color = texture(sampler2D(srcTex, srcSmp), texUV);
}
@end

@program passthrough vs fs_passthrough
