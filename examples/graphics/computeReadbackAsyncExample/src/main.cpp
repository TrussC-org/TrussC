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
        counter.readAsync([this](ReadbackResult result) {
            if (result.ok()) count = result.get<uint32_t>(0);
        }); // The callback runs in a later update.
    }
    void draw() override {
        drawBitmapString("Values >= 64: " + to_string(count), 20, 30);
    }
};
int main() { return runApp<tcApp>(WindowSettings().setSize(480, 100)); }
