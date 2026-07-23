@module tc_pbr
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
#define MAX_SHADOW_LIGHTS 4
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
    vec4 iesParams;                       // x=iesLightIndex (-1=none), y=maxVertAngle (rad), zw=unused
    vec4 texFlags;                        // x=hasBaseColorTex, y=hasMetRoughTex, z=hasEmissiveTex, w=hasOcclusionTex
    mat4 shadowViewProj[MAX_SHADOW_LIGHTS];   // per-slot light VP for shadow depth comparison
    vec4 shadowSlotParams[MAX_SHADOW_LIGHTS]; // x=lightIndex (-1=unused), y=bias, z=strength, w=unused
    vec4 shadowMapParams;                     // x=mapSize, y=numShadowSlots, z=originTopLeft, w=unused
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

// IES photometric profile (1D intensity lookup, U = vertical angle / maxAngle)
layout(binding=5) uniform texture2D iesProfileTex;
layout(binding=5) uniform sampler iesProfileTexSmp;

// PBR material texture maps (glTF 2.0 convention)
layout(binding=6) uniform texture2D baseColorTex;
layout(binding=6) uniform sampler baseColorTexSmp;
layout(binding=7) uniform texture2D metallicRoughnessTex;
layout(binding=7) uniform sampler metallicRoughnessTexSmp;
layout(binding=8) uniform texture2D emissiveTex;
layout(binding=8) uniform sampler emissiveTexSmp;
layout(binding=9) uniform texture2D occlusionTex;
layout(binding=9) uniform sampler occlusionTexSmp;

// Shadow map array (R32F depth from each shadow light's POV, one layer per slot)
layout(binding=10) uniform texture2DArray shadowMap;
layout(binding=10) uniform sampler shadowMapSmp;

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
// Shadow map sampling with bilinear-PCF (3x3 kernel of software-sampler2DShadow
// taps) and slope-scaled bias
// ---------------------------------------------------------------------------

// One bilinear PCF tap emulating a hardware sampler2DShadow: fetch the 4 texels
// surrounding `uv`, binary-compare EACH stored depth against `compareDepth`
// first, then bilerp the four 0/1 results by the sub-texel fraction. Comparing
// before filtering is essential — filtering the depths first would invent a
// phantom averaged occluder along shadow edges.
float shadowTapBilinear(vec2 uv, float layer, float compareDepth, float mapSize) {
    // NEAREST sampler: texel centers sit at (i+0.5)/mapSize. Shift by -0.5 so
    // floor() lands on the lower-left texel and `f` is measured from centers.
    vec2 texel = uv * mapSize - 0.5;
    vec2 base = floor(texel);
    vec2 f = texel - base;
    float invSize = 1.0 / mapSize;

    vec2 uv00 = (base + vec2(0.5, 0.5)) * invSize;
    vec2 uv10 = (base + vec2(1.5, 0.5)) * invSize;
    vec2 uv01 = (base + vec2(0.5, 1.5)) * invSize;
    vec2 uv11 = (base + vec2(1.5, 1.5)) * invSize;

    float d00 = texture(sampler2DArray(shadowMap, shadowMapSmp), vec3(uv00, layer)).r;
    float d10 = texture(sampler2DArray(shadowMap, shadowMapSmp), vec3(uv10, layer)).r;
    float d01 = texture(sampler2DArray(shadowMap, shadowMapSmp), vec3(uv01, layer)).r;
    float d11 = texture(sampler2DArray(shadowMap, shadowMapSmp), vec3(uv11, layer)).r;

    float s00 = (compareDepth > d00) ? 0.0 : 1.0;
    float s10 = (compareDepth > d10) ? 0.0 : 1.0;
    float s01 = (compareDepth > d01) ? 0.0 : 1.0;
    float s11 = (compareDepth > d11) ? 0.0 : 1.0;

    return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
}

// 16-tap Poisson disk (unit radius) reused for the PCSS blocker search and the
// variable-radius PCF filter. A fixed const array keeps the loops bounded so
// sokol-shdc can cross-compile to every backend (Metal/HLSL/WGSL/GLSL).
const vec2 poisson16[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790)
);

// Convert a world-space radius (at the receiver) into a shadow-map UV radius.
// The shadow projection is a perspective frustum, so 1 world unit maps to
// uvPerWorld = 0.5 * projScale / linearDepth UV units, where projScale is the
// projection's x-axis scale (matrix element [0][0] = near/halfW). Because our
// spot lights are rotated, [0][0] alone is not the scale — the view rotation
// mixes the axes — so we take the length of the clip.x world-gradient
// (m[0][0], m[1][0], m[2][0]), which is rotation-invariant and equals near/halfW.
// The 1/linearDepth term makes a fixed world penumbra shrink in UV with
// distance, i.e. the penumbra stays constant in world space.
float shadowUvPerWorld(int slot, float linearDepth) {
    vec3 gradClipX = vec3(shadowViewProj[slot][0][0],
                          shadowViewProj[slot][1][0],
                          shadowViewProj[slot][2][0]);
    float projScale = length(gradClipX);
    return 0.5 * projScale / max(linearDepth, 1e-4);
}

float calcShadow(int slot, vec3 worldPos, vec3 N, vec3 L) {
    vec4 clip = shadowViewProj[slot] * vec4(worldPos, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 shadowUV = ndc.xy * 0.5 + 0.5;

    // The shadow map is an offscreen render target. On top-left-origin
    // backends (Metal / D3D11 / WebGPU) its rows are stored flipped relative
    // to the GL convention assumed by the NDC->UV mapping above, so sampling
    // without a flip reads the mirror image: shadows land offset and half the
    // receiver plane self-shadows along a straight line (the v=0.5 axis).
    // shadowMapParams.z carries sg_query_features().origin_top_left.
    if (shadowMapParams.z > 0.5) {
        shadowUV.y = 1.0 - shadowUV.y;
    }

    // Outside shadow frustum = fully lit
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
        shadowUV.y < 0.0 || shadowUV.y > 1.0 ||
        clip.w < 0.0) {  // behind the light
        return 1.0;
    }

    // Linear depth comparison: clip.w = -z_eye (distance from light).
    // Shadow pass stores clip.w, so we compute the same here.
    float currentDepth = clip.w;

    // Slope-scaled bias: the stored depth is linear/world-scale, so constBias
    // is a world-space offset. On surfaces grazing the light (near the
    // terminator) a single texel spans far more depth, so a constant bias
    // under-shoots and self-shadow acne appears. Scale by tan(angle between N
    // and L); tan is clamped so grazing pixels don't peter-pan. At full facing
    // (N==L) the slope term is 0, so lit surfaces keep the exact constBias
    // behavior existing apps were tuned against.
    float constBias = shadowSlotParams[slot].y;  // in world units
    float ndl = clamp(dot(N, L), 0.001, 1.0);
    float slope = min(tan(acos(ndl)), 8.0);
    float bias = constBias * (1.0 + slope);
    float compareDepth = currentDepth - bias;

    float mapSize = max(shadowMapParams.x, 1.0);
    float texelSize = 1.0 / mapSize;

    float softness = shadowSlotParams[slot].w;  // emitter size (world units)

    // -----------------------------------------------------------------------
    // Phase A path (softness == 0): fixed 3x3 kernel of bilinear taps.
    // Zero extra cost, identical to the tuned hard-shadow look.
    // -----------------------------------------------------------------------
    if (softness <= 0.0) {
        float shadow = 0.0;
        for (int x = -1; x <= 1; x++) {
            for (int y = -1; y <= 1; y++) {
                vec2 offset = vec2(float(x), float(y)) * texelSize;
                shadow += shadowTapBilinear(shadowUV + offset, float(slot), compareDepth, mapSize);
            }
        }
        shadow /= 9.0;
        return mix(1.0, shadow, shadowSlotParams[slot].z);
    }

    // -----------------------------------------------------------------------
    // PCSS path (softness > 0): blocker search -> penumbra estimate -> PCF.
    // Our shadow map stores LINEAR world-scale depth, so the penumbra formula
    // is exact and contact hardening falls out for free: near a contact point
    // (receiverDepth ~= blockerDepth) the penumbra collapses to ~0 and the
    // filter converges to the hard look; far from the caster it widens.
    // -----------------------------------------------------------------------
    float uvPerWorld = shadowUvPerWorld(slot, currentDepth);

    // Per-fragment random rotation of the Poisson disk. A single fixed disk
    // shared by every fragment makes all penumbrae sample the SAME 16 directions,
    // so at large softness the taps line up into concentric bands / ghost edges.
    // Rotating the disk by a per-pixel angle turns that structured banding into
    // fine dithered noise. Interleaved Gradient Noise (Jimenez 2014) on the
    // fragment coordinate gives a cheap, well-distributed hash. We build the 2x2
    // rotation once and reuse it for BOTH the blocker search and the PCF loop
    // (sharing is fine and cheaper — the noise decorrelates neighbours anyway).
    const float TAU = 6.28318530718;
    float ign = fract(52.9829189 * fract(dot(gl_FragCoord.xy,
                                            vec2(0.06711056, 0.00583715))));
    float angle = TAU * ign;
    float ca = cos(angle);
    float sa = sin(angle);
    mat2 rot = mat2(ca, -sa, sa, ca);

    // Blocker search: sample the Poisson disk over a search radius derived from
    // the emitter size, clamped so we always cover a couple of texels (find the
    // caster even for tiny emitters) but never blow up the sample footprint.
    float searchUV = clamp(softness * uvPerWorld, 1.5 * texelSize, 16.0 * texelSize);
    float blockerSum = 0.0;
    float blockerCount = 0.0;
    for (int i = 0; i < 16; i++) {
        vec2 o = (rot * poisson16[i]) * searchUV;
        float d = texture(sampler2DArray(shadowMap, shadowMapSmp), vec3(shadowUV + o, float(slot))).r;
        // Closer to the light than the (biased) receiver -> it's a blocker.
        if (d < compareDepth) {
            blockerSum += d;
            blockerCount += 1.0;
        }
    }

    // No blockers found -> fully lit (mix with strength is a no-op at 1.0).
    if (blockerCount < 0.5) {
        return 1.0;
    }
    float avgBlocker = blockerSum / blockerCount;

    // Penumbra width in world units (exact for linear depth), then to UV.
    // Clamp the UV radius to 16 texels to bound cost and ringing artifacts.
    float penumbraWorld = softness * max(currentDepth - avgBlocker, 0.0) / max(avgBlocker, 1e-4);
    float radiusUV = clamp(penumbraWorld * uvPerWorld, 0.0, 16.0 * texelSize);

    // Variable-radius PCF over the same Poisson disk. As radiusUV collapses
    // below a texel the taps pile into one texel and the result hardens.
    float shadow = 0.0;
    for (int i = 0; i < 16; i++) {
        vec2 o = (rot * poisson16[i]) * radiusUV;
        shadow += shadowTapBilinear(shadowUV + o, float(slot), compareDepth, mapSize);
    }
    shadow /= 16.0;

    return mix(1.0, shadow, shadowSlotParams[slot].z);
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

        // Spot cone falloff. Skipped for projector lights — the projector
        // frustum defines the boundary instead of the spot cone.
        // projectorParams.x is -1 when no projector exists; compare with a
        // threshold first to avoid int(-0.5)→0 truncation matching light 0.
        bool isProjector = (projectorParams.x >= -0.5) &&
                           (lightIdx == int(projectorParams.x + 0.5));
        if (type == 2 && !isProjector) {
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

    // IES photometric profile: modulate intensity by angular distribution.
    // The texture stores normalized candela as a 1D lookup (U = vertAngle / maxAngle).
    // iesParams.x stores the light index (>=0) or -1 for none. We compare with
    // a threshold rather than rounding to avoid int(-0.5)→0 truncation ambiguity.
    if (iesParams.x >= -0.5 && lightIdx == int(iesParams.x + 0.5) && type != 0) {
        // Direction from light to fragment (= aim direction at 0 vertical angle)
        vec3 toFrag = -toLight;
        // spotDir.xyz is the light's aiming direction
        vec3 aimDir = normalize(spotDir.xyz);
        // Vertical angle: angle between fragment direction and aim axis
        float cosAngle = dot(toFrag, aimDir);
        float vertAngle = acos(clamp(cosAngle, -1.0, 1.0));
        float maxAngle = iesParams.y;
        float u = clamp(vertAngle / max(maxAngle, 0.001), 0.0, 1.0);
        float iesIntensity = texture(sampler2D(iesProfileTex, iesProfileTexSmp), vec2(u, 0.5)).r;
        radiance *= iesIntensity;
    }

    // Projector texture modulation: multiply light color by the projected image.
    // Threshold check first: with no projector (x = -1), int(-0.5) truncates
    // to 0 and would wrongly treat light 0 as the projector.
    if (projectorParams.x >= -0.5 && lightIdx == int(projectorParams.x + 0.5) && type == 2) {
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
    float alpha      = baseColor.a;
    float metallic   = pbrParams.x;
    float roughness  = pbrParams.y;
    float ao         = pbrParams.z;
    float emissiveK  = pbrParams.w;

    // Apply PBR texture maps (each multiplies the corresponding scalar)
    if (texFlags.x > 0.5) {
        vec4 bc = texture(sampler2D(baseColorTex, baseColorTexSmp), v_uv);
        albedo *= bc.rgb;
        alpha  *= bc.a;
    }
    if (texFlags.y > 0.5) {
        vec4 mr = texture(sampler2D(metallicRoughnessTex, metallicRoughnessTexSmp), v_uv);
        roughness *= mr.g;   // glTF: green channel = roughness
        metallic  *= mr.b;   // glTF: blue channel = metallic
    }
    if (texFlags.w > 0.5) {
        float aoTex = texture(sampler2D(occlusionTex, occlusionTexSmp), v_uv).r;
        ao *= aoTex;
    }

    // F0 for dielectrics is 0.04, for metals it's the albedo (tinted reflection)
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    int numLights = int(cameraPos.w + 0.5);

    vec3 Lo = vec3(0.0);
    int numShadowSlots = int(shadowMapParams.y + 0.5);

    for (int i = 0; i < MAX_LIGHTS; i++) {
        if (i >= numLights) break;
        vec3 contribution = evalLight(i, lightPosType[i], lightColorIntensity[i],
                                      lightAttenuation[i], lightSpotDir[i],
                                      N, V, v_worldPos,
                                      albedo, metallic, roughness, F0);
        // Apply the shadow of the slot owned by this light (if any)
        for (int s = 0; s < MAX_SHADOW_LIGHTS; s++) {
            if (s >= numShadowSlots) break;
            if (int(shadowSlotParams[s].x + 0.5) == i) {
                // Direction to the owning light, for slope-scaled bias.
                vec4 pt = lightPosType[i];
                vec3 shadowL;
                if (int(pt.w + 0.5) == 0) {
                    shadowL = normalize(-pt.xyz);  // directional
                } else {
                    shadowL = normalize(pt.xyz - v_worldPos);  // point / spot
                }
                contribution *= calcShadow(s, v_worldPos, N, shadowL);
                break;
            }
        }
        Lo += contribution;
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

    vec3 emissiveColor = emissive.rgb;
    if (texFlags.z > 0.5) {
        emissiveColor *= texture(sampler2D(emissiveTex, emissiveTexSmp), v_uv).rgb;
    }
    vec3 color = ambient + Lo + emissiveColor * emissiveK;

    // Apply exposure, ACES filmic tonemap, then sRGB gamma.
    color *= iblParams.z;
    color = tonemapACES(color);
    color = pow(color, vec3(1.0 / 2.2));

    frag_color = vec4(color, alpha);
}
@end

@program pbr_mesh vs_pbr fs_pbr
