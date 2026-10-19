@vs reload_probe_vs
in vec4 position;
void main() {
    gl_Position = position;
}
@end

@fs reload_probe_fs
out vec4 frag_color;
void main() {
    frag_color = vec4(0.25, 0.0, 0.0, 1.0);
}
@end

@program reload_probe reload_probe_vs reload_probe_fs
