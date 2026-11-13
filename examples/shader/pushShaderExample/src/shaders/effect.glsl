// =============================================================================
// effect.glsl - Rainbow color effect shader for demonstrating pushShader()
// =============================================================================
// This shader applies a time-based rainbow effect to any geometry.
// Works with: drawRect, drawCircle, drawTriangle, beginShape/endShape, Mesh, StrokeMesh
// NOTE: drawLine is not supported (use StrokeMesh instead)
// =============================================================================

@vs vs_effect
layout(location=0) in vec3 position;
layout(location=1) in vec2 texcoord0;
layout(location=2) in vec4 color0;

layout(binding=1) uniform vs_params {
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

@fs fs_rainbow
in vec2 uv;
in vec4 vertColor;

layout(binding=0) uniform effect_params {
    float time;
    float effectStrength;
    float _pad0;
    float _pad1;
};

out vec4 frag_color;

// HSV to RGB conversion
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    // Rainbow effect based on position and time
    float hue = fract(gl_FragCoord.x * 0.003 + gl_FragCoord.y * 0.003 + time * 0.5);
    vec3 rainbow = hsv2rgb(vec3(hue, 0.8, 1.0));

    // Mix with vertex color
    vec3 finalColor = mix(vertColor.rgb, rainbow, effectStrength);
    frag_color = vec4(finalColor, vertColor.a);
}
@end

@program rainbow vs_effect fs_rainbow
