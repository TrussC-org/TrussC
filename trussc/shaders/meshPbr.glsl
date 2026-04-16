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
in vec4 tangent;

out vec3 v_worldPos;
out vec3 v_worldNormal;
out vec3 v_worldTangent;
out float v_bitangentSign;
out vec2 v_uv;

void main() {
    vec4 wp = model * vec4(position, 1.0);
    v_worldPos = wp.xyz;
    v_worldNormal = normalize((normalMat * vec4(normal, 0.0)).xyz);
    v_worldTangent = normalize((normalMat * vec4(tangent.xyz, 0.0)).xyz);
    v_bitangentSign = tangent.w;
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
    vec4 iblParams;   // x=hasIbl (0 or 1), y=prefilterMaxLod, z=exposure, w=hasNormalMap
    vec4 lightPosType[MAX_LIGHTS];
    vec4 lightColorIntensity[MAX_LIGHTS];
    vec4 lightAttenuation[MAX_LIGHTS];    // xyz=c/l/q, w=spotOuterCos
    vec4 lightSpotDir[MAX_LIGHTS];        // xyz=spot direction, w=spotInnerCos
    mat4 projectorViewProj;               // single projector VP matrix
    vec4 projectorParams;                 // x=projectorLightIndex (-1=none), yzw=unused
};

// IBL resources. Bound only when iblParams.x > 0.5.
layout(binding=0) uniform textureCube irradianceMap;
layout(binding=0) uniform sampler irradianceSmp;
layout(binding=1) uniform textureCube prefilterMap;
layout(binding=1) uniform sampler prefilterSmp;
layout(binding=2) uniform texture2D brdfLut;
layout(binding=2) uniform sampler brdfLutSmp;

// Normal map
layout(binding=3) uniform texture2D normalMap;
layout(binding=3) uniform sampler normalMapSmp;

// Projector texture (modulates spot light color via projection)
layout(binding=4) uniform texture2D projectorTex;
layout(binding=4) uniform sampler projectorTexSmp;

in vec3 v_worldPos;
in vec3 v_worldNormal;
in vec3 v_worldTangent;
in float v_bitangentSign;
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

// Fresnel with roughness bias (for IBL diffuse kS computation, Karis 2013)
vec3 F_SchlickRoughness(float NdotV, vec3 F0, float roughness) {
    float f = pow(1.0 - NdotV, 5.0);
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * f;
}

// ACES filmic tonemapping (Narkowicz 2015 approximation)
vec3 tonemapACES(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Per-light evaluation
// ---------------------------------------------------------------------------

// type encoding: 0=directional, 1=point, 2=spot (+ optional projector)
vec3 evalLight(int lightIdx, vec4 posType, vec4 colorIntensity, vec4 atten, vec4 spotDir,
               vec3 N, vec3 V, vec3 worldPos,
               vec3 albedo, float metallic, float roughness, vec3 F0) {
    int type = int(posType.w + 0.5);

    vec3 toLight;
    float attenuation;
    if (type == 0) {
        // Directional
        toLight = normalize(-posType.xyz);
        attenuation = 1.0;
    } else {
        // Point or Spot (both have position + attenuation)
        vec3 d = posType.xyz - worldPos;
        float dist = length(d);
        toLight = d / max(dist, 1e-5);
        float denom = atten.x
                    + atten.y * dist
                    + atten.z * dist * dist;
        attenuation = 1.0 / max(denom, 1e-5);

        // Spot cone falloff
        if (type == 2) {
            vec3 sDir = normalize(spotDir.xyz);
            float theta = dot(-toLight, sDir);  // cosine of angle from spot axis
            float innerCos = spotDir.w;
            float outerCos = atten.w;
            float epsilon = innerCos - outerCos;
            float spotFalloff = clamp((theta - outerCos) / max(epsilon, 1e-5), 0.0, 1.0);
            attenuation *= spotFalloff;
        }
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

    // Projector texture modulation: multiply light color by the projected image
    int projIdx = int(projectorParams.x + 0.5);
    if (lightIdx == projIdx && type == 2) {
        vec4 clip = projectorViewProj * vec4(worldPos, 1.0);
        vec3 ndc = clip.xyz / clip.w;
        vec2 projUV = ndc.xy * 0.5 + 0.5;
        if (projUV.x >= 0.0 && projUV.x <= 1.0 &&
            projUV.y >= 0.0 && projUV.y <= 1.0 &&
            ndc.z >= -1.0 && ndc.z <= 1.0) {
            vec3 projColor = texture(sampler2D(projectorTex, projectorTexSmp), projUV).rgb;
            radiance *= projColor;
        } else {
            return vec3(0.0);  // outside projector frustum
        }
    }

    return (diffuse + specular) * radiance * NdotL;
}

// Construct TBN matrix from vertex tangent, or fall back to screen-space
// derivatives if no tangent was supplied (tangent vector is zero).
mat3 buildTBN(vec3 N, vec3 T, float bSign, vec3 worldPos, vec2 uv) {
    float tLen = length(T);
    if (tLen > 0.001) {
        // Explicit tangent supplied by the mesh
        T = normalize(T);
        // Re-orthogonalize against N (Gram-Schmidt)
        T = normalize(T - N * dot(N, T));
        vec3 B = cross(N, T) * bSign;
        return mat3(T, B, N);
    }
    // Derivatives fallback for meshes without tangent data
    vec3 dp1 = dFdx(worldPos);
    vec3 dp2 = dFdy(worldPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 dT = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 dB = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(dT, dT), dot(dB, dB)));
    return mat3(dT * invmax, dB * invmax, N);
}

void main() {
    vec3 N = normalize(v_worldNormal);
    vec3 V = normalize(cameraPos.xyz - v_worldPos);

    // Apply normal map if bound
    if (iblParams.w > 0.5) {
        mat3 TBN = buildTBN(N, v_worldTangent, v_bitangentSign, v_worldPos, v_uv);
        vec3 mapN = texture(sampler2D(normalMap, normalMapSmp), v_uv).xyz * 2.0 - 1.0;
        N = normalize(TBN * mapN);
    }

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
        Lo += evalLight(i, lightPosType[i], lightColorIntensity[i],
                        lightAttenuation[i], lightSpotDir[i],
                        N, V, v_worldPos,
                        albedo, metallic, roughness, F0);
    }

    // Indirect (IBL). Split-sum approximation:
    //   diffuse  ≈ irradianceMap(N) * albedo * kD
    //   specular ≈ prefilterMap(R, roughness*mipMax) * (F0 * brdf.x + brdf.y)
    vec3 ambient;
    if (iblParams.x > 0.5) {
        float NdotV = max(dot(N, V), 0.0);
        vec3 F = F_SchlickRoughness(NdotV, F0, roughness);
        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        vec3 irradiance = texture(samplerCube(irradianceMap, irradianceSmp), N).rgb;
        vec3 diffuse = irradiance * albedo;

        vec3 R = reflect(-V, N);
        float lod = roughness * iblParams.y;
        vec3 prefiltered = textureLod(samplerCube(prefilterMap, prefilterSmp), R, lod).rgb;
        vec2 envBRDF = texture(sampler2D(brdfLut, brdfLutSmp), vec2(NdotV, roughness)).rg;
        vec3 specular = prefiltered * (F * envBRDF.x + envBRDF.y);

        ambient = (kD * diffuse + specular) * ao;
    } else {
        // Fallback hemisphere ambient when no environment is bound.
        ambient = albedo * (1.0 - metallic) * 0.03 * ao;
    }

    vec3 color = ambient + Lo + emissive.rgb * emissiveK;

    // Apply exposure, ACES filmic tonemap, then sRGB gamma.
    color *= iblParams.z;
    color = tonemapACES(color);
    color = pow(color, vec3(1.0 / 2.2));

    frag_color = vec4(color, baseColor.a);
}
@end

@program pbr_mesh vs_pbr fs_pbr
