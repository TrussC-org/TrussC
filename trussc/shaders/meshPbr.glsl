// =============================================================================
// meshPbr.glsl - Physically Based Rendering mesh shader
// =============================================================================
// Metallic-roughness workflow, Cook-Torrance GGX BRDF, direct lighting only.
// Up to 8 directional or point lights. No IBL, no normal mapping, no shadows.
//
// Used by LightingMode::GpuPbr via the internal PbrPipeline singleton.
// Vertex layout: pos(float3) + normal(float3) + texcoord0(float2) = 32 bytes.
// =============================================================================

@vs vs_pbr
layout(binding=0) uniform vs_params {
    mat4 model;
    mat4 viewProj;
    mat4 normalMat;
};

in vec3 position;
in vec3 normal;
in vec2 texcoord0;

out vec3 v_worldPos;
out vec3 v_worldNormal;
out vec2 v_uv;

void main() {
    vec4 wp = model * vec4(position, 1.0);
    v_worldPos = wp.xyz;
    // normalMat is the inverse-transpose of the upper-left 3x3 of the model
    // matrix, expanded to mat4. The w component of the input is zero so any
    // translation in column 4 is discarded.
    v_worldNormal = normalize((normalMat * vec4(normal, 0.0)).xyz);
    v_uv = texcoord0;
    gl_Position = viewProj * wp;
}
@end

@fs fs_pbr
#define MAX_LIGHTS 8
#define PI 3.14159265359
#define INV_PI 0.31830988618

// Per-light data is flattened into parallel vec4 arrays because sokol-shdc
// does not allow struct types inside uniform blocks.
//
//   lightPosType[i]:        xyz=position (point) or direction (directional),
//                           w=type (0=directional, 1=point)
//   lightColorIntensity[i]: rgb=light color, a=intensity scalar
//   lightAttenuation[i]:    x=constant, y=linear, z=quadratic, w=unused
layout(binding=1) uniform fs_params {
    vec4 baseColor;   // rgb=albedo, a=alpha
    vec4 pbrParams;   // x=metallic, y=roughness, z=ao, w=emissiveStrength
    vec4 emissive;    // rgb=emissive color
    vec4 cameraPos;   // xyz=camera world pos, w=numLights (as float)
    vec4 lightPosType[MAX_LIGHTS];
    vec4 lightColorIntensity[MAX_LIGHTS];
    vec4 lightAttenuation[MAX_LIGHTS];
};

in vec3 v_worldPos;
in vec3 v_worldNormal;
in vec2 v_uv;

out vec4 frag_color;

// ---------------------------------------------------------------------------
// Cook-Torrance BRDF building blocks
// ---------------------------------------------------------------------------

// Trowbridge-Reitz / GGX normal distribution
float D_GGX(float NdotH, float alpha) {
    float a2 = alpha * alpha;
    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Smith height-correlated visibility term (includes 1/(4 NdotL NdotV))
float V_SmithGGXCorrelated(float NdotV, float NdotL, float alpha) {
    float a2 = alpha * alpha;
    float ggxV = NdotL * sqrt((NdotV - a2 * NdotV) * NdotV + a2);
    float ggxL = NdotV * sqrt((NdotL - a2 * NdotL) * NdotL + a2);
    return 0.5 / max(ggxV + ggxL, 1e-5);
}

// Schlick Fresnel approximation
vec3 F_Schlick(float VdotH, vec3 F0) {
    float f = pow(1.0 - VdotH, 5.0);
    return F0 + (vec3(1.0) - F0) * f;
}

// ---------------------------------------------------------------------------
// Per-light evaluation
// ---------------------------------------------------------------------------

vec3 evalLight(vec4 posType, vec4 colorIntensity, vec4 atten,
               vec3 N, vec3 V, vec3 worldPos,
               vec3 albedo, float metallic, float roughness, vec3 F0) {
    bool isPoint = posType.w > 0.5;

    vec3 toLight;
    float attenuation;
    if (isPoint) {
        vec3 d = posType.xyz - worldPos;
        float dist = length(d);
        toLight = d / max(dist, 1e-5);
        float denom = atten.x
                    + atten.y * dist
                    + atten.z * dist * dist;
        attenuation = 1.0 / max(denom, 1e-5);
    } else {
        toLight = normalize(-posType.xyz);
        attenuation = 1.0;
    }

    vec3 L = toLight;
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    float NdotV = max(dot(N, V), 1e-5);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float alpha = roughness * roughness;

    float D = D_GGX(NdotH, alpha);
    float Vis = V_SmithGGXCorrelated(NdotV, NdotL, alpha);
    vec3 F = F_Schlick(VdotH, F0);

    vec3 specular = D * Vis * F;

    // Energy conservation: diffuse only for non-metals, and scaled by (1 - F)
    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kd * albedo * INV_PI;

    vec3 radiance = colorIntensity.rgb * colorIntensity.a * attenuation;
    return (diffuse + specular) * radiance * NdotL;
}

void main() {
    vec3 N = normalize(v_worldNormal);
    vec3 V = normalize(cameraPos.xyz - v_worldPos);

    vec3 albedo      = baseColor.rgb;
    float metallic   = pbrParams.x;
    float roughness  = pbrParams.y;
    float ao         = pbrParams.z;
    float emissiveK  = pbrParams.w;

    // F0 for dielectrics is 0.04, for metals it's the albedo (tinted reflection)
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    int numLights = int(cameraPos.w + 0.5);

    vec3 Lo = vec3(0.0);
    for (int i = 0; i < MAX_LIGHTS; i++) {
        if (i >= numLights) break;
        Lo += evalLight(lightPosType[i], lightColorIntensity[i], lightAttenuation[i],
                        N, V, v_worldPos,
                        albedo, metallic, roughness, F0);
    }

    // Simple hemisphere ambient (no IBL yet). Scaled by AO.
    vec3 ambient = albedo * (1.0 - metallic) * 0.03 * ao;

    vec3 color = ambient + Lo + emissive.rgb * emissiveK;

    // Gamma correct (no tonemap in this phase)
    color = pow(color, vec3(1.0 / 2.2));

    frag_color = vec4(color, baseColor.a);
}
@end

@program pbr_mesh vs_pbr fs_pbr
