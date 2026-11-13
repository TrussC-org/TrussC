@module orientation
@vs vs
in vec2 position;
in vec2 texcoord0;
out vec2 uv;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    uv = texcoord0;
}
@end
@fs fs
layout(binding=0) uniform texture2D source;
layout(binding=0) uniform sampler sourceSampler;
layout(binding=0) uniform params { vec4 mip; };
in vec2 uv;
out vec4 frag_color;
void main() {
    frag_color = textureLod(sampler2D(source, sourceSampler), uv, mip.x);
}
@end
@program sample vs fs
