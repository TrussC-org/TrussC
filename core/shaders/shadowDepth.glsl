@module tc_shadow
// =============================================================================
// shadowDepth.glsl - Shadow map depth-only shader
// =============================================================================
// Stores clip.w (= -z_eye = distance from light) into R32F.
// Linear depth gives uniform precision across the entire range, unlike
// NDC z which clusters near 1.0 for far objects.
// =============================================================================

@vs vs_shadow
layout(binding=0) uniform shadow_vs_params {
    mat4 model;
    mat4 lightViewProj;
    // xyz = light direction (normalized), w = mode (0=perspective, 1=ortho)
    vec4 depthParams;
    // x = refDot = dot(eye, lightDir) for ortho; yzw unused
    vec4 depthParams2;
};

in vec3 position;
out float v_linearDepth;

void main() {
    vec4 worldPos = model * vec4(position, 1.0);
    vec4 clipPos = lightViewProj * worldPos;
    gl_Position = clipPos;
    // Store LINEAR depth in WORLD units, independent of the NDC z convention.
    // Perspective: clip.w = -z_eye (distance from light).
    // Ortho: clip.w == 1, so instead project the world position onto the light
    // direction and subtract the eye's projection (refDot) -> signed distance
    // from the near plane, still linear in world units.
    if (depthParams.w > 0.5) {
        v_linearDepth = dot(worldPos.xyz, depthParams.xyz) - depthParams2.x;
    } else {
        v_linearDepth = clipPos.w;
    }
}
@end

@fs fs_shadow
in float v_linearDepth;
out vec4 frag_color;

void main() {
    frag_color = vec4(v_linearDepth, 0.0, 0.0, 1.0);
}
@end

@program shadow_depth vs_shadow fs_shadow
