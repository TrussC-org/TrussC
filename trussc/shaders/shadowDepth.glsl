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
};

in vec3 position;
out float v_linearDepth;

void main() {
    vec4 clipPos = lightViewProj * model * vec4(position, 1.0);
    gl_Position = clipPos;
    v_linearDepth = clipPos.w;  // = -z_eye (distance from light)
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
