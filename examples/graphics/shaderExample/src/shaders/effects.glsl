// =============================================================================
// effects.glsl - 4 shader effects (sokol-shdc format)
// =============================================================================

// Common vertex shader
@vs vs
in vec2 position;
in vec2 texcoord0;
out vec2 uv;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    uv = texcoord0;
}
@end

// Common uniform block
@block uniforms
layout(binding=0) uniform fs_params {
    float time;
    float _pad0;
    float _pad1;
    float _pad2;
    vec4 resolution;
    vec4 mouse;
    vec4 custom;
};
@end

// =============================================================================
// Effect 0: Gradient - Rainbow gradient
// =============================================================================
@fs fs_gradient
@include_block uniforms

in vec2 uv;
out vec4 frag_color;

void main() {
    vec3 col = 0.5 + 0.5 * cos(time + uv.xyx + vec3(0.0, 2.0, 4.0));
    frag_color = vec4(col, 1.0);
}
@end

@program gradient vs fs_gradient

// =============================================================================
// Effect 1: Ripple - Wave ripple effect
// =============================================================================
@fs fs_ripple
@include_block uniforms

in vec2 uv;
out vec4 frag_color;

void main() {
    vec2 p = uv - 0.5;
    float aspect = resolution.x / resolution.y;
    p.x *= aspect;

    float dist = length(p);
    float wave = sin(dist * 20.0 - time * 3.0) * 0.5 + 0.5;
    wave *= 1.0 - smoothstep(0.0, 0.5, dist);

    vec3 col = mix(vec3(0.1, 0.1, 0.3), vec3(0.3, 0.6, 1.0), wave);
    frag_color = vec4(col, 1.0);
}
@end

@program ripple vs fs_ripple

// =============================================================================
// Effect 2: Plasma - Classic plasma effect
// =============================================================================
@fs fs_plasma
@include_block uniforms

in vec2 uv;
out vec4 frag_color;

void main() {
    vec2 p = uv * 4.0;
    float t = time * 0.5;

    float v1 = sin(p.x + t);
    float v2 = sin(p.y + t);
    float v3 = sin(p.x + p.y + t);
    float v4 = sin(length(p - vec2(2.0)) + t);

    float v = v1 + v2 + v3 + v4;

    vec3 col;
    col.r = sin(v * 3.14159) * 0.5 + 0.5;
    col.g = sin(v * 3.14159 + 2.094) * 0.5 + 0.5;
    col.b = sin(v * 3.14159 + 4.188) * 0.5 + 0.5;

    frag_color = vec4(col, 1.0);
}
@end

@program plasma vs fs_plasma

// =============================================================================
// Effect 3: Mouse - Mouse follow glow effect
// =============================================================================
@fs fs_mouse
@include_block uniforms

in vec2 uv;
out vec4 frag_color;

void main() {
    vec2 mouseUV = mouse.xy / resolution.xy;
    float aspect = resolution.x / resolution.y;

    // Adjust for aspect ratio
    vec2 p = uv;
    vec2 m = mouseUV;
    p.x *= aspect;
    m.x *= aspect;

    float dist = distance(p, m);
    float glow = 0.05 / (dist + 0.01);
    glow = clamp(glow, 0.0, 1.0);

    vec3 col = vec3(glow * 0.5, glow * 0.8, glow);

    // Background grid
    vec2 grid = fract(uv * 20.0);
    float gridLine = step(0.95, grid.x) + step(0.95, grid.y);
    col += gridLine * 0.1;

    frag_color = vec4(col, 1.0);
}
@end

@program mouse_follow vs fs_mouse
