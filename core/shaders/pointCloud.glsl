//------------------------------------------------------------------------------
//  pointCloud.glsl - GPU instanced fat-point renderer (tc::PointCloudRenderer)
//------------------------------------------------------------------------------
//  One unit quad (4 corner verts) is instanced once per point. The point center
//  is projected to clip space, then the quad corners are offset in NDC (screen-
//  aligned), so every splat is a screen-facing square regardless of the camera
//  matrix convention. Aspect-corrected so the splat is square in pixels.
//
//  Regenerate the header with (from the repo root):
//    core/tools/sokol-shdc/sokol-shdc.exe \
//      -i core/shaders/pointCloud.glsl \
//      -o core/include/tc/gpu/shaders/pointCloud.glsl.h \
//      -l metal_macos:metal_ios:hlsl5:glsl300es:glsl430:wgsl --ifdef
//------------------------------------------------------------------------------
@vs vs
layout(binding=0) uniform vs_params {
    mat4 viewProj;   // currentProjection * currentView
    vec4 params;     // x = size (NDC fraction), y = round (0/1), z = aspect (h/w)
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
    color = inColor;
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
    // Optional round splat: discard the quad corners outside the inscribed disc.
    if (vRound > 0.5 && dot(uv, uv) > 0.25) discard;
    frag = color;
}
@end

@program point_cloud vs fs
