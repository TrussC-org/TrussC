// =============================================================================
// computeTextureExample - Generate an image in a compute shader.
//
// A storage image receives compute output and is drawn as a regular texture.
// Compute requires a supported backend; Web builds use WebGPU.
// =============================================================================

#include "tcApp.h"
#include "texture.compute.glsl.h"

void tcApp::setup() {
    // Storage usage lets a compute shader write it; it still draws like any texture
    output.allocate(256, 256, TextureFormat::RGBA8, TextureUsage::Storage);

    compute.load(generate_texture_shader_desc);

    // the shader's "output_image" is this texture
    compute.setStorageImage(VIEW_output_image, output);
}

void tcApp::update() {
    // 32x32 groups of 8x8 threads (local_size) = 256x256, one thread per pixel.
    // The image is the same every frame; pass a time uniform to animate it
    compute.dispatch(32, 32, 1);
}

void tcApp::draw() {
    // the compute result, drawn as a normal texture
    output.draw(0, 0);
}
