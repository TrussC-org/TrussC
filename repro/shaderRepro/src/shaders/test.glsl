// Test shaders for deferred-draw bug repro
// program "solid":   fills geometry with a uniform color (ignores vertex color)
// program "texquad": samples a bound texture

@vs vs_basic
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
    vec2 ndc = (position.xy / screenSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, position.z, 1.0);
    uv = texcoord0;
    vertColor = color0;
}
@end

@fs fs_solid
layout(binding=0) uniform solid_params {
    vec4 solidColor;
};

in vec2 uv;
in vec4 vertColor;
out vec4 frag_color;

void main() {
    frag_color = solidColor;
}
@end

@fs fs_tex
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
in vec4 vertColor;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(tex, smp), uv);
}
@end

@program solid vs_basic fs_solid
@program texquad vs_basic fs_tex
