//------------------------------------------------------------------------------
//  pointCloud.glsl - GPU instanced fat-point splat for Mesh(PrimitiveMode::Points)
//------------------------------------------------------------------------------
//  One unit quad (4 corner verts, triangle strip) is instanced once per point.
//  The point center is projected to clip space, then the quad corners are offset
//  in NDC (screen-aligned), so every splat is a screen-facing square regardless
//  of the camera matrix convention. The offset is aspect-corrected so the splat
//  is square in pixels.
//
//  Size is passed as an NDC half-extent derived from a logical-pixel point size
//  on the CPU (params.x = 2 * pointSizePx / windowHeightLogical), matching the
//  unit convention of strokeWeight.
//
//  Round splats use analytic coverage (fwidth/smoothstep) written into alpha and
//  resolved by alpha-to-coverage, so the disc edge is anti-aliased and sub-pixel
//  points fade instead of shimmering — without alpha blending, so depth stays
//  correct and order-independent (requires a multisampled target).
//
//  The header is generated automatically by the build (sokol-shdc, see
//  core/CMakeLists.txt) into core/include/tc/gpu/shaders/pointCloud.glsl.h.
//------------------------------------------------------------------------------
@module tc_pointcloud

@vs vs
layout(binding=0) uniform vs_params {
    mat4 viewProj;   // currentProjection * currentView (transposed for column-major)
    vec4 params;     // x = size (NDC half-extent), y = round (0/1), z = aspect (h/w), w = unused
    vec4 tint;       // multiplied into every point color (carries the draw color for uncolored clouds)
};

in vec2 corner;      // buffer 0, per-vertex: quad corner in [-0.5, 0.5]
in vec3 inPos;       // buffer 1, per-instance: point position (world)
in vec4 inColor;     // buffer 1, per-instance: point color (rgba)

out vec4 color;
out vec2 uv;
out float vRound;

void main() {
    vec4 clip = viewProj * vec4(inPos, 1.0);
    vec2 off = corner * params.x;   // screen-aligned offset (NDC)
    off.x *= params.z;              // aspect-correct -> square in pixels
    clip.xy += off * clip.w;        // offset in NDC (pre-divide)
    gl_Position = clip;
    color = inColor * tint;
    uv = corner;
    vRound = params.y;
}
@end

@fs fs
in vec4 color;
in vec2 uv;
in float vRound;
out vec4 frag;

void main() {
    float a = color.a;
    if (vRound > 0.5) {
        // Analytic edge coverage, fed to alpha-to-coverage. uv is [-0.5,0.5];
        // d = 0 at center, 1 at the inscribed circle.
        float d   = length(uv) * 2.0;
        float w   = fwidth(d);
        float cov = 1.0 - smoothstep(1.0 - w, 1.0 + w, d);
        a *= cov;
    }
    frag = vec4(color.rgb, a);
}
@end

@program point vs fs

//------------------------------------------------------------------------------
//  point_prim - true 1px GPU point primitive (PointStyle::Pixel)
//  Draws the per-point buffer directly as POINTS (one vertex per point, no quad
//  expansion). Size is fixed at 1 device pixel (GL/Metal honor gl_PointSize;
//  D3D11/WebGPU clamp to 1px). The lightest path: 1 vertex/point, no instancing.
//------------------------------------------------------------------------------
@vs vs_prim
layout(binding=0) uniform vs_params {
    mat4 viewProj;
    vec4 params;     // unused here; kept so the uniform block matches the splat vs
    vec4 tint;
};

in vec3 inPos;       // per-vertex: point position (world)
in vec4 inColor;     // per-vertex: point color (rgba)

out vec4 color;

void main() {
    gl_Position = viewProj * vec4(inPos, 1.0);
    gl_PointSize = 1.0;
    color = inColor * tint;
}
@end

@fs fs_prim
in vec4 color;
out vec4 frag;

void main() {
    frag = color;
}
@end

@program point_prim vs_prim fs_prim
