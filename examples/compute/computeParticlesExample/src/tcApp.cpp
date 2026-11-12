// =============================================================================
// computeParticlesExample - Update particle positions on the GPU and draw them.
//
// A compute pass writes particle storage that the vertex shader then reads.
// Vertex storage reads require GPU support; Raspberry Pi 5 V3D lacks it.
// =============================================================================

#include "tcApp.h"
#include "step.compute.glsl.h"
#include "particles.compute.glsl.h"

void tcApp::setup() {
    vector<Vec4> initial(count);
    for (auto& p : initial) p = Vec4(random(-1, 1), random(-1, 1), random(-0.2f, 0.2f), random(0.05f, 0.2f));
    particles.allocate(initial.size() * sizeof(Vec4), initial.data());
    step.load(step_particles_shader_desc);
    step.setStorageBuffer(VIEW_particle_state, particles);
    points.load(draw_particles_shader_desc);
    points.setStorageBuffer(VIEW_particle_input, particles);
}

void tcApp::update() {
    step.setUniform(UB_step_params, Vec4(getDeltaTime(), count, 0, 0));
    step.dispatch(count / 64);
}

void tcApp::draw() {
    clear(0.02f, 0.02f, 0.04f);
    points.drawVertices(3, count);
}
