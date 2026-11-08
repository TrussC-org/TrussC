// Vertex storage reads need GPU support; Pi 5 (GLES 3.1, V3D) has none; fragment and compute reads work.
#include <TrussC.h>
#include "step.compute.glsl.h"
#include "particles.compute.glsl.h"
using namespace std;
using namespace tc;

class tcApp : public App {
    static constexpr int count = 2048;
    StorageBuffer particles;
    ComputeShader step;
    Shader points;
public:
    void setup() override {
        vector<Vec4> initial(count);
        for (auto& p : initial) p = Vec4(random(-1, 1), random(-1, 1), random(-0.2f, 0.2f), random(0.05f, 0.2f));
        particles.allocate(initial.size() * sizeof(Vec4), initial.data());
        step.load(step_particles_shader_desc);
        step.setStorageBuffer(VIEW_particle_state, particles);
        points.load(draw_particles_shader_desc);
        points.setStorageBuffer(VIEW_particle_input, particles);
    }
    void update() override {
        step.setUniform(UB_step_params, Vec4(getDeltaTime(), count, 0, 0));
        step.dispatch(count / 64);
    }
    void draw() override {
        clear(0.02f, 0.02f, 0.04f);
        points.drawVertices(3, count);
    }
};
int main() { return runApp<tcApp>(WindowSettings().setSize(640, 480)); }
