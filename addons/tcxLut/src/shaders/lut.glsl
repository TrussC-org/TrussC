// =============================================================================
// lut.glsl - 1D / 3D Look-Up Table shader for color grading
// =============================================================================
// Applies a per-channel 1D curve, a 3D table, or both in that order.
// =============================================================================

// Vertex shader
@vs vs
layout(binding=1) uniform lut_vs_params {
    mat4 mvp;
    vec4 rect;
};
in vec2 position;
in vec2 texcoord0;
out vec2 uv;

void main() {
    gl_Position = mvp * vec4(rect.xy + vec2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5) * rect.zw, 0.0, 1.0);
    uv = texcoord0;
}
@end

// Fragment shader - with LUT
@fs fs_lut
layout(binding=0) uniform texture2D srcTex;
layout(binding=0) uniform sampler srcSmp;
layout(binding=1) uniform texture3D lutTex;
layout(binding=1) uniform sampler lutSmp;
layout(binding=2) uniform texture2D curveTex;
layout(binding=2) uniform sampler curveSmp;
@image_sample_type curveTex unfilterable_float
@sampler_type curveSmp nonfiltering

layout(binding=0) uniform fs_params {
    float lutSize;
    float blend;
    float curveSize;
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

    vec3 lutColor = src.rgb;
    if (curveSize > 0.0) {
        vec3 position = clamp(lutColor, 0.0, 1.0) * (curveSize - 1.0);
        ivec3 lower = ivec3(floor(position));
        ivec3 upper = min(lower + ivec3(1), ivec3(int(curveSize) - 1));
        // Explicit fetches keep RGBA32F usable without float filtering (WebGPU).
        vec3 a = vec3(texelFetch(sampler2D(curveTex, curveSmp), ivec2(lower.r, 0), 0).r,
                      texelFetch(sampler2D(curveTex, curveSmp), ivec2(lower.g, 0), 0).g,
                      texelFetch(sampler2D(curveTex, curveSmp), ivec2(lower.b, 0), 0).b);
        vec3 b = vec3(texelFetch(sampler2D(curveTex, curveSmp), ivec2(upper.r, 0), 0).r,
                      texelFetch(sampler2D(curveTex, curveSmp), ivec2(upper.g, 0), 0).g,
                      texelFetch(sampler2D(curveTex, curveSmp), ivec2(upper.b, 0), 0).b);
        lutColor = mix(a, b, position - vec3(lower));
    }
    if (lutSize > 0.0) {
        float scale = (lutSize - 1.0) / lutSize;
        float offset = 0.5 / lutSize;
        vec3 lutCoord = clamp(lutColor, 0.0, 1.0) * scale + offset;
        lutColor = texture(sampler3D(lutTex, lutSmp), lutCoord).rgb;
    }

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
