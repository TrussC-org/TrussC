#include <TrussC.h>
#include "count.compute.glsl.h"
using namespace std;
using namespace tc;

class tcApp : public App {
    StorageBuffer counter;
    ComputeShader compute;
    uint32_t count = 0;
public:
    void setup() override {
        counter.allocate(sizeof(count), &count);
        compute.load(count_values_shader_desc);
        compute.setStorageBuffer(VIEW_counter, counter);
        compute.dispatch(4); // Count the integers 0..127 that are at least 64.
#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
        counter.read(&count, sizeof(count)); // Blocks until the GPU finishes.
#endif
    }
    void draw() override {
#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
        drawBitmapString("Values >= 64: " + to_string(count), 20, 30);
#else
        drawBitmapString("Synchronous readback is unavailable on Web", 20, 30);
#endif
    }
};
int main() { return runApp<tcApp>(WindowSettings().setSize(480, 100)); }
