// =============================================================================
// computeTextureExample - Generate an image in a compute shader.
//
// A storage image receives compute output and is drawn as a regular texture.
// Compute requires a supported backend; Web builds use WebGPU.
// =============================================================================

#include "tcApp.h"
#include "texture.compute.glsl.h"

void tcApp::setup() {
    output.allocate(256, 256, TextureFormat::RGBA8, TextureUsage::Storage);
    compute.load(generate_texture_shader_desc);
    compute.setStorageImage(VIEW_output_image, output);
}

void tcApp::update() {
    compute.dispatch(32, 32, 1);
}

void tcApp::draw() {
    output.draw(0, 0);
}
