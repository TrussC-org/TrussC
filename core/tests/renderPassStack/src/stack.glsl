@module stack
@vs mesh_vs
in vec3 position;
in vec2 texcoord0;
in vec4 color0;
out vec4 tint;
void main() {
    gl_Position = vec4(position, 1.0);
    tint = color0 + vec4(texcoord0, 0.0, 0.0) * 0.001;
}
@end
@fs mesh_fs
in vec4 tint;
out vec4 frag_color;
void main() { frag_color = tint; }
@end
@program mesh mesh_vs mesh_fs

@vs full_vs
in vec2 position;
in vec2 texcoord0;
out vec2 uv;
void main() { gl_Position = vec4(position, 0.0, 1.0); uv = texcoord0; }
@end
@fs full_fs
in vec2 uv;
out vec4 frag_color;
void main() {
    // Asymmetric quadrants catch flips and stale readbacks.
    frag_color = vec4(uv.x < 0.5 ? 1.0 : 0.0, uv.y < 0.5 ? 1.0 : 0.0, 0.0, 1.0);
}
@end
@program full full_vs full_fs

// A user fullscreen shader which leaves the alpha-quad probe untouched.
@fs discard_fs
in vec2 uv;
out vec4 frag_color;
void main() {
    if (uv.x < 0.75) discard;
    frag_color = vec4(0.0, 1.0, 0.0, 1.0);
}
@end
@program discard full_vs discard_fs

@fs cube_fs
layout(binding=0) uniform textureCube cubeTex;
layout(binding=0) uniform sampler cubeSmp;
layout(binding=0) uniform cube_params { vec4 directionLod; };
in vec2 uv;
out vec4 frag_color;
void main() {
    frag_color = textureLod(samplerCube(cubeTex, cubeSmp), directionLod.xyz + vec3((uv - vec2(0.5))*0.01, 0.0), directionLod.w);
}
@end
@program cube full_vs cube_fs
