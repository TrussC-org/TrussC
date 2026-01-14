// =============================================================================
// ycocg.glsl - YCoCg to RGB conversion shader for HAP-Q playback
// =============================================================================
// HAP-Q encodes video in Scaled YCoCg-DXT5 format. This shader converts
// the YCoCg color space back to RGB for correct display.
//
// DXT5 texture channels:
//   R = Co (chrominance orange)
//   G = Cg (chrominance green)
//   B = Scale factor
//   A = Y (luminance)
// =============================================================================

@vs vs_ycocg
layout(location=0) in vec3 position;
layout(location=1) in vec2 texcoord0;
layout(location=2) in vec4 color0;

layout(binding=0) uniform vs_params {
    vec2 screenSize;
    vec2 _pad;
};

out vec2 uv;
out vec4 vertColor;

void main() {
    // Convert screen coordinates to NDC
    vec2 ndc = (position.xy / screenSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;  // Flip Y for OpenGL coordinate system
    gl_Position = vec4(ndc, position.z, 1.0);
    uv = texcoord0;
    vertColor = color0;
}
@end

@fs fs_ycocg
in vec2 uv;
in vec4 vertColor;

layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

out vec4 frag_color;

void main() {
    // Sample YCoCg texture (BC3/DXT5)
    vec4 cocgsy = texture(sampler2D(tex, smp), uv);

    // Scaled YCoCg-DXT5 decode
    // Scale factor is stored in B channel: scale = (B * 255 / 8) + 1
    float scale = (cocgsy.b * (255.0 / 8.0)) + 1.0;

    // Chrominance values (centered at 0.5, scaled)
    float Co = (cocgsy.r - 0.5) / scale;
    float Cg = (cocgsy.g - 0.5) / scale;

    // Luminance is in alpha channel
    float Y = cocgsy.a;

    // YCoCg to RGB conversion
    float R = Y + Co - Cg;
    float G = Y + Cg;
    float B = Y - Co - Cg;

    // DEBUG: Force blue to test if shader is applied
    frag_color = vec4(R, G, 1.0, 1.0) * vertColor;
}
@end

@program ycocg vs_ycocg fs_ycocg
