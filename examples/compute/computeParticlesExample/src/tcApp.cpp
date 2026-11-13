// =============================================================================
// computeParticlesExample - Update particle positions on the GPU and draw them.
// Particles chase the mouse; click to scatter them.
//
// A compute pass writes particle storage that the vertex shader then reads.
// Vertex storage reads require GPU support; Raspberry Pi 5 V3D lacks it.
// =============================================================================

#include "tcApp.h"
#include "step.compute.glsl.h"
#include "particles.compute.glsl.h"

void tcApp::setup() {
    // random start: anywhere on screen, with a slow velocity (y is always positive)
    vector<Vec4> initial(count);
    for (auto& p : initial) p = Vec4(random(-1, 1), random(-1, 1), random(-0.2f, 0.2f), random(0.05f, 0.2f));

    // copy the array to the GPU once; from here on it only lives there
    particles.allocate(initial.size() * sizeof(Vec4), initial.data());

    // the compute shader's "particle_state" buffer is this StorageBuffer
    step.load(step_particles_shader_desc);
    step.setStorageBuffer(VIEW_particle_state, particles);

    // the vertex shader's "particle_input" is the same buffer, so nothing is copied back to the CPU
    points.load(draw_particles_shader_desc);
    points.setStorageBuffer(VIEW_particle_input, particles);
}

void tcApp::update() {
    // the mouse in clip space (-1..1, y up), the space the particles live in
    float mx = getMouseX() / getWindowWidth() * 2 - 1;
    float my = 1 - getMouseY() / getWindowHeight() * 2;

    // x = seconds since last frame, y = particle count (threads past it do nothing),
    // zw = mouse position the particles are pulled toward
    step.setUniform(UB_step_params, Vec4(getDeltaTime(), count, mx, my));

    // x = kick strength (0 on frames without a click), y = random seed for this kick
    step.setUniform(UB_kick_params, Vec4(kick, random(0, 1000000), 0, 0));
    kick = 0;

    // one thread per particle: count / 64 groups x 64 threads (local_size_x in step.compute.glsl)
    step.dispatch(count / 64);
}

void tcApp::draw() {
    clear(0.02f, 0.02f, 0.04f);

    // 3 vertices x count instances: each instance is one particle's triangle
    points.drawVertices(3, count);
}

void tcApp::mousePressed(const MouseEventArgs& e) {
    // the particles live only on the GPU, so the kick itself happens in
    // step.compute.glsl; here we only ask for one on the next update()
    kick = 2.0f;
}
