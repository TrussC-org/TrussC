#include <TrussC.h>
#include "texture.compute.glsl.h"
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    Texture output;
    ComputeShader compute;
    void setup() override {
        output.allocate(256, 256, TextureFormat::RGBA8, TextureUsage::Storage);
        compute.load(generate_texture_shader_desc);
        compute.setStorageImage(VIEW_output_image, output);
    }
    void update() override { compute.dispatch(32, 32, 1); }
    void draw() override { output.draw(0, 0); }
};
int main() { return runApp<tcApp>(WindowSettings().setSize(256, 256)); }
