@module tc_ibl
// =============================================================================
// iblBake.glsl - Image-based lighting pre-computation shaders
// =============================================================================
// Four fullscreen programs used once at Environment::build() time to populate
// the cubemaps that feed meshPbr's IBL term:
//
//   1. equirect_to_cube - project a 2D equirectangular HDR into a cubemap face
//   2. irradiance       - cosine-weighted hemisphere integral (diffuse IBL)
//   3. prefilter        - GGX importance-sampled specular convolution per mip
//   4. brdf_lut         - split-sum specular BRDF LUT (2D, not a cube)
//
// All programs share a common VS that draws a fullscreen quad. The caller is
// responsible for: (a) binding the appropriate target attachment (cube face +
// mip, or 2D for brdf_lut) before sg_begin_pass, and (b) supplying the right
// uniform block values per invocation.
// =============================================================================

@vs vs_bake
in vec2 position;
out vec2 v_uv;

void main() {
    v_uv = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
@end

// =============================================================================
// Shared helpers (pasted into each fragment shader because sokol-shdc does not
// support #include or cross-stage helper blocks).
// =============================================================================
// // vec3 faceUvToDir(int face, vec2 uv) { ... }
// // Returns a world-space direction for the given cube face and in-face uv
// // in [0,1]^2, using the GL/sokol cubemap face conventions:
// //   0:+X  1:-X  2:+Y  3:-Y  4:+Z  5:-Z
// =============================================================================

// -----------------------------------------------------------------------------
// 1. Equirect → Cubemap
// -----------------------------------------------------------------------------
@fs fs_equirect_to_cube
layout(binding=0) uniform texture2D equirectTex;
layout(binding=0) uniform sampler equirectSmp;

layout(binding=0) uniform equirect_params {
    vec4 faceIdx;  // x = face index as float (0..5)
};

in vec2 v_uv;
out vec4 frag_color;

vec3 faceUvToDir(int f, vec2 uv) {
    vec2 n = uv * 2.0 - 1.0;
    if (f == 0) return normalize(vec3( 1.0, -n.y, -n.x));
    if (f == 1) return normalize(vec3(-1.0, -n.y,  n.x));
    if (f == 2) return normalize(vec3( n.x,  1.0,  n.y));
    if (f == 3) return normalize(vec3( n.x, -1.0, -n.y));
    if (f == 4) return normalize(vec3( n.x, -n.y,  1.0));
    return              normalize(vec3(-n.x, -n.y, -1.0));
}

void main() {
    int f = int(faceIdx.x + 0.5);
    vec3 dir = faceUvToDir(f, v_uv);
    // Map direction to equirect UV:
    //   phi   = atan2(z, x)  ∈ [-pi, pi]
    //   theta = asin(y)      ∈ [-pi/2, pi/2]
    float phi = atan(dir.z, dir.x);
    float theta = asin(clamp(dir.y, -1.0, 1.0));
    vec2 uv = vec2(phi * 0.15915494 + 0.5,          // 1 / (2*pi)
                   0.5 - theta * 0.31830989); // 1 / pi, flipped: v=0 is north pole
    vec3 color = texture(sampler2D(equirectTex, equirectSmp), uv).rgb;
    frag_color = vec4(color, 1.0);
}
@end
@program equirect_to_cube vs_bake fs_equirect_to_cube

// -----------------------------------------------------------------------------
// 2. Irradiance convolution (diffuse IBL)
// -----------------------------------------------------------------------------
@fs fs_irradiance
layout(binding=0) uniform textureCube envTex;
layout(binding=0) uniform sampler envSmp;

layout(binding=0) uniform irradiance_params {
    vec4 faceIdx;  // x = face index
};

in vec2 v_uv;
out vec4 frag_color;

vec3 faceUvToDir(int f, vec2 uv) {
    vec2 n = uv * 2.0 - 1.0;
    if (f == 0) return normalize(vec3( 1.0, -n.y, -n.x));
    if (f == 1) return normalize(vec3(-1.0, -n.y,  n.x));
    if (f == 2) return normalize(vec3( n.x,  1.0,  n.y));
    if (f == 3) return normalize(vec3( n.x, -1.0, -n.y));
    if (f == 4) return normalize(vec3( n.x, -n.y,  1.0));
    return              normalize(vec3(-n.x, -n.y, -1.0));
}

void main() {
    int f = int(faceIdx.x + 0.5);
    vec3 N = faceUvToDir(f, v_uv);

    // Build tangent frame around N
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    const float TWO_PI = 6.28318530718;
    const float HALF_PI = 1.57079632679;
    const float PI = 3.14159265359;
    const float DELTA_PHI = 0.025;
    const float DELTA_THETA = 0.025;

    vec3 irradiance = vec3(0.0);
    float sampleCount = 0.0;
    for (float phi = 0.0; phi < TWO_PI; phi += DELTA_PHI) {
        for (float theta = 0.0; theta < HALF_PI; theta += DELTA_THETA) {
            // Spherical in tangent space
            vec3 tangent = vec3(sin(theta) * cos(phi),
                                sin(theta) * sin(phi),
                                cos(theta));
            // Tangent → world
            vec3 sampleDir = tangent.x * right + tangent.y * up + tangent.z * N;
            irradiance += texture(samplerCube(envTex, envSmp), sampleDir).rgb
                        * cos(theta) * sin(theta);
            sampleCount += 1.0;
        }
    }
    irradiance = PI * irradiance / sampleCount;
    frag_color = vec4(irradiance, 1.0);
}
@end
@program irradiance vs_bake fs_irradiance

// -----------------------------------------------------------------------------
// 3. Prefiltered environment map (specular IBL, one mip per roughness)
// -----------------------------------------------------------------------------
@fs fs_prefilter
layout(binding=0) uniform textureCube envTex;
layout(binding=0) uniform sampler envSmp;

layout(binding=0) uniform prefilter_params {
    vec4 faceIdx;  // x = face index
    vec4 params;   // x = roughness
};

in vec2 v_uv;
out vec4 frag_color;

const float PI = 3.14159265359;
const uint SAMPLE_COUNT = 1024u;

vec3 faceUvToDir(int f, vec2 uv) {
    vec2 n = uv * 2.0 - 1.0;
    if (f == 0) return normalize(vec3( 1.0, -n.y, -n.x));
    if (f == 1) return normalize(vec3(-1.0, -n.y,  n.x));
    if (f == 2) return normalize(vec3( n.x,  1.0,  n.y));
    if (f == 3) return normalize(vec3( n.x, -1.0, -n.y));
    if (f == 4) return normalize(vec3( n.x, -n.y,  1.0));
    return              normalize(vec3(-n.x, -n.y, -1.0));
}

float radicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint n) {
    return vec2(float(i) / float(n), radicalInverseVdC(i));
}

vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

void main() {
    int f = int(faceIdx.x + 0.5);
    float roughness = params.x;
    vec3 N = faceUvToDir(f, v_uv);
    vec3 R = N;
    vec3 V = R;  // standard split-sum approximation: isotropic NdotV

    vec3 prefilteredColor = vec3(0.0);
    float totalWeight = 0.0;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importanceSampleGGX(Xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            prefilteredColor += texture(samplerCube(envTex, envSmp), L).rgb * NdotL;
            totalWeight += NdotL;
        }
    }
    prefilteredColor /= max(totalWeight, 1e-5);
    frag_color = vec4(prefilteredColor, 1.0);
}
@end
@program prefilter vs_bake fs_prefilter

// -----------------------------------------------------------------------------
// 4. BRDF integration LUT (2D, split-sum scale + bias)
// -----------------------------------------------------------------------------
@fs fs_brdf_lut
in vec2 v_uv;
out vec4 frag_color;

const float PI = 3.14159265359;
const uint SAMPLE_COUNT = 1024u;

float radicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint n) {
    return vec2(float(i) / float(n), radicalInverseVdC(i));
}

vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    // Note: for IBL, k = alpha^2 / 2 (different from direct lighting)
    float a = roughness;
    float k = (a * a) / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySchlickGGX(NdotV, roughness)
         * geometrySchlickGGX(NdotL, roughness);
}

vec2 integrateBRDF(float NdotV, float roughness) {
    vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    vec3 N = vec3(0.0, 0.0, 1.0);

    float A = 0.0;
    float B = 0.0;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importanceSampleGGX(Xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);
        if (NdotL > 0.0) {
            float G = geometrySmith(max(N.z * V.z + 1e-5, 0.0), NdotL, roughness);
            // Note: Karis 2013 — G_Vis = G * VdotH / (NdotH * NdotV)
            float G_Vis = (G * VdotH) / max(NdotH * NdotV, 1e-5);
            float Fc = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    return vec2(A, B);
}

void main() {
    // v_uv.x = NdotV, v_uv.y = roughness (both in [0,1])
    vec2 integrated = integrateBRDF(v_uv.x, max(v_uv.y, 0.01));
    frag_color = vec4(integrated, 0.0, 1.0);
}
@end
@program brdf_lut vs_bake fs_brdf_lut
